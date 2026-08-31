# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared disposition and execution-profile model for every test suite."""

from __future__ import annotations

import dataclasses
import os
from pathlib import Path
import signal
import subprocess
import threading


DISPOSITIONS = ("PASS", "BUG", "UNIMPL", "NA", "FLAKY")
PROFILE_KEYS = {
    "runtime", "target_arch", "host_arch", "wow64", "kernel32",
}


@dataclasses.dataclass(frozen=True)
class CapturedProcess:
    returncode: int | None
    output: str
    timed_out: bool


def _terminate_tree(process: subprocess.Popen[bytes]) -> None:
    """Best-effort termination of the private tree started below."""
    if os.name == "nt":
        # Popen.kill() only terminates the test image. A child which inherited
        # stdout keeps the reader waiting for EOF, so ask Windows to terminate
        # the whole descendant tree while that relationship is still known.
        try:
            subprocess.run(
                ["taskkill", "/pid", str(process.pid), "/t", "/f"],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, timeout=10, check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            if process.poll() is None:
                process.kill()
    else:
        # start_new_session below makes the test the leader of a private
        # process group. Kill Wine and ordinary descendants together.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_captured(command: list[str], *, cwd: Path,
                 env: dict[str, str], timeout: int) -> CapturedProcess:
    """Run a test, bounding both its lifetime and inherited output pipes."""
    process = subprocess.Popen(
        command, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        start_new_session=os.name != "nt",
    )
    assert process.stdout is not None
    chunks: list[bytes] = []

    # communicate() waits for BOTH the process and pipe EOF. That conflates a
    # test parent which exited successfully with an orphan child which merely
    # inherited stdout: mq_timedsend/5-1 does exactly that, so its four-second
    # PASS became a 120-second TIMEOUT. Drain on a separate thread and wait for
    # the test process independently, then dispose of descendants as soon as
    # the root verdict is known.
    def drain() -> None:
        try:
            while True:
                chunk = process.stdout.read1(65536)
                if not chunk:
                    break
                chunks.append(chunk)
        except (OSError, ValueError):
            pass

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    timed_out = False
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        _terminate_tree(process)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

    # The root return code is the test verdict. Dispose of every descendant
    # immediately after it is known, even if the pipe happens to reach EOF on
    # its own: mq_timedsend/5-1's un-waited child can send one last SIGABRT at
    # a PID Wine has already reused for the next case. A one-second pipe grace
    # made that race reproducible as an intermittent rc=6 in the next run.
    _terminate_tree(process)
    reader.join(timeout=5)
    if reader.is_alive():
        # A Wine child may have been forked by the persistent wineserver and
        # therefore escaped the wrapper's Unix process group. Do not wait on
        # its inherited pipe once the root verdict is known.
        process.stdout.close()
        reader.join(timeout=1)
    output = b"".join(chunks).decode("utf-8", "replace")
    return CapturedProcess(None if timed_out else process.returncode,
                           output, timed_out)


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
        # No rule names this case specifically. Fall back to a suite-wide
        # wildcard rule -- case == "*", the same spelling parse_selector()
        # already gives "*" for "match any profile" -- rather than only the
        # `default` argument below. A manifest can then declare its own
        # baseline disposition as one real, visible row (e.g.
        # `*\t*\tPASS\tno more specific rule matched`) instead of that
        # baseline living only as a hidden code-level default. An explicit
        # per-case rule always wins regardless of its selector's
        # specificity, because it is only ever considered here, after a
        # case-specific search has already come up empty.
        candidates = [
            rule for rule in rules
            if rule.suite == suite and rule.case == "*" and rule.matches(profile)
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
