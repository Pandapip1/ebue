#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and adjudicate the Open POSIX Test Suite with ntlibc policy."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
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


@dataclasses.dataclass
class CaseResult:
    case: str
    disposition: str
    built: bool
    observation: str
    detail: str = ""


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
    parser.add_argument("--jobs", type=int,
                        default=int(os.environ.get("OPTSRUN_JOBS", "4")))
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
                results[case] = CaseResult(case, disposition, False, "NA", reason)
            else:
                selected.append((case, disposition))
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {
                pool.submit(build_one, cfg, case, disposition, work): case
                for case, disposition in selected
            }
            done = 0
            for future in as_completed(futures):
                result = future.result()
                results[result.case] = result
                done += 1
                # A build-phase verdict is final for two populations, and
                # only those two: a case that must run but did not compile,
                # and an UNIMPL case that compiled when it must not. Every
                # other built case is merely not decided yet -- its verdict
                # needs the run -- so reporting it here would be wrong.
                if not result.built and result.disposition != "UNIMPL":
                    progress(f"posix-opts: FAIL {result.case} "
                             f"({result.disposition} did not build)")
                elif result.built and result.disposition == "UNIMPL":
                    progress(f"posix-opts: FAIL {result.case} "
                             f"(UNIMPL built, and must not)")
                if done % 100 == 0 or done == len(selected):
                    progress(f"posix-opts: built {done}/{len(selected)}")
        runnable = [
            result for result in results.values()
            if result.built and result.disposition != "UNIMPL"
        ]
        # LTP cases share enough process/timing state that execution remains
        # serial; compilation is the expensive parallel-safe phase.
        progress(f"posix-opts: running {len(runnable)} case(s), "
                 f"{args.attempts} attempt(s) each, serially")
        for done, result in enumerate(
            sorted(runnable, key=lambda item: item.case), start=1
        ):
            result = run_one(result, runner, work, args.attempts, args.timeout)
            results[result.case] = result
            if not accepted(args.mode, result):
                progress(f"posix-opts: FAIL {result.case} "
                         f"({result.disposition} observed {result.observation})")
            elif done % 25 == 0 or done == len(runnable):
                progress(f"posix-opts: ran {done}/{len(runnable)}")

    ordered = [results[case] for case in discovered]
    failures = [result for result in ordered if not accepted(args.mode, result)]
    for result in ordered:
        print(f"{result.disposition:<7} {result.case} [{result.observation}]")
        if result in failures and result.detail:
            for line in result.detail.splitlines()[-12:]:
                print(f"        {line}")
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
