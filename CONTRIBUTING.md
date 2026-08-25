<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to ntlibc

## Platform rules

ntlibc talks to Windows NT through ntdll's `Nt*` and `Rtl*` interfaces.
Use ntdll whenever it can provide the behavior. Calls into kernel32 or any
other higher-level DLL require both:

- an `#ifdef NTLIBC_USE_KERNEL32` guard; and
- a usable ntdll-only fallback.

The default configuration is `--disable-kernel32`. The guarded build exists
for the few facilities with no ntdll equivalent, such as console control
handlers, and is covered by the x86_64 kernel32 CI leg.

The shell under `src/sh/` is part of libc because `system()`, `popen()` and
`wordexp()` require shell-language behavior that `cmd.exe` cannot provide.
`sh/main.c` is a thin executable wrapper over the same in-process engine.

## Source conventions

- Add an SPDX copyright and licence header to every new file. CI runs
  [REUSE](https://reuse.software/).
- Comments should explain platform constraints, observed behavior or a
  non-obvious tradeoff, rather than narrating the code.
- Keep public headers independently usable. `make hygiene` checks every
  header for both supported architectures.
- Generated `*.h.gen` and `boot/kaem/*.kaem` files are committed. Run
  `make generated` after changing their inputs.

## Build and test

The normal x86_64 loop is:

```sh
./configure --host=x86_64-win32 CC=x86_64-win32-tcc
make -j4
make -j4 check
```

Test dispositions have one vocabulary everywhere:

| disposition | normal | pedantic | strict |
|---|---|---|---|
| `PASS` | must compile and pass | same | same |
| `BUG` | skipped | must compile and fail | disallowed (after the same probe) |
| `UNIMPL` | skipped | must fail compilation or linking | disallowed (after the same probe) |
| `NA` | skipped for its recorded reason | same | same |
| `FLAKY` | run; pass or failure is recorded | same | must pass |

Pedantic mode is a ratchet: it fails when a disposition has gone stale, not
merely because a known bug exists. Strict mode includes every pedantic check
and additionally requires that no `BUG` or `UNIMPL` disposition remain and
that every `FLAKY` case pass. Source-level cases use
`NTLIBC_TEST(DISPOSITION, stable_case_name)` and are generated as independent
translation units when probed, so one case cannot hide another compile result.

```sh
make check-pedantic
make check-strict
python3 tools/test-policy.py list
```

The same mode suffixes apply to imported suites, for example
`make libc-test-pedantic` and `make libc-test-strict`. Profile-specific
overrides live in `test/test-profiles.tsv`; they are keyed by suite and stable
case name, so libc-test and Open POSIX use the same resolver as in-tree cases.
Selectors may constrain `runtime`, `target_arch`, `host_arch`, `wow64`,
`kernel32`, or a named `capability.*`. The most-specific matching rule wins;
equal-specificity matches are rejected as ambiguous. Imported baselines claim
only the profile in which they were measured, so a new platform fails as
unannotated instead of inheriting Wine/x86_64 results by accident.
Pass runner capabilities as whitespace-separated selectors when needed. The
same `TEST_PROFILE` reaches the in-tree, libc-test, and Open POSIX policy
resolvers; for example `make check-pedantic TEST_PROFILE="capability.symlink=no
capability.console=no"` describes the stock headless Wine CI environment.

Use `--host=i386-win32 CC=i386-win32-tcc` for i386. If guarded kernel32
code changed, run `make check-kernel32`; it cleans and rebuilds before and
after the kernel32-enabled check so configuration-dependent objects cannot
be reused accidentally.

Useful focused checks are:

```sh
make lint          # warnings, analyzers and source-level policy checks
make hygiene       # public-header isolation
make linkcheck     # declarations, definitions and PE import/header checks
make asan          # native AddressSanitizer and UBSan suite
make fuzz          # native libFuzzer harnesses, 60 seconds each
make libc-test     # musl libc-test corpus under Wine
make posix-gapmap  # classify all Open POSIX Test Suite cases
make posix-optsrun # Open POSIX suite through the shared policy
```

The two external-suite reports are reproducible, ignored build artefacts.
CI uploads them for inspection; they are not committed and do not require
merge drivers or pre-commit regeneration.

## The pre-push gate

`tools/gate.sh` runs the complete local verification set with independent
stages in isolated copies of the working tree:

```sh
tools/gate.sh
tools/gate.sh check-x86_64 lint-plain
tools/gate.sh --list
```

Each stage writes a separate log and result file. A requested stage that
does not report is a failure, so a killed worker cannot disappear into a
green summary.

Concurrency is bounded by two settings:

| variable | default | meaning |
|---|---:|---|
| `GATE_STAGE_CONCURRENCY` | 4 | stages allowed to run at once (`0` is unlimited) |
| `GATE_MAKE_JOBS` | 3 | workers allowed inside each stage |

The gate passes the second value to the subordinate runners as well as to
`make`; this keeps the actual bound close to their product. Override either
setting for a dedicated build machine. `GATE_WINE` selects an alternate
Wine binary for the two project-suite stages.

## Sanitizers and fuzzing

`make asan` and `make fuzz` build the OS-independent library code natively
with clang. They complement rather than replace the PE/Wine and real-Windows
tests: native stubs cannot validate ntdll, WOW64 or console behavior.

The fuzz corpus defaults to `obj/fuzzcorpus/<harness>/`. Common commands:

```sh
make fuzz FUZZ_TIME=300
FUZZ_JOBS=4 tools/fuzz.sh 60
tools/fuzz.sh 60 strtod printf
tools/fuzz.sh --repro /absolute/path/to/crash-input strtod
```

`FUZZ_JOBS` shards harnesses across workers. The default is one for a
developer invocation; CI sets it to the runner's CPU count. A finding exits
non-zero and leaves its input under the harness's `crashes/` directory.
Promote durable reproducers into the matching `test/*.c` file.

## CI structure

The workflows deliberately keep different schedules and meanings separate:

- `ci.yml` runs deterministic push/PR gates: both architectures under Wine,
  the x86_64 kernel32 configuration, the same binaries on real Windows,
  libc-test, both OPTS measurements, lint, sanitizers and REUSE.
- `fuzz.yml` runs a short corpus-backed net on pushes to main and a longer
  corpus-writing scheduled search.
- `posix-gapmap-nightly.yml` measures the pinned LTP suite and reports how
  far its pin has drifted from upstream.

`.github/actions/setup-tinycc/action.yml` is the single implementation of
the pinned toolchain cache/restore/build setup shared by CI workflows. The
toolchain revision remains explicit in each workflow using it so Renovate
can update and review the pin.

Wine and real Windows both execute the PE files through `tools/run-tests.py`.
It checks the artifact layout, refuses a zero-test run, isolates working
directories, serializes process-sensitive cases, and reports exit 77 as `NA`
rather than treating an environment-limited test as a pass.

## Generated build files

`tools/gen-alltypes.sh` expands the small `*.h.in` type descriptions into
the committed `*.h.gen` files. `tools/gen-kaem.sh` derives the two kaem
bootstrap recipes from the Makefile's source lists. Use:

```sh
make generated
git diff -- 'arch/*/bits/*.h.gen' 'include/*.h.gen' boot/kaem/
```

`./configure` installs `.githooks/pre-commit`, which checks both generated
families plus staged conflict markers. It also registers the
`ntlibc-kaem` merge driver for the two kaem files. If configure has not run
in a checkout, install these manually:

```sh
git config core.hooksPath .githooks
git config merge.ntlibc-kaem.driver 'tools/merge-kaem.sh %O %A %B %P'
```

The merge driver handles independent source-list insertions and archive
member additions. It leaves unfamiliar conflicts for a human; after any
manual resolution, run `make kaem` and stage both generated files.

## Dependency updates

Renovate tracks GitHub Actions and the pinned tinycc commit. Nothing is
auto-merged. When tinycc moves, validate both architectures, generated-file
drift and the real-Windows jobs before merging. Keep every workflow's
`TINYCC_SHA` identical so they share one cache entry and one compiler claim.
