#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check allocator provenance, single consumption, and borrow lifetimes."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tools/lint-ownership-fixtures"
DIAGNOSTIC = re.compile(
    r"^(.*?):(\d+):(\d+): warning: "
    r"(deallocator argument is not proven owned|reallocator argument is not proven owned|"
    r"ownership is already consumed|"
    r"borrow accesses a consumed owner); origin '(.*)'; context '(.*)'; "
    r"expression '(.*)'; site '(.*)' \[ntlibc\.Ownership\]$"
)


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    reason: str
    context: str
    expression: str
    site: str
    line: int

    @property
    def key(self) -> tuple[str, str, str, str, str]:
        return self.path, self.reason, self.context, self.expression, self.site


def relative(name: str) -> str:
    path = pathlib.Path(name)
    if path.is_absolute():
        try:
            return path.relative_to(ROOT).as_posix()
        except ValueError:
            return path.as_posix()
    return path.as_posix()


def parse_log(path: pathlib.Path) -> list[Finding]:
    text = path.read_text(encoding="utf-8", errors="replace")
    if "PLEASE submit a bug report" in text or "clang frontend command failed" in text:
        raise SystemExit(f"lint-ownership: analyzer crashed; see {path}")
    findings = []
    for line in text.splitlines():
        match = DIAGNOSTIC.match(line)
        if match:
            findings.append(Finding(relative(match.group(5)), match.group(4),
                                    match.group(6), match.group(7),
                                    f"{match.group(8)} @column {match.group(3)}",
                                    int(match.group(2))))
    return findings


def fixture_test(path: pathlib.Path) -> None:
    expected = set()
    for source in FIXTURES.glob("*.c"):
        for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
            if "ownership-expect" in line:
                expected.add((source.relative_to(ROOT).as_posix(), number))
    actual = {(finding.path, finding.line) for finding in parse_log(path)}
    if actual != expected:
        print("lint-ownership: fixture self-test failed", file=sys.stderr)
        print(f"  expected: {sorted(expected)}", file=sys.stderr)
        print(f"  actual:   {sorted(actual)}", file=sys.stderr)
        raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", required=True, type=pathlib.Path)
    parser.add_argument("logs", nargs="*", type=pathlib.Path)
    arguments = parser.parse_args()
    fixture_test(arguments.fixtures)

    findings = {finding.key: finding
                for log in arguments.logs for finding in parse_log(log)}
    for finding in sorted(findings.values()):
        print(f"{finding.path}:{finding.line}: {finding.reason} in "
              f"{finding.context}: {finding.expression}")
    if findings:
        releases = sum(finding.reason.endswith("argument is not proven owned")
                       for finding in findings.values())
        repeats = sum(finding.reason == "ownership is already consumed"
                      for finding in findings.values())
        borrows = len(findings) - releases - repeats
        print(f"lint-ownership: {releases} unproved release(s), "
              f"{repeats} repeated consumption(s), "
              f"{borrows} expired borrow access(es)")
        return 1
    print("lint-ownership: no findings (fixtures passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
