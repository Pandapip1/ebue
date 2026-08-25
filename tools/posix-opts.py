#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and adjudicate the Open POSIX Test Suite with ntlibc policy."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading
import dataclasses
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile
import time

from testlib import policy_accepts


ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / "third_party" / "ltp" / "testcases" / "open_posix_testsuite"
IFACES = SUITE / "conformance" / "interfaces"
EXPECTED = ROOT / "test" / "posix-opts-expected.txt"
CENSUS = 1610

# Interfaces whose assertions are about elapsed time rather than about a
# return value.  These are the only cases that cannot absorb machine load:
# a nanosleep that is asked to sleep 10 ms and measures 12 does not fail
# because the library is wrong, it fails because something else was on the
# CPU.  All three FLAKY entries in test/posix-opts-expected.txt live here
# ("baseline disagreed across repeated runs"), and one of them still came
# back FLAKY on a fully serial run, so the boundary is real and close.
#
# Everything outside this set asserts on return values and errno and is
# indifferent to what else is running, which is what makes the split
# worth having: it is ~40 cases of 591.
CLOCK_BOUND = (
    "nanosleep", "clock_nanosleep", "clock_gettime", "clock_getres",
    "clock_settime", "clock", "time", "timer_create", "timer_settime",
    "timer_gettime", "sleep", "alarm",
)


def is_clock_bound(case: str) -> bool:
    return case.split("/", 1)[0] in CLOCK_BOUND


@dataclasses.dataclass
class CaseResult:
    case: str
    disposition: str
    built: bool
    observation: str
    detail: str = ""


_emit_lock = threading.Lock()


def emit(result: CaseResult, mode: str) -> bool:
    """Report one case the moment it is decided. Returns True if accepted.

    Held under a lock because the parallel run phase emits from worker
    threads: without it a failing case's output could interleave with
    another case's, and a spliced traceback is worse than no traceback.
    The lock is the whole ordering discipline -- completion order is not
    stable and is not made stable, because what matters is that each
    case's block is atomic, not that blocks arrive in a fixed sequence.

    A passing case gets one line. Its output is not news; the run has 591
    of them and printing them all is how a log stops being read.
    """
    ok = accepted(mode, result)
    with _emit_lock:
        mark = "     " if ok else "FAIL "
        print(f"{mark}{result.disposition:<7} {result.case} [{result.observation}]")
        if not ok and result.detail:
            for line in result.detail.splitlines()[-12:]:
                print(f"        {line}")
        sys.stdout.flush()
    return ok


def progress(message: str) -> None:
    """Live progress, on stderr, deliberately not on stdout.

    stdout carries the final report in `discovered` order, so two runs of
    the same tree are diffable and a redirect still yields a stable
    artefact. Completion order is not stable -- the build phase is a
    thread pool -- so streaming it onto stdout would destroy that.

    This exists because a batched harness that prints only at the end has
    nothing to say when it is killed. CI's posix-optsrun job is cancelled
    at its 30-minute wall on every push while the real work takes ~40
    minutes, and it has therefore never once reported which cases it got
    through or what was already failing. A run that dies half way should
    still leave evidence of the half it did.
    """
    print(message, file=sys.stderr, flush=True)


def check_pin() -> None:
    """Refuse to measure a suite that is not the one this repository pins.

    Two independent sources say which LTP revision is on disk:

      the submodule's own HEAD   what is actually being compiled
      the gitlink at HEAD        what this repository says should be there

    When both are readable and they DISAGREE, that is a hard error rather
    than a preference for one: the checkout has been moved off the pin, so
    every disposition in test/posix-opts-expected.txt is being adjudicated
    against a suite nobody else has.  Case names would still line up often
    enough for the census to pass, which is exactly what makes this the one
    failure here that produces plausible, wrong, permanent numbers.

    tools/gate.sh's stage copies are rsync'd with --exclude=.git, which
    strips third_party/ltp's .git FILE along with the top-level directory,
    so inside a stage copy the submodule's own HEAD is simply unreadable.
    OPTSRUN_GITDIR points such a copy back at the real repository; where
    neither source is readable there is nothing to compare and nothing to
    contradict, which is a missing check rather than a failed one.
    """
    gitdir = os.environ.get("OPTSRUN_GITDIR", str(ROOT))
    def rev(where: str, spec: str) -> str:
        result = subprocess.run(
            ["git", "-C", where, "rev-parse", spec], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False,
        )
        return result.stdout.strip() if result.returncode == 0 else ""
    head = rev(str(SUITE), "HEAD")
    link = rev(gitdir, "HEAD:third_party/ltp")
    if head and link and head != link:
        raise RuntimeError(
            f"LTP pin mismatch: third_party/ltp is checked out at {head}, "
            f"but this repository pins {link}. Every disposition would be "
            f"adjudicated against a suite this repository does not describe. "
            f"Either restore the pin (git submodule update --init) or commit "
            f"the move."
        )


