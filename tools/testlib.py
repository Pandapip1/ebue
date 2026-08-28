# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared disposition and execution-profile model for every test suite."""

from __future__ import annotations

import dataclasses
import os
from pathlib import Path
import signal
import subprocess


DISPOSITIONS = ("PASS", "BUG", "UNIMPL", "NA", "FLAKY")
PROFILE_KEYS = {
    "runtime", "target_arch", "host_arch", "wow64", "kernel32",
}


@dataclasses.dataclass(frozen=True)
class CapturedProcess:
    returncode: int | None
    output: str
    timed_out: bool


def _text(value: str | bytes | None) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return value or ""


def run_captured(command: list[str], *, cwd: Path,
                 env: dict[str, str], timeout: int) -> CapturedProcess:
    """Run a test with a timeout that also disposes of its process tree."""
    process = subprocess.Popen(
        command, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace", start_new_session=os.name != "nt",
    )
    try:
        output, _ = process.communicate(timeout=timeout)
        return CapturedProcess(process.returncode, output, False)
    except subprocess.TimeoutExpired as error:
        captured = _text(error.output)

    if os.name == "nt":
        # Popen.kill() only terminates the test image.  A child which inherited
        # stdout keeps communicate() waiting for EOF forever, which used to
        # turn one test timeout into the Windows job's 25-minute cancellation.
        try:
            subprocess.run(
                ["taskkill", "/pid", str(process.pid), "/t", "/f"],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, timeout=10, check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            process.kill()
    else:
        # start_new_session above makes the test the leader of a private
        # process group.  Kill Wine and ordinary descendants together.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    try:
        output, _ = process.communicate(timeout=5)
        captured = output
    except subprocess.TimeoutExpired:
        # A Wine child may have been forked by the persistent wineserver and
        # therefore escaped the wrapper's Unix process group.  Do not wait on
        # its inherited pipe: the per-case verdict is still TIMEOUT.
        if process.stdout is not None:
            process.stdout.close()
        if process.poll() is None:
            process.kill()
        process.wait(timeout=5)
    return CapturedProcess(None, captured, True)


@dataclasses.dataclass(frozen=True)
class Rule:
    suite: str
    case: str
    selector: tuple[tuple[str, str], ...]
    disposition: str
    reason: str
    line: int

    def matches(self, profile: dict[str, str]) -> bool:
        return all(profile.get(key) == value for key, value in self.selector)


def parse_profile(values: list[str]) -> dict[str, str]:
    profile: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"profile term must be KEY=VALUE: {value}")
        key, item = value.split("=", 1)
        if not key or not item:
            raise ValueError(f"profile term must be KEY=VALUE: {value}")
        if key not in PROFILE_KEYS and not key.startswith("capability."):
            raise ValueError(f"unknown profile dimension: {key}")
        if key in profile and profile[key] != item:
            raise ValueError(f"profile gives {key} more than one value")
        profile[key] = item
    return profile


def parse_selector(value: str) -> tuple[tuple[str, str], ...]:
    if value == "*":
        return ()
    profile = parse_profile(value.split(","))
    return tuple(sorted(profile.items()))


def load_manifest(path: Path) -> tuple[list[Rule], list[str]]:
    rules: list[Rule] = []
    errors: list[str] = []
    seen: set[tuple[str, str, tuple[tuple[str, str], ...]]] = set()
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t", 4)
        if len(fields) != 5:
            errors.append(f"{path}:{line_number}: expected five tab-separated fields")
            continue
        suite, case, selector_text, disposition, reason = fields
        try:
            selector = parse_selector(selector_text)
        except ValueError as error:
            errors.append(f"{path}:{line_number}: {error}")
            continue
        if disposition not in DISPOSITIONS:
            errors.append(f"{path}:{line_number}: unknown disposition {disposition}")
            continue
        if disposition != "PASS" and (not reason or reason == "-"):
            errors.append(f"{path}:{line_number}: {disposition} requires a reason")
            continue
        key = (suite, case, selector)
        if key in seen:
            errors.append(f"{path}:{line_number}: duplicate rule for {suite}/{case}/{selector_text}")
            continue
        seen.add(key)
        rules.append(Rule(suite, case, selector, disposition, reason, line_number))
    return rules, errors


def resolve(rules: list[Rule], suite: str, case: str,
            profile: dict[str, str], default: str | None = None,
            default_reason: str = "declared by suite") -> Rule:
    candidates = [
        rule for rule in rules
        if rule.suite == suite and rule.case == case and rule.matches(profile)
    ]
    if not candidates:
        if default is None:
            raise ValueError(f"no disposition for {suite}/{case}")
        if default not in DISPOSITIONS:
            raise ValueError(f"unknown default disposition {default}")
        return Rule(suite, case, (), default, default_reason, 0)
    specificity = max(len(rule.selector) for rule in candidates)
    best = [rule for rule in candidates if len(rule.selector) == specificity]
    if len(best) != 1:
        locations = ", ".join(str(rule.line) for rule in best)
        raise ValueError(
            f"ambiguous disposition for {suite}/{case} at manifest lines {locations}"
        )
    return best[0]


def policy_accepts(mode: str, disposition: str, built: bool, outcome: str) -> bool:
    if disposition == "PASS":
        return built and outcome == "PASS"
    if disposition == "BUG":
        if mode == "normal":
            return not built and outcome == "NA"
        if mode == "strict":
            return False
        return built and outcome == "FAIL"
    if disposition == "UNIMPL":
        if mode == "normal":
            return not built and outcome == "NA"
        if mode == "strict":
            return False
        return not built and outcome == "UNBUILDABLE"
    if disposition == "NA":
        return not built and outcome == "NA"
    if disposition == "FLAKY":
        if not built or outcome not in {"PASS", "FAIL"}:
            return False
        return mode != "strict" or outcome == "PASS"
    return False
