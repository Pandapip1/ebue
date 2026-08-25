#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run ntlibc PE tests with the same harness under Wine and Windows."""

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

from testlib import parse_profile


SERIAL_PREFIXES = ("fork", "waitpid", "exec", "spawn", "posix-signal")

# Per-test timeout overrides, in seconds, keyed on the executable's stem.
# --timeout is the ceiling for everything else; a name here may raise its
# own, never lower it.
#
# sh-engine: measured at 2.2 s under Wine on a loaded workstation, and it
# exceeds 120 s on real Windows -- it timed out on 2 of 3 windows-test legs
# even on a fast run and 3 of 3 on a slow one, having passed on i386 the
# run before. A 50x gap between runtimes is not a slow test, and this entry
# is NOT an assertion that it is one: at 120 s a hang and a slow test are
# indistinguishable, because both produce the same TIMEOUT. Ten minutes is
# chosen to tell them apart. If sh-engine now passes, the test was slow on
# native NT and the number was wrong; if it still times out, the extra
# eight minutes prove a genuine stall in the shell engine's process
# spawn/wait/pipe handling, which is a defect in ntlibc and not in this
# table. Either outcome is information the 120 s ceiling was destroying.
#
# So this entry is expected to be temporary in one direction or the other.
# Do not add names here to make a red test green -- that converts a defect
# into a slow suite, which is the one move that buys nothing.
SLOW_TESTS = {"sh-engine": 600}


def timeout_for(path: Path, default: int) -> int:
    return max(default, SLOW_TESTS.get(path.stem, 0))


@dataclasses.dataclass(frozen=True)
class Result:
    executable: Path
    outcome: str
    returncode: int | None
    output: str


def is_serial(path: Path) -> bool:
    return path.name.startswith(SERIAL_PREFIXES)


def run_one(path: Path, runner: list[str], root: Path, timeout: int) -> Result:
    environment = os.environ.copy()
    if runner:
        environment["WINEDEBUG"] = "-all"
        environment["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    with tempfile.TemporaryDirectory(prefix="work.", dir=root) as work:
        try:
            completed = subprocess.run(
                [*runner, str(path.resolve())],
                cwd=work,
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            return Result(path, "TIMEOUT", None, output)
        except OSError as error:
            return Result(path, "ERROR", None, str(error))

    if completed.returncode == 0:
        outcome = "PASS"
    elif completed.returncode == 77:
        # The executable deliberately declined a test because the current
        # environment cannot exercise it.  NA is a disposition, not a pass.
        outcome = "NA"
    else:
        outcome = "FAIL"
    return Result(path, outcome, completed.returncode, completed.stdout)


def discover_artifact(root: Path) -> list[Path]:
    test_dir = root / "test"
    shell = root / "sh" / "sh.exe"
    missing = [path for path in (test_dir, shell) if not path.exists()]
    if missing:
        for path in missing:
            print(f"run-tests: expected artifact path is missing: {path}", file=sys.stderr)
        if root.exists():
            print("run-tests: downloaded artifact contains:", file=sys.stderr)
            for path in sorted(root.rglob("*")):
                print(f"  {path.relative_to(root)}", file=sys.stderr)
        raise RuntimeError("incomplete test artifact")
    return sorted(test_dir.glob("*.exe"), key=lambda path: path.name)


def report(result: Result, timeout: int) -> None:
    if result.outcome == "TIMEOUT":
        suffix = f" (exceeded {timeout}s)"
    elif result.returncode is None:
        suffix = ""
    else:
        suffix = f" (rc={result.returncode})"
    print(f"{result.outcome:<7} {result.executable.name}{suffix}")
    if result.outcome not in {"PASS"} and result.output:
        for line in result.output.splitlines()[-40:]:
            print(f"        {line}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", type=Path,
                        help="discover test/*.exe in a downloaded obj artifact")
    parser.add_argument("executables", nargs="*", type=Path,
                        help="test executables to run")
    parser.add_argument("--runner", default="",
                        help="command prefix, normally Wine; empty runs directly")
    parser.add_argument("--jobs", type=int,
                        default=int(os.environ.get("RUNTESTS_JOBS", os.cpu_count() or 1)))
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--profile", action="append", default=[], metavar="KEY=VALUE")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.jobs < 1 or args.timeout < 1:
        print("run-tests: --jobs and --timeout must be positive", file=sys.stderr)
        return 2
    if args.artifact_root is not None and args.executables:
        print("run-tests: use --artifact-root or explicit executables, not both",
              file=sys.stderr)
        return 2
    if args.artifact_root is not None:
        try:
            executables = discover_artifact(args.artifact_root)
        except RuntimeError:
            return 2
    else:
        executables = args.executables
    if not executables:
        print("run-tests: no test executables were supplied or discovered", file=sys.stderr)
        return 2
    missing = [path for path in executables if not path.is_file()]
    if missing:
        for path in missing:
            print(f"run-tests: test executable is missing: {path}", file=sys.stderr)
        return 2

    try:
        runner = shlex.split(args.runner, posix=os.name != "nt")
    except ValueError as error:
        print(f"run-tests: invalid --runner: {error}", file=sys.stderr)
        return 2
    if os.name != "nt" and not runner:
        print("run-tests: --runner is required off Windows", file=sys.stderr)
        return 2
    try:
        profile_values = parse_profile(args.profile)
    except ValueError as error:
        print(f"run-tests: {error}", file=sys.stderr)
        return 2
    host = platform.machine().lower()
    host = "x86_64" if host in {"amd64", "x86_64"} else "i386" if host in {
        "x86", "i386", "i686"
    } else host
    profile_values.setdefault("host_arch", host)
    if "target_arch" in profile_values:
        profile_values.setdefault(
            "wow64", "yes" if profile_values["target_arch"] == "i386" and
            host == "x86_64" else "no"
        )

    results: dict[Path, Result] = {}
    serial = [path for path in executables if is_serial(path)]
    parallel = [path for path in executables if not is_serial(path)]
    with tempfile.TemporaryDirectory(prefix="ntlibc-run-tests.") as tmp:
        root = Path(tmp)
        # The process-sensitive tests are one sequential task, overlapping
        # the independent pool without ever overlapping each other.
        with ThreadPoolExecutor(max_workers=args.jobs + bool(serial)) as pool:
            futures = {
                pool.submit(run_one, path, runner, root,
                            timeout_for(path, args.timeout)): path
                for path in parallel
            }

            def run_serial() -> list[Result]:
                return [run_one(path, runner, root,
                                timeout_for(path, args.timeout)) for path in serial]

            serial_future = pool.submit(run_serial) if serial else None
            for future in as_completed(futures):
                result = future.result()
                results[result.executable] = result
            if serial_future:
                for result in serial_future.result():
                    results[result.executable] = result

    counts: dict[str, int] = {}
    for path in executables:
        result = results[path]
        report(result, timeout_for(path, args.timeout))
        counts[result.outcome] = counts.get(result.outcome, 0) + 1
    order = ("PASS", "FAIL", "NA", "TIMEOUT", "ERROR")
    profile_text = ",".join(f"{key}={value}" for key, value in sorted(profile_values.items()))
    print("run-tests: " + ", ".join(
        f"{counts[outcome]} {outcome}" for outcome in order if counts.get(outcome)
    ) + (f" [{profile_text}]" if profile_text else ""))
    return 1 if any(outcome in counts for outcome in ("FAIL", "TIMEOUT", "ERROR")) else 0


if __name__ == "__main__":
    raise SystemExit(main())
