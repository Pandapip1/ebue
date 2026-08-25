#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inventory and independently probe ntlibc's source-level test fences.

The executable suites report what happened.  This tool validates why a
test is fenced out.  Normal mode only inventories fences.  Pedantic mode
probes every BUG and UNIMPL fence independently; strict mode performs the
same probes and additionally rejects every BUG and UNIMPL disposition.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys
import tempfile

from testlib import (DISPOSITIONS, Rule, load_manifest, parse_profile,
                     parse_selector, policy_accepts, resolve)


ROOT = Path(__file__).resolve().parent.parent
TEST_DIR = ROOT / "test"
PROFILE_MANIFEST = TEST_DIR / "test-profiles.tsv"
OPEN_RE = re.compile(r"^\s*#\s*if\s+0\b")
MARKER_RE = re.compile(
    r"^\s*#\s*if\s+NTLIBC_TEST\(\s*(PASS|BUG|UNIMPL|NA|FLAKY)\s*,\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\)"
)
IF_RE = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
ENDIF_RE = re.compile(r"^\s*#\s*endif\b")
TEST_NAME_RE = re.compile(r"\b(test_[A-Za-z0-9_]+)\s*\(")
SOURCE_DISPOSITIONS = DISPOSITIONS


@dataclasses.dataclass
class Fence:
    path: Path
    start: int
    end: int
    disposition: str
    lines: list[str]
    test_names: set[str]
    case_name: str | None = None

    @property
    def ident(self) -> str:
        return f"{self.path.relative_to(ROOT)}:{self.start + 1}"


def opening_disposition(line: str) -> str | None:
    # The opening line is the machine-readable declaration.  Reasons may
    # mention other dispositions later in the comment without changing it.
    tags = []
    if re.search(r"\bBUG\b", line):
        tags.append("BUG")
    if re.search(r"\bUNIMPL\b", line):
        tags.append("UNIMPL")
    if re.search(r"\bN/A\b|\bNA\b", line):
        tags.append("NA")
    return tags[0] if len(tags) == 1 else None


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def parse_file(path: Path) -> tuple[list[Fence], list[str]]:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    fences: list[Fence] = []
    errors: list[str] = []
    i = 0
    while i < len(lines):
        marker = MARKER_RE.match(lines[i])
        if not OPEN_RE.match(lines[i]) and not marker:
            i += 1
            continue
        disposition = marker.group(1) if marker else opening_disposition(lines[i])
        case_name = marker.group(2) if marker else None
        if disposition is None:
            errors.append(
                f"{path.relative_to(ROOT)}:{i + 1}: fence opening must name "
                "exactly one of PASS, BUG, UNIMPL, NA, or FLAKY"
            )
        depth = 1
        j = i + 1
        while j < len(lines) and depth:
            if IF_RE.match(lines[j]):
                depth += 1
            elif ENDIF_RE.match(lines[j]):
                depth -= 1
            j += 1
        if depth:
            errors.append(f"{path.relative_to(ROOT)}:{i + 1}: unterminated fence")
            break
        block = lines[i:j]
        code = strip_comments("".join(block[1:-1]))
        fences.append(
            Fence(path, i, j - 1, disposition or "INVALID", block,
                  set(TEST_NAME_RE.findall(code)), case_name)
        )
        i = j
    return fences, errors


def inventory() -> tuple[list[Fence], list[str]]:
    fences: list[Fence] = []
    errors: list[str] = []
    for path in sorted(TEST_DIR.glob("*.c")):
        found, bad = parse_file(path)
        fences.extend(found)
        errors.extend(bad)
    return fences, errors


def groups(fences: list[Fence]) -> list[list[Fence]]:
    """Join definition/call fences that refer to the same test_* function."""
    parent = list(range(len(fences)))

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(a: int, b: int) -> None:
        a, b = find(a), find(b)
        if a != b:
            parent[b] = a

    owners: dict[tuple[Path, str], int] = {}
    for i, fence in enumerate(fences):
        names = {fence.case_name} if fence.case_name else fence.test_names
        for name in names:
            key = (fence.path, name)
            if key in owners:
                union(i, owners[key])
            else:
                owners[key] = i

    result: dict[int, list[Fence]] = {}
    for i, fence in enumerate(fences):
        result.setdefault(find(i), []).append(fence)
    return sorted(result.values(), key=lambda group: group[0].ident)