def read_config() -> dict[str, str]:
    path = ROOT / "config.mak"
    if not path.is_file():
        raise RuntimeError("config.mak is missing; run ./configure first")
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def profile(cfg: dict[str, str], runtime: str, supplied: list[str]) -> list[str]:
    host = platform.machine().lower()
    host = "x86_64" if host in {"amd64", "x86_64"} else "i386" if host in {
        "x86", "i386", "i686"
    } else host
    values = {
        "runtime": runtime,
        "target_arch": cfg.get("ARCH", "unknown"),
        "host_arch": host,
        "kernel32": cfg.get("KERNEL32", "no"),
    }
    values["wow64"] = (
        "yes" if values["target_arch"] == "i386" and host == "x86_64" else "no"
    )
    for term in supplied:
        if "=" not in term:
            raise RuntimeError(f"profile term must be KEY=VALUE: {term}")
        key, value = term.split("=", 1)
        values[key] = value
    return [f"{key}={value}" for key, value in sorted(values.items())]


def resolved_policy(profile_terms: list[str]) -> dict[str, tuple[str, str]]:
    command = [
        sys.executable, str(ROOT / "tools" / "test-policy.py"), "resolve",
        "--suite", "posix-opts", "--defaults", str(EXPECTED),
    ]
    for term in profile_terms:
        command.extend(("--profile", term))
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(result.stdout.strip())
    policy: dict[str, tuple[str, str]] = {}
    for line in result.stdout.splitlines():
        case, disposition, reason = line.split("\t", 2)
        policy[case] = (disposition, reason)
    return policy


def compile_command(cfg: dict[str, str], source: Path, output: Path) -> list[str]:
    arch = cfg["ARCH"]
    command = shlex.split(cfg["CC"])
    command += shlex.split(cfg.get("CFLAGS_C99FSE", ""))
    command += shlex.split(cfg.get("CFLAGS_AUTO", ""))
    command += [
        "-D_GNU_SOURCE", f"-I{ROOT / 'arch' / arch}",
        f"-I{ROOT / 'arch' / 'generic'}", f"-I{ROOT / 'obj' / 'include'}",
        f"-I{ROOT / 'include'}", f"-I{SUITE / 'include'}", f"-I{SUITE}",
        "-nostdlib", "-o", str(output), str(ROOT / "lib" / "crt1.o"),
        str(source), str(SUITE / "lib" / "common.c"),
        f"-L{ROOT / 'lib'}", "-lc", "-lntdll",
    ]
    return command


def build_one(cfg: dict[str, str], case: str, disposition: str,
              work: Path) -> CaseResult:
    output = work / "exe" / (case.replace("/", "_") + ".exe")
    result = subprocess.run(
        compile_command(cfg, IFACES / case, output), cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )
    if result.returncode:
        return CaseResult(case, disposition, False, "UNBUILDABLE", result.stdout)
    return CaseResult(case, disposition, True, "BUILT", str(output))


