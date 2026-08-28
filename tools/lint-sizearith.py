#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reject unchecked allocation arithmetic, capacity growth, and narrowing.

This is a deliberately small C lexer, not a regular-expression grep.  It
keeps source positions while masking comments and literals, balances call
parentheses, and examines the actual size argument of the allocators used by
this tree.  Existing debt is recorded by exact source expression in
tools/sizearith-known.txt; new sites and stale entries both fail the stage.

USHORT casts additionally accept a preceding __US_MAX_WCHARS, USHRT_MAX,
0xffff, or 65535 bound in their function.  Use a ``sizearith-safe: reason``
comment on the same or preceding line only when a bound is proved by
construction.  Checked arithmetic should normally be expressed through
src/internal/libc.h's __size_* and __array_next_capacity helpers instead.
"""

from __future__ import annotations

import collections
import os
import pathlib
import re
import sys
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parent.parent
KNOWN = ROOT / "tools/sizearith-known.txt"
FIXTURES = ROOT / "tools/lint-sizearith-fixtures"
ALLOC_SIZE_ARGS = {
    "malloc": (0,),
    "__malloc": (0,),
    "realloc": (1,),
    "__realloc": (1,),
    "calloc": (0, 1),
    "reallocarray": (1, 2),
    "RtlAllocateHeap": (2,),
    "RtlReAllocateHeap": (3,),
}
TOKEN = re.compile(
    r"[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+|\d+|<<=|>>=|\*=|\+=|-=|"
    r"<<|>>|->|\+\+|--|&&|\|\||==|!=|<=|>=|[^\s]"
)
GROWTH_NAME = re.compile(r"(?:^|_)(?:cap|capacity|newcap|new_cap|nc)$|cap", re.I)
LENGTH_NAME = re.compile(
    r"^(?:n|used|total|argc)$|len|length|size|count|cap|bytes?|alloc|"
    r"wordc|bufsz|nslot|nprog",
    re.I,
)


@dataclass(frozen=True)
class Tok:
    text: str
    start: int
    end: int
    line: int


@dataclass(frozen=True)
class Site:
    rule: str
    path: str
    line: int
    snippet: str

    @property
    def key(self) -> tuple[str, str, str]:
        return self.rule, self.path, self.snippet


def mask_noncode(source: str) -> str:
    out = list(source)
    state = "code"
    i = 0
    while i < len(source):
        c = source[i]
        n = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if c == "/" and n == "/":
                out[i] = out[i + 1] = " "
                state = "line"
                i += 2
                continue
            if c == "/" and n == "*":
                out[i] = out[i + 1] = " "
                state = "block"
                i += 2
                continue
            if c == '"':
                out[i] = " "
                state = "string"
            elif c == "'":
                out[i] = " "
                state = "char"
        elif state == "line":
            if c == "\n":
                state = "code"
            else:
                out[i] = " "
        elif state == "block":
            if c == "*" and n == "/":
                out[i] = out[i + 1] = " "
                state = "code"
                i += 2
                continue
            if c != "\n":
                out[i] = " "
        else:
            out[i] = " "
            if c == "\\" and i + 1 < len(source):
                if source[i + 1] != "\n":
                    out[i + 1] = " "
                i += 2
                continue
            if (state == "string" and c == '"') or (state == "char" and c == "'"):
                state = "code"
        i += 1
    return "".join(out)


def lex(masked: str) -> list[Tok]:
    starts = [0]
    for m in re.finditer("\n", masked):
        starts.append(m.end())
    toks = []
    line = 0
    for m in TOKEN.finditer(masked):
        while line + 1 < len(starts) and starts[line + 1] <= m.start():
            line += 1
        toks.append(Tok(m.group(), m.start(), m.end(), line + 1))
    return toks


def pairs(toks: list[Tok], left: str, right: str) -> dict[int, int]:
    stack: list[int] = []
    result: dict[int, int] = {}
    for i, tok in enumerate(toks):
        if tok.text == left:
            stack.append(i)
        elif tok.text == right and stack:
            j = stack.pop()
            result[j] = i
    return result


def split_args(toks: list[Tok], begin: int, end: int) -> list[tuple[int, int]]:
    result = []
    start = begin
    depth = 0
    for i in range(begin, end):
        if toks[i].text in ("(", "[", "{"):
            depth += 1
        elif toks[i].text in (")", "]", "}"):
            depth -= 1
        elif toks[i].text == "," and depth == 0:
            result.append((start, i))
            start = i + 1
    result.append((start, end))
    return result


def sizeof_ignored(toks: list[Tok], parens: dict[int, int]) -> set[int]:
    ignored: set[int] = set()
    i = 0
    while i < len(toks):
        if toks[i].text != "sizeof":
            i += 1
            continue
        ignored.add(i)
        j = i + 1
        if j < len(toks) and toks[j].text == "(" and j in parens:
            k = parens[j]
            ignored.update(range(j, k + 1))
            i = k + 1
            continue
        if j < len(toks) and toks[j].text == "*":
            ignored.add(j)
            j += 1
        if j < len(toks):
            ignored.add(j)
        i = j + 1
    return ignored


def has_arithmetic(toks: list[Tok], begin: int, end: int, ignored: set[int]) -> bool:
    have_operand = False
    for i in range(begin, end):
        if i in ignored:
            if toks[i].text == "sizeof":
                have_operand = True
            continue
        text = toks[i].text
        if text in ("+", "-", "*", "<<", ">>"):
            if have_operand:
                return True
            continue
        if text not in ("(", "[", "{", ",", "?", ":", "="):
            have_operand = True
    return False


def normalise(source: str, begin: int, end: int) -> str:
    return " ".join(source[begin:end].split()).replace("\t", " ")


def exempt(source_lines: list[str], line: int) -> bool:
    here = source_lines[line - 1] if 0 < line <= len(source_lines) else ""
    prev = source_lines[line - 2] if line > 1 else ""
    return "sizearith-safe:" in here or "sizearith-safe:" in prev


def ushort_guarded(toks: list[Tok], cast: int, braces: dict[int, int]) -> bool:
    """Return whether a USHORT bound appears before this cast's function body."""
    containing = [begin for begin, end in braces.items() if begin < cast < end]
    if not containing:
        return False
    body = min(containing)
    for tok in toks[body + 1:cast]:
        low = tok.text.lower()
        if (low in ("__us_max_wchars", "ushrt_max", "65535") or
                re.fullmatch(r"0xffff(?:u|ul|ull)?", low)):
            return True
    return False