def validate_groups(fences: list[Fence]) -> list[str]:
    errors: list[str] = []
    for group in groups(fences):
        kinds = {f.disposition for f in group}
        if len(kinds) != 1:
            errors.append(
                f"{group[0].ident}: linked fence group mixes dispositions: "
                + ", ".join(sorted(kinds))
            )
    return errors


def config() -> dict[str, str]:
    path = ROOT / "config.mak"
    if not path.is_file():
        raise RuntimeError("config.mak is missing; run ./configure first")
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line or line.lstrip().startswith("#"):
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def transformed_source(group: list[Fence], destination: Path) -> None:
    source = group[0].path
    selected = {(f.start, f.end) for f in group}
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    for start, _end in selected:
        if OPEN_RE.match(lines[start]):
            lines[start] = re.sub(r"^(\s*#\s*if\s+)0\b", r"\g<1>1", lines[start])
        else:
            lines[start] = re.sub(
                r"^(\s*#\s*if\s+)NTLIBC_TEST\([^)]*\)", r"\g<1>1", lines[start]
            )

    # A fenced static test function is absent from the normal main() call
    # list.  This generated translation unit represents that one case, so
    # invoke every selected zero-argument test function before main returns.
    # Definition/call fence pairs already contain their own invocation.
    selected_code = strip_comments("".join(
        line for fence in group for line in fence.lines[1:-1]
    ))
    definitions = re.findall(
        r"\bstatic\s+(?:void|int)\s+(test_[A-Za-z0-9_]+)\s*\(\s*void\s*\)",
        selected_code,
    )
    calls = [
        name for name in definitions
        if len(re.findall(rf"\b{re.escape(name)}\s*\(", selected_code)) == 1
    ]
    if calls:
        verdicts = [
            i for i, line in enumerate(lines)
            if re.match(r"^\s*if\s*\(\s*(?:fails|failed|failures)\b", line)
        ]
        if not verdicts:
            verdicts = [
                i for i, line in enumerate(lines)
                if re.match(r"^\s*return\s+(?:fails|failed|failures)\b", line)
            ]
        if not verdicts:
            verdicts = [i for i, line in enumerate(lines) if re.match(r"^\s*return\b", line)]
        if not verdicts:
            raise RuntimeError(
                f"{source.relative_to(ROOT)} has no final verdict for probe injection"
            )
        at = verdicts[-1]
        lines[at:at] = [
            f"\t{name}(); /* independently enabled policy probe */\n" for name in calls
        ]
    destination.write_text("".join(lines), encoding="utf-8")


def compile_probe(source: Path, output: Path, cfg: dict[str, str]) -> subprocess.CompletedProcess[str]:
    arch = cfg.get("ARCH", "")
    command = []
    command.extend(shlex.split(cfg.get("CC", "")))
    command.extend(shlex.split(cfg.get("CFLAGS_C99FSE", "")))
    command.extend(shlex.split(cfg.get("CFLAGS_AUTO", "")))
    command.extend([
        f"-I{ROOT / 'arch' / arch}",
        f"-I{ROOT / 'arch' / 'generic'}",
        f"-I{ROOT / 'obj' / 'include'}",
        f"-I{ROOT / 'include'}",
        "-nostdlib", "-o", str(output), str(ROOT / "lib" / "crt1.o"),
        str(source), f"-L{ROOT / 'lib'}", "-lc", "-lntdll",
    ])
    if not command or not command[0]:
        raise RuntimeError("config.mak does not define CC")
    return subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)



# subprocess.run(text=True) does NOT decode what comes back on a timeout.
# CPython raises TimeoutExpired with the raw buffered output, so .stdout is
# `bytes` even though every successful return from the same call is `str`
# (measured on 3.12.3, with and without errors="replace"). Joining those
# logs then dies with "TypeError: sequence item N: expected str instance,
# bytes found" -- so a run that was merely SLOW is reported as a Python
# traceback rather than as the TIMEOUT it is. That matters most exactly
# when it is least welcome: under a saturated machine, where timeouts are
# common and the traceback looks like a defect in the tree.
def _as_text(output) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace")
    return output

