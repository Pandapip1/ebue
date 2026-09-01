#!/usr/bin/env python3
"""Add direct buffer/size writable-span contracts from a lint report.

This migration is intentionally narrow.  It handles only diagnostics where a
known writing operation receives a direct pointer parameter (optionally plus
an offset) and an unchanged, direct ``size_t`` parameter as its byte count.
It updates the definition, same-file redeclarations, and matching public
header declarations.  Compound lengths, pointer-to-pointer carriers, member
expressions, casts, and ambiguous declarations are reported and skipped.

Dry-run by default; pass --apply to rewrite files.
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "obj/lint/x86_64.memory-contract.report"
FINDING = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+): memory operation span is not proven "
    r"valid in (?P<function>[A-Za-z_]\w*): (?P<call>.*)$"
)
CALL = re.compile(r"^(?P<name>[A-Za-z_]\w*)\s*\((?P<args>.*)\)$")
WRITE_ARGS = {
    "memcpy": (0, 2),
    "memmove": (0, 2),
    "memset": (0, 2),
    "read": (1, 2),
    "pread": (1, 2),
    "recv": (1, 2),
    "getdents": (1, 2),
    "stpncpy": (0, 2),
}


@dataclasses.dataclass(frozen=True)
class Candidate:
    source: pathlib.Path
    function: str
    pointer: str
    length: str
    arity: int
    pointer_index: int
    length_index: int


@dataclasses.dataclass(frozen=True)
class Signature:
    open_paren: int
    close_paren: int
    terminator: str
    parameters: tuple[str, ...]


def split_top_level(text: str) -> list[str]:
    parts: list[str] = []
    start = 0
    depth = 0
    quote = ""
    escape = False
    for index, char in enumerate(text):
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = ""
            continue
        if char in "\"'":
            quote = char
        elif char in "([{" :
            depth += 1
        elif char in ")]}" :
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(text[start:index].strip())
            start = index + 1
    parts.append(text[start:].strip())
    return parts


def matching_paren(text: str, opening: int) -> int | None:
    depth = 0
    for index in range(opening, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
    return None


def signatures(text: str, function: str) -> list[Signature]:
    found: list[Signature] = []
    for match in re.finditer(r"\b" + re.escape(function) + r"\s*\(", text):
        opening = text.find("(", match.start())
        closing = matching_paren(text, opening)
        if closing is None:
            continue
        tail = text[closing + 1 :]
        terminal = re.search(r"[;{]", tail)
        if not terminal:
            continue
        terminator = terminal.group()
        params = split_top_level(text[opening + 1 : closing])
        if params == ["void"]:
            params = []
        found.append(Signature(opening, closing, terminator, tuple(params)))
    return found


def parameter_name(parameter: str, wanted: str) -> bool:
    return re.search(r"\b" + re.escape(wanted) + r"\b", parameter) is not None


def direct_pointer_parameter(parameter: str, name: str) -> bool:
    before = parameter.split(name, 1)[0]
    return (
        parameter_name(parameter, name)
        and "*" in before
        and before.count("*") == 1
        and "const" not in before.split("*")[0].split()
        and "(" not in before
    )


def root_pointer(expression: str) -> str | None:
    expression = expression.strip()
    return expression if re.fullmatch(r"[A-Za-z_]\w*", expression) else None


def locate_definition(text: str, function: str, line: int) -> Signature | None:
    offset = sum(len(part) for part in text.splitlines(keepends=True)[: line - 1])
    definitions = [sig for sig in signatures(text, function) if sig.terminator == "{"]
    before = [sig for sig in definitions if sig.open_paren < offset]
    return max(before, key=lambda sig: sig.open_paren) if before else None


def candidates(report: pathlib.Path) -> tuple[list[Candidate], list[str]]:
    result: list[Candidate] = []
    skipped: list[str] = []
    cache: dict[pathlib.Path, str] = {}
    for raw in report.read_text().splitlines():
        finding = FINDING.match(raw)
        if not finding:
            continue
        call = CALL.match(finding["call"])
        if not call or call["name"] not in WRITE_ARGS:
            continue
        args = split_top_level(call["args"])
        pointer_arg, length_arg = WRITE_ARGS[call["name"]]
        if max(pointer_arg, length_arg) >= len(args):
            continue
        pointer = root_pointer(args[pointer_arg])
        length = args[length_arg].strip()
        if not pointer or not re.fullmatch(r"[A-Za-z_]\w*", length):
            skipped.append(raw + " [compound carrier or length]")
            continue
        source = ROOT / finding["file"]
        text = cache.setdefault(source, source.read_text())
        definition = locate_definition(text, finding["function"], int(finding["line"]))
        if not definition:
            skipped.append(raw + " [definition not found]")
            continue
        pointer_indexes = [
            i for i, param in enumerate(definition.parameters)
            if parameter_name(param, pointer)
        ]
        length_indexes = [
            i for i, param in enumerate(definition.parameters)
            if parameter_name(param, length) and "size_t" in param
        ]
        if len(pointer_indexes) != 1 or len(length_indexes) != 1:
            skipped.append(raw + " [parameters not unambiguous]")
            continue
        pi, li = pointer_indexes[0], length_indexes[0]
        if not direct_pointer_parameter(definition.parameters[pi], pointer):
            skipped.append(raw + " [carrier is not a direct writable pointer]")
            continue
        result.append(Candidate(source, finding["function"], pointer, length,
                                len(definition.parameters), pi, li))
    return sorted(set(result), key=lambda item: (str(item.source), item.function)), skipped


def name_unnamed(parameter: str, name: str) -> str | None:
    if parameter_name(parameter, name):
        return parameter
    if "(" in parameter or "[" in parameter or parameter.strip() == "...":
        return None
    return parameter.rstrip() + " " + name


def annotate(parameter: str, pointer: str, length: str) -> str:
    if "withtok(writable_span(" in parameter:
        return parameter
    match = re.search(r"\b" + re.escape(pointer) + r"\b", parameter)
    assert match
    return (parameter[: match.end()] + f" withtok(writable_span({length}))" +
            parameter[match.end() :])


def rewrite_signatures(text: str, candidate: Candidate, allow_unnamed: bool) -> tuple[str, int]:
    edits: list[tuple[int, int, str]] = []
    for sig in signatures(text, candidate.function):
        if len(sig.parameters) != candidate.arity:
            continue
        params = list(sig.parameters)
        pointer_param = params[candidate.pointer_index]
        length_param = params[candidate.length_index]
        if allow_unnamed:
            pointer_param = name_unnamed(pointer_param, candidate.pointer)
            length_param = name_unnamed(length_param, candidate.length)
            if pointer_param is None or length_param is None:
                continue
        if not parameter_name(pointer_param, candidate.pointer) or not parameter_name(length_param, candidate.length):
            continue
        if "*" not in pointer_param or "size_t" not in length_param:
            continue
        params[candidate.pointer_index] = annotate(pointer_param, candidate.pointer,
                                                   candidate.length)
        params[candidate.length_index] = length_param
        replacement = ", ".join(params)
        line_start = text.rfind("\n", 0, sig.open_paren) + 1
        if sig.open_paren - line_start + len(replacement) > 96:
            replacement = "\n\t" + ",\n\t".join(params) + "\n"
        if replacement != text[sig.open_paren + 1 : sig.close_paren]:
            edits.append((sig.open_paren + 1, sig.close_paren, replacement))
    for begin, end, replacement in reversed(edits):
        text = text[:begin] + replacement + text[end:]
    return text, len(edits)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=pathlib.Path, default=DEFAULT_REPORT)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    found, skipped = candidates(args.report)
    changes: dict[pathlib.Path, str] = {}
    counts: dict[pathlib.Path, int] = {}
    headers = tuple((ROOT / "include").rglob("*.h"))
    for candidate in found:
        targets = [candidate.source]
        source_text = changes.get(candidate.source, candidate.source.read_text())
        is_static = bool(re.search(
            r"\bstatic\b[^;{}]*\b" + re.escape(candidate.function) + r"\s*\(",
            source_text[: locate_definition(source_text, candidate.function, 10**9).open_paren]
        ))
        if not is_static:
            targets.extend(path for path in headers
                           if re.search(r"\b" + re.escape(candidate.function) + r"\s*\(",
                                        changes.get(path, path.read_text())))
        for target in targets:
            old = changes.get(target, target.read_text())
            new, count = rewrite_signatures(old, candidate, target != candidate.source)
            if count:
                changes[target] = new
                counts[target] = counts.get(target, 0) + count
    for candidate in found:
        print(f"candidate: {candidate.source.relative_to(ROOT)}: {candidate.function} "
              f"{candidate.pointer} -> writable_span({candidate.length})")
    for path in sorted(changes):
        print(f"rewrite: {path.relative_to(ROOT)} ({counts[path]} signature(s))")
        if args.apply:
            path.write_text(changes[path])
    for reason in skipped:
        print("skip: " + reason, file=sys.stderr)
    if not args.apply:
        print("dry run; pass --apply to rewrite")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