def attempt(executable: Path, runner: list[str], work: Path, timeout: int,
            number: int) -> tuple[str, str]:
    directory = work / "run" / f"{executable.stem}.{number}"
    directory.mkdir()
    environment = os.environ.copy()
    if runner:
        environment["WINEDEBUG"] = "-all"
        environment["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    try:
        result = subprocess.run(
            [*runner, str(executable)], cwd=directory, env=environment,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, errors="replace",
            timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired as error:
        return "TIMEOUT", error.stdout or ""
    except OSError as error:
        return "ERROR", str(error)
    outcomes = {0: "PASS", 1: "FAIL", 2: "UNRESOLVED", 4: "UNSUPPORTED", 5: "UNTESTED"}
    return outcomes.get(result.returncode, "ABNORMAL"), result.stdout


def run_one(result: CaseResult, runner: list[str], work: Path,
            attempts: int, timeout: int) -> CaseResult:
    executable = Path(result.detail)
    seen: list[str] = []
    logs: list[str] = []
    for number in range(attempts):
        outcome, output = attempt(executable, runner, work, timeout, number)
        seen.append(outcome)
        logs.append(output)
    passes = seen.count("PASS")
    if passes == attempts:
        observation = "PASS"
    elif passes:
        observation = "FLAKY"
    else:
        observation = seen[-1] if len(set(seen)) == 1 else "FLAKY"
    return dataclasses.replace(result, observation=observation,
                               detail="\n".join(logs))


def accepted(mode: str, result: CaseResult) -> bool:
    observation = result.observation
    if result.disposition == "FLAKY" and observation == "FLAKY":
        observation = "FAIL"
    return policy_accepts(mode, result.disposition, result.built, observation)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="normal",
                        choices=("normal", "pedantic", "strict", "selftest"))
    parser.add_argument("--runtime", default="wine")
    parser.add_argument("--profile", action="append", default=[])
    parser.add_argument("--runner", default=os.environ.get("WINE", ""))
    # nproc, matching tools/run-tests.py's RUNTESTS_JOBS rather than the
    # hardcoded 4 this used to carry. Safe to derive from the machine here
    # for the reason it is NOT safe inside tools/gate.sh: the gate runs many
    # stages at once, so its cost is a product, and it therefore pins
    # OPTSRUN_JOBS to GATE_MAKE_JOBS itself before invoking this. Standalone
    # -- a developer, or a CI job that owns its runner -- there is no product
    # and no reason to leave five sixths of the machine idle.
    parser.add_argument("--jobs", type=int,
                        default=int(os.environ.get("OPTSRUN_JOBS",
                                                   os.cpu_count() or 1)))
    parser.add_argument("--attempts", type=int,
                        default=int(os.environ.get("OPTSRUN_ATTEMPTS", "3")))
    parser.add_argument("--timeout", type=int, default=120)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.mode == "selftest":
        checks = [
            policy_accepts("pedantic", "BUG", True, "FAIL"),
            not policy_accepts("pedantic", "BUG", True, "ABNORMAL"),
            not policy_accepts("pedantic", "BUG", True, "TIMEOUT"),
            not policy_accepts("strict", "BUG", True, "FAIL"),
            policy_accepts("pedantic", "UNIMPL", False, "UNBUILDABLE"),
            not policy_accepts("strict", "UNIMPL", False, "UNBUILDABLE"),
        ]
        print("posix-opts selftest: PASS" if all(checks) else "posix-opts selftest: FAIL")
        return 0 if all(checks) else 1
    if min(args.jobs, args.attempts, args.timeout) < 1:
        print("posix-opts: jobs, attempts and timeout must be positive", file=sys.stderr)
        return 2
    if not IFACES.is_dir():
        print("posix-opts: LTP submodule is empty; run git submodule update --init",
              file=sys.stderr)
        return 2
    check_pin()
    cfg = read_config()
    if not (ROOT / "lib" / "libc.a").is_file():
        raise RuntimeError("lib/libc.a is missing; run make first")
    profile_terms = profile(cfg, args.runtime, args.profile)
    policy = resolved_policy(profile_terms)
    discovered = sorted(
        str(path.relative_to(IFACES)) for path in IFACES.rglob("*.c")
    )
    if len(discovered) != CENSUS or set(discovered) != set(policy):
        missing = sorted(set(discovered) - set(policy))
        stale = sorted(set(policy) - set(discovered))
        raise RuntimeError(
            f"census/policy mismatch: {len(discovered)} sources; "
            f"unannotated={missing[:8]}, stale={stale[:8]}"
        )
    runner = shlex.split(args.runner, posix=os.name != "nt")
    if args.runtime != "windows" and not runner:
        raise RuntimeError("no runner configured")

    started = time.monotonic()
    results: dict[str, CaseResult] = {}
    with tempfile.TemporaryDirectory(prefix="ntlibc-posix-opts.") as name:
        work = Path(name)
        (work / "exe").mkdir()
        (work / "run").mkdir()
        selected: list[tuple[str, str]] = []
        for case in discovered:
            disposition, reason = policy[case]
            if disposition == "NA" or (
                args.mode == "normal" and disposition in {"BUG", "UNIMPL"}
            ):
                skipped = CaseResult(case, disposition, False, "NA", reason)
                results[case] = skipped
                emit(skipped, args.mode)
            else:
                selected.append((case, disposition))
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {
                pool.submit(build_one, cfg, case, disposition, work): case
                for case, disposition in selected
            }
            for future in as_completed(futures):
                result = future.result()
                results[result.case] = result
                # A case that built and is not UNIMPL has not been decided
                # yet -- its verdict needs the run, and claiming one here
                # either way would be a guess. Everything else is final at
                # build time and is reported now: an UNIMPL case (which
                # never runs, so this is its verdict) and any case that
                # failed to compile.
                if not (result.built and result.disposition != "UNIMPL"):
                    emit(result, args.mode)
        runnable = sorted(
            (result for result in results.values()
             if result.built and result.disposition != "UNIMPL"),
            key=lambda item: item.case,
        )
        bulk = [r for r in runnable if not is_clock_bound(r.case)]
        clock = [r for r in runnable if is_clock_bound(r.case)]

        def run_and_emit(result: CaseResult) -> CaseResult:
            out = run_one(result, runner, work, args.attempts, args.timeout)
            results[out.case] = out
            emit(out, args.mode)
            return out

        # Phase one: everything that asserts on return values, in parallel.
        # This is the bulk -- and it used to be the whole run, serially,
        # which is why a full sweep took ~39 minutes and CI's 30-minute
        # wall killed it on every push without it ever reporting anything.
        progress(f"posix-opts: running {len(bulk)} case(s) at {args.jobs} "
                 f"job(s), {args.attempts} attempt(s) each")
        if bulk:
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                for future in as_completed(
                    [pool.submit(run_and_emit, r) for r in bulk]
                ):
                    future.result()

        # Phase two: the clock-bound cases, serially, and deliberately
        # AFTER phase one rather than beside it. Running them in their own
        # serial thread next to a parallel pool -- the shape
        # tools/run-tests.py uses for process-sensitive tests -- would not
        # protect them, because what moves a sleep across its tolerance is
        # load on the machine, not concurrency among themselves. They are
        # ~40 of 591, so buying them a quiet machine costs a couple of
        # minutes and keeps the one property that makes them meaningful.
        if clock:
            progress(f"posix-opts: running {len(clock)} clock-bound case(s) "
                     f"serially, alone")
            for result in clock:
                run_and_emit(result)

    ordered = [results[case] for case in discovered]
    failures = [result for result in ordered if not accepted(args.mode, result)]
    # No end-of-run dump: every case was reported by emit() at the moment it
    # was decided. Repeating all 1610 here would only be useful to a reader
    # who was not given anything until the process exited, which is the
    # thing this stopped doing.
    if failures:
        print(f"posix-opts: {len(failures)} case(s) failed policy:")
        for result in failures:
            print(f"    {result.disposition:<7} {result.case} "
                  f"[{result.observation}]")
    counts: dict[str, int] = {}
    for result in ordered:
        counts[result.disposition] = counts.get(result.disposition, 0) + 1
    elapsed = int(time.monotonic() - started)
    tally = "  ".join(
        f"{disposition}={counts.get(disposition, 0)}"
        for disposition in ("PASS", "BUG", "UNIMPL", "NA", "FLAKY")
    )
    print(f"posix-opts: mode={args.mode} profile={','.join(profile_terms)} {elapsed}s")
    print(f"posix-opts: {tally}")
    print(f"posix-opts: {len(ordered)} cases, {len(failures)} policy failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"posix-opts: {error}", file=sys.stderr)
        raise SystemExit(2)