def scan(path: pathlib.Path) -> list[Site]:
    source = path.read_text(encoding="utf-8")
    lines = source.splitlines()
    toks = lex(mask_noncode(source))
    parens = pairs(toks, "(", ")")
    braces = pairs(toks, "{", "}")
    ignored = sizeof_ignored(toks, parens)
    rel = path.relative_to(ROOT).as_posix()
    sites: list[Site] = []
    allocation_spans: list[tuple[int, int]] = []

    for i, tok in enumerate(toks[:-1]):
        argnos = ALLOC_SIZE_ARGS.get(tok.text)
        if argnos is None or toks[i + 1].text != "(" or i + 1 not in parens:
            continue
        close = parens[i + 1]
        args = split_args(toks, i + 2, close)
        bad = False
        for argno in argnos:
            if argno < len(args):
                begin, end = args[argno]
                bad = bad or (begin < end and has_arithmetic(toks, begin, end, ignored))
        if not bad:
            continue
        line = tok.line
        if exempt(lines, line):
            continue
        sites.append(Site("allocation-arithmetic", rel, line,
                          normalise(source, tok.start, toks[close].end)))
        allocation_spans.append((i, close))

    def inside_allocation(index: int) -> bool:
        return any(a <= index <= b for a, b in allocation_spans)

    for i, tok in enumerate(toks):
        if inside_allocation(i):
            continue
        found = False
        end = i
        if GROWTH_NAME.search(tok.text):
            if i + 1 < len(toks) and toks[i + 1].text in ("*=", "+=", "<<="):
                found, end = True, min(i + 2, len(toks) - 1)
            elif (i + 2 < len(toks) and toks[i + 1].text == "*" and
                  i + 1 not in ignored and re.fullmatch(r"(?:0[xX][0-9A-Fa-f]+|\d+)",
                                                        toks[i + 2].text)):
                found, end = True, i + 2
            elif (i + 2 < len(toks) and toks[i + 1].text == "<<" and
                  toks[i + 2].text == "1"):
                found, end = True, i + 2
        elif (re.fullmatch(r"(?:0[xX][0-9A-Fa-f]+|\d+)", tok.text) and
              i + 2 < len(toks) and toks[i + 1].text == "*" and
              i + 1 not in ignored and GROWTH_NAME.search(toks[i + 2].text)):
            found, end = True, i + 2
        if found:
            line = tok.line
            if not exempt(lines, line):
                sites.append(Site("unchecked-growth", rel, line,
                                  normalise(source, tok.start, toks[end].end)))

    for i in range(len(toks) - 3):
        if toks[i].text != "(" or toks[i + 2].text != ")":
            continue
        ctype = toks[i + 1].text
        if ctype not in ("USHORT", "ULONG", "int"):
            continue
        if i > 0 and toks[i - 1].text == "sizeof":
            continue
        begin = i + 3
        end = begin + 1
        if toks[begin].text == "(" and begin in parens:
            end = parens[begin] + 1
        elif toks[begin].text in ("+", "-", "*", "~", "!") and end < len(toks):
            end += 1
        operand = toks[begin:end]
        # A direct sizeof operand is a compile-time constant.  This rule
        # targets run-time lengths that can exceed the destination type.
        first = next((t.text for t in operand if t.text not in ("(", ")")), "")
        relevant = ctype == "USHORT" or (first != "sizeof" and any(
            t.text == "sizeof" or LENGTH_NAME.search(t.text)
            for t in operand if re.match(r"[A-Za-z_]", t.text)
        ))
        if not relevant:
            continue
        line = toks[i].line
        if exempt(lines, line) or (ctype == "USHORT" and ushort_guarded(toks, i, braces)):
            continue
        sites.append(Site("length-narrowing", rel, line,
                          normalise(source, toks[i].start, toks[end - 1].end)))

    # Repeated tokens in one expression can make the growth matcher report
    # the same site twice.  Preserve source order while collapsing them.
    return list(dict.fromkeys(sites))


