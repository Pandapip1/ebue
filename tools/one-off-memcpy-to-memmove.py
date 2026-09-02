#!/usr/bin/env python3
"""Rewrite reviewed, overlap-capable memcpy findings to memmove.

The memory-contract report is the source of locations, but not every unproved
overlap should be weakened to an overlap-capable operation: fresh allocations
and logically separate internal buffers should remain memcpy so they continue
to exercise disjointness inference.  APPROVED_FUNCTIONS contains only APIs
whose valid inputs can actually alias (not merely cases the checker currently
fails to distinguish).  Dry-run by default; pass --apply to rewrite.
"""

from __future__ import annotations

import argparse
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "obj/lint/x86_64.memory-contract.report"
OVERLAP = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+): memcpy ranges are not proven "
    r"nonoverlapping in (?P<function>[A-Za-z_]\w*): (?P<call>memcpy\(.*\))$"
)

# Reviewed alias semantics:
# - fill_current: the caller-provided result buffer may itself point into the
#   environment/name storage used as a source.
# - __file_read/__file_write: fmemopen explicitly exposes its backing buffer,
#   so a caller may validly use a region of that buffer as the I/O buffer.
# - __fread/__fwrite: the same public-buffer alias reaches the stdio staging
#   buffer paths; memmove preserves the required byte sequence.
APPROVED_FUNCTIONS = {
    ("src/misc/pwd.c", "fill_current"),
    ("src/stdio/buf.c", "__file_read"),
    ("src/stdio/buf.c", "__file_write"),
    ("src/stdio/rw.c", "__fread"),
    ("src/stdio/rw.c", "__fwrite"),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=pathlib.Path, default=DEFAULT_REPORT)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    edits: dict[pathlib.Path, list[int]] = {}
    skipped = 0
    for raw in args.report.read_text().splitlines():
        finding = OVERLAP.match(raw)
        if not finding:
            continue
        key = (finding["file"], finding["function"])
        if key not in APPROVED_FUNCTIONS:
            print("skip (disjointness proof retained): " + raw)
            skipped += 1
            continue
        path = ROOT / finding["file"]
        edits.setdefault(path, []).append(int(finding["line"]))
        print("rewrite: " + raw)

    changed = 0
    for path, line_numbers in sorted(edits.items()):
        lines = path.read_text().splitlines(keepends=True)
        for line_number in sorted(set(line_numbers)):
            index = line_number - 1
            if index >= len(lines) or "memcpy(" not in lines[index]:
                raise SystemExit(
                    f"stale diagnostic: {path.relative_to(ROOT)}:{line_number}"
                )
            lines[index] = lines[index].replace("memcpy(", "memmove(", 1)
            changed += 1
        if args.apply:
            path.write_text("".join(lines))

    print(f"{changed} reviewed replacement(s); {skipped} proof case(s) kept")
    if not args.apply:
        print("dry run; pass --apply to rewrite")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