def run_probe(executable: Path, cfg: dict[str, str], work: Path) -> tuple[str, str]:
    wine = os.environ.get("WINE", cfg.get("WINE", ""))
    if not wine:
        return "NO-RUNNER", "config.mak has no WINE"
    command = shlex.split(wine) + [str(executable)]
    env = os.environ.copy()
    env["WINEDEBUG"] = "-all"
    env["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    try:
        result = subprocess.run(command, cwd=work, env=env, text=True,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120, check=False)
    except subprocess.TimeoutExpired as exc:
        return "TIMEOUT", _as_text(exc.stdout)
    if result.returncode == 0:
        return "PASS", result.stdout
    if result.returncode == 77:
        return "UNRESOLVED", result.stdout
    if result.returncode < 0:
        return "ABNORMAL", result.stdout
    return "FAIL", result.stdout


def probe(mode: str, fences: list[Fence]) -> int:
    cfg = config()
    failures = 0
    counts = {kind: 0 for kind in SOURCE_DISPOSITIONS}
    for group in groups(fences):
        disposition = group[0].disposition
        counts[disposition] = counts.get(disposition, 0) + 1

    with tempfile.TemporaryDirectory(prefix="ntlibc-test-policy.") as tmp_name:
        tmp = Path(tmp_name)
        (tmp / "test").mkdir()
        (tmp / "test" / "test-policy.h").write_bytes(
            (TEST_DIR / "test-policy.h").read_bytes()
        )
        os.symlink(ROOT / "src", tmp / "src")
        for number, group in enumerate(groups(fences), 1):
            disposition = group[0].disposition
            label = group[0].case_name or ",".join(f.ident for f in group)
            if disposition == "NA":
                print(f"NA       {label}")
                continue
            if mode == "normal" and disposition in {"BUG", "UNIMPL"}:
                print(f"{disposition:<8} {label} (not probed in normal mode)")
                continue
            source = tmp / "test" / group[0].path.name
            executable = tmp / f"probe-{number}.exe"
            transformed_source(group, source)
            built = compile_probe(source, executable, cfg)
            if disposition == "UNIMPL":
                if built.returncode != 0:
                    print(f"UNIMPL   {label} (compile failed as declared)")
                else:
                    print(f"STALE    {label} (UNIMPL compiled successfully)")
                    failures += 1
                continue
            if built.returncode != 0:
                print(f"INVALID  {label} ({disposition} did not compile)")
                for line in built.stdout.splitlines()[-12:]:
                    print(f"         {line}")
                failures += 1
                continue
            outcome, output = run_probe(executable, cfg, tmp)
            if disposition == "PASS" and outcome == "PASS":
                print(f"PASS     {label}")
            elif disposition == "FLAKY" and outcome in {"PASS", "FAIL"} and mode != "strict":
                print(f"FLAKY    {label} ({outcome})")
            elif disposition == "FLAKY" and outcome == "PASS":
                print(f"FLAKY    {label} (PASS required by strict)")
            elif disposition == "BUG" and outcome == "FAIL":
                print(f"BUG      {label} (compiled and failed as declared)")
            else:
                expected = "PASS" if disposition in {"PASS", "FLAKY"} else "FAIL"
                print(f"STALE    {label} ({disposition} produced {outcome}, expected {expected})")
                for line in output.splitlines()[-12:]:
                    print(f"         {line}")
                failures += 1

    if mode == "strict":
        for disposition in ("BUG", "UNIMPL"):
            if counts.get(disposition, 0):
                print(
                    f"STRICT   {counts[disposition]} {disposition} fence(s) "
                    "are disallowed"
                )
                failures += counts[disposition]
    print(
        "policy: " + ", ".join(f"{counts.get(k, 0)} {k}" for k in SOURCE_DISPOSITIONS)
        + f"; {failures} policy failure(s)"
    )
    return 1 if failures else 0


def selftest() -> int:
    cases = [
        ("normal", "PASS", True, "PASS", True),
        ("normal", "PASS", True, "FAIL", False),
        ("normal", "BUG", False, "NA", True),
        ("pedantic", "BUG", True, "FAIL", True),
        ("pedantic", "BUG", False, "UNBUILDABLE", False),
        ("pedantic", "BUG", True, "PASS", False),
        ("strict", "BUG", True, "FAIL", False),
        ("normal", "UNIMPL", False, "NA", True),
        ("pedantic", "UNIMPL", False, "UNBUILDABLE", True),
        ("pedantic", "UNIMPL", True, "FAIL", False),
        ("strict", "UNIMPL", False, "UNBUILDABLE", False),
        ("strict", "UNIMPL", True, "FAIL", False),
        ("pedantic", "NA", False, "NA", True),
        ("normal", "FLAKY", True, "FAIL", True),
        ("pedantic", "FLAKY", True, "FAIL", True),
        ("strict", "FLAKY", True, "FAIL", False),
        ("strict", "FLAKY", True, "PASS", True),
        ("normal", "FLAKY", True, "TIMEOUT", False),
    ]
    failed = 0
    for mode, disposition, built, outcome, expected in cases:
        got = policy_accepts(mode, disposition, built, outcome)
        if got != expected:
            print(f"selftest: FAIL {mode} {disposition} {built} {outcome}")
            failed += 1
    if failed:
        return 1
    rules, errors = load_manifest(PROFILE_MANIFEST)
    if errors:
        for error in errors:
            print(f"selftest: FAIL {error}")
        return 1
    synthetic = [
        dataclasses.replace(
            resolve([], "suite", "case", {}, default="BUG"),
            selector=(("runtime", "wine"),), disposition="NA", line=1,
        ),
        dataclasses.replace(
            resolve([], "suite", "case", {}, default="BUG"),
            selector=(("runtime", "wine"), ("target_arch", "i386")),
            disposition="FLAKY", line=2,
        ),
    ]
    selected = resolve(synthetic, "suite", "case", {
        "runtime": "wine", "target_arch": "i386"
    }, default="BUG")
    if selected.disposition != "FLAKY":
        print("selftest: FAIL profile resolver did not choose the most-specific rule")
        return 1
    print(f"selftest: all {len(cases)} policy transitions and profile resolution passed")
    return 0


def execution_profile(command: str, explicit: list[str]) -> dict[str, str]:
    profile: dict[str, str] = {}
    try:
        cfg = config()
    except RuntimeError:
        cfg = {}
    arch = cfg.get("ARCH")
    if arch:
        profile["target_arch"] = arch
    if cfg.get("KERNEL32") in {"yes", "no"}:
        profile["kernel32"] = cfg["KERNEL32"]
    host = platform.machine().lower()
    if host in {"amd64", "x86_64"}:
        host = "x86_64"
    elif host in {"x86", "i386", "i686"}:
        host = "i386"
    profile["host_arch"] = host
    if command in {"pedantic", "strict"}:
        profile["runtime"] = os.environ.get("NTLIBC_TEST_RUNTIME", "wine")
    supplied = parse_profile(explicit)
    profile.update(supplied)
    if "target_arch" in profile and "host_arch" in profile:
        profile.setdefault(
            "wow64",
            "yes" if profile["target_arch"] == "i386" and
            profile["host_arch"] == "x86_64" else "no",
        )
    return profile


def resolve_defaults(path: Path, suite: str, profile: dict[str, str]) -> int:
    rules, errors = load_manifest(PROFILE_MANIFEST)
    if errors:
        for error in errors:
            print(f"test-policy: {error}", file=sys.stderr)
        return 1
    cases: set[str] = set()
    local_keys: set[tuple[str, tuple[tuple[str, str], ...]]] = set()
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        fields = line.split(None, 3)
        if len(fields) < 3:
            print(f"test-policy: {path}:{line_number}: malformed default row",
                  file=sys.stderr)
            return 1
        case, selector_text, disposition = fields[:3]
        reason = fields[3] if len(fields) == 4 else "-"
        if disposition not in DISPOSITIONS:
            print(f"test-policy: {path}:{line_number}: unknown disposition {disposition}",
                  file=sys.stderr)
            return 1
        if disposition != "PASS" and reason == "-":
            print(f"test-policy: {path}:{line_number}: {disposition} requires a reason",
                  file=sys.stderr)
            return 1
        try:
            selector = parse_selector(selector_text)
        except ValueError as error:
            print(f"test-policy: {path}:{line_number}: {error}", file=sys.stderr)
            return 1
        key = (case, selector)
        if key in local_keys:
            print(f"test-policy: {path}:{line_number}: duplicate rule for {case}",
                  file=sys.stderr)
            return 1
        local_keys.add(key)
        cases.add(case)
        rules.append(Rule(suite, case, selector, disposition, reason, line_number))
    for case in sorted(cases):
        try:
            selected = resolve(rules, suite, case, profile)
        except ValueError as error:
            print(f"test-policy: {error}", file=sys.stderr)
            return 1
        print(f"{case}\t{selected.disposition}\t{selected.reason}")
    unknown = sorted({rule.case for rule in rules if rule.suite == suite} - cases)
    if unknown:
        print(f"test-policy: profile override(s) name unknown {suite} cases: "
              + ", ".join(unknown), file=sys.stderr)
        return 1
    return 0


# Capability terms are DECLARATIONS, not measurements: testlib.Rule.matches()
# only compares a selector against what --profile supplied, and nothing in
# this tree probes for a symlink or a console.  So a rule guarded by
# `capability.console=no` silently stops applying the moment nobody passes
# that term -- and the case it was exempting gets probed and reported STALE,
# which reads as a defect in the tree rather than as a missing argument.
# That happened: the terms lived only in ci.yml, so every local run probed
# two cases their rules meant to exempt.  Naming the gap is cheap; finding
# it from a STALE line is not.
def unset_capabilities(rules: list[Rule], profile: dict[str, str]) -> list[str]:
    wanted = {key for rule in rules for key, _ in rule.selector
              if key.startswith("capability.")}
    return sorted(key for key in wanted if key not in profile)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=(
        "check", "list", "pedantic", "strict", "selftest", "resolve",
        "validate",
    ))
    parser.add_argument("--profile", action="append", default=[], metavar="KEY=VALUE")
    parser.add_argument("--suite")
    parser.add_argument("--defaults", type=Path)
    args = parser.parse_args()
    if args.command == "selftest":
        return selftest()
    if args.command == "resolve":
        if not args.suite or not args.defaults:
            parser.error("resolve requires --suite and --defaults")
        try:
            profile = execution_profile(args.command, args.profile)
        except ValueError as error:
            parser.error(str(error))
        return resolve_defaults(args.defaults, args.suite, profile)
    fences, errors = inventory()
    rules, manifest_errors = load_manifest(PROFILE_MANIFEST)
    errors.extend(manifest_errors)
    errors.extend(validate_groups(fences))
    try:
        profile = execution_profile(args.command, args.profile)
    except ValueError as error:
        errors.append(str(error))
        profile = {}
    known = {fence.case_name for fence in fences if fence.case_name}
    for rule in rules:
        if rule.suite == "ntlibc" and rule.case not in known:
            errors.append(
                f"{PROFILE_MANIFEST.relative_to(ROOT)}:{rule.line}: "
                f"override names unknown ntlibc case {rule.case}"
            )
    missing = unset_capabilities(rules, profile)
    if missing:
        print("test-policy: capability term(s) used by test/test-profiles.tsv "
              "but not set in this profile: " + ", ".join(missing),
              file=sys.stderr)
        print("test-policy: rules guarded by them cannot match, so the cases "
              "they exempt are probed against their base disposition.",
              file=sys.stderr)
        print("test-policy: the Makefile's TEST_PROFILE default supplies these; "
              "pass them explicitly if you overrode it.", file=sys.stderr)
    resolved: list[Fence] = []
    for fence in fences:
        if not fence.case_name:
            resolved.append(fence)
            continue
        try:
            rule = resolve(rules, "ntlibc", fence.case_name, profile,
                           default=fence.disposition, default_reason=fence.ident)
            resolved.append(dataclasses.replace(fence, disposition=rule.disposition))
        except ValueError as error:
            errors.append(str(error))
    fences = resolved
    if errors:
        for error in errors:
            print(f"test-policy: {error}", file=sys.stderr)
        return 1
    if args.command == "list":
        for fence in fences:
            names = ",".join(sorted(fence.test_names)) or "inline"
            print(
                f"{fence.disposition}\t{fence.case_name or '-'}\t"
                f"{fence.ident}\t{names}"
            )
    grouped = groups(fences)
    counts = {
        kind: sum(group[0].disposition == kind for group in grouped)
        for kind in SOURCE_DISPOSITIONS
    }
    profile_text = ",".join(f"{key}={value}" for key, value in sorted(profile.items()))
    print("test-policy: " + ", ".join(f"{counts[k]} {k}" for k in SOURCE_DISPOSITIONS)
          + (f" [{profile_text}]" if profile_text else ""))
    if args.command == "check" and any(
        fence.disposition in {"PASS", "FLAKY"} for fence in fences
    ):
        return probe("normal", fences)
    if args.command in {"pedantic", "strict"}:
        return probe(args.command, fences)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