def read_known() -> collections.Counter[tuple[str, str, str]]:
    known: collections.Counter[tuple[str, str, str]] = collections.Counter()
    for number, raw in enumerate(KNOWN.read_text(encoding="utf-8").splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 4:
            raise SystemExit(f"{KNOWN.relative_to(ROOT)}:{number}: expected four tab-separated fields")
        known[(fields[0], fields[1], fields[2])] += 1
    return known


def fixture_test() -> None:
    expected: collections.Counter[tuple[str, str, int]] = collections.Counter()
    actual: collections.Counter[tuple[str, str, int]] = collections.Counter()
    files = sorted(FIXTURES.glob("*.c"))
    if not files:
        raise SystemExit("lint-sizearith: fixture floor failed: no fixture .c files")
    for path in files:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = re.search(r"sizearith-expect:\s*([a-z-]+)", line)
            if match:
                expected[(path.name, match.group(1), number)] += 1
        for site in scan(path):
            actual[(path.name, site.rule, site.line)] += 1
    if actual != expected:
        print("lint-sizearith: fixture self-test failed", file=sys.stderr)
        print(f"  expected: {sorted(expected.elements())}", file=sys.stderr)
        print(f"  actual:   {sorted(actual.elements())}", file=sys.stderr)
        raise SystemExit(1)


def source_files(arguments: list[str]) -> list[pathlib.Path]:
    roots = arguments or ["src", "crt", "sh", "arch"]
    result: list[pathlib.Path] = []
    for name in roots:
        path = (ROOT / name).resolve()
        if path.is_file() and path.suffix in (".c", ".h"):
            result.append(path)
        elif path.is_dir():
            result.extend(p for p in sorted(path.rglob("*"))
                          if p.is_file() and p.suffix in (".c", ".h"))
    return sorted(set(result))


def main() -> int:
    fixture_test()
    emit = False
    arguments = sys.argv[1:]
    if arguments[:1] == ["--emit-baseline"]:
        emit = True
        arguments = arguments[1:]
    files = source_files(arguments)
    if not files:
        print("lint-sizearith: FAILED -- no .c files scanned", file=sys.stderr)
        return 1
    sites = [site for path in files for site in scan(path)]
    if emit:
        for site in sites:
            print("\t".join((*site.key, "existing site; remove while completing the checked-size audit")))
        return 0

    scanned = {path.relative_to(ROOT).as_posix() for path in files}
    remaining = collections.Counter(
        {key: count for key, count in read_known().items() if key[1] in scanned}
    )
    new: list[Site] = []
    matched = 0
    for site in sites:
        if remaining[site.key]:
            remaining[site.key] -= 1
            matched += 1
        else:
            new.append(site)
    stale = list(remaining.elements())
    for site in new:
        print(f"{site.path}:{site.line}: {site.rule}: {site.snippet}")
        print("  use checked size/growth conversion, or sizearith-safe: with a proof")
    for rule, path, snippet in stale:
        print(f"{KNOWN.relative_to(ROOT)}: stale {rule} entry: {path}: {snippet}", file=sys.stderr)
    if new or stale:
        print(f"lint-sizearith: {len(new)} new finding(s), {len(stale)} stale baseline entry/entries, "
              f"{matched} known site(s) in {len(files)} file(s)")
        if stale or os.environ.get("LINT_STRICT", "1") != "0":
            return 1
        return 0
    print(f"lint-sizearith: no new findings ({matched} known site(s), "
          f"{len(sites)} site(s) classified in {len(files)} file(s); fixtures passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
