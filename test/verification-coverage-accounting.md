<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# What this project does not verify

[POSIX-COVERAGE.md](POSIX-COVERAGE.md) and
[POSIX-GAP-ACCOUNTING.md](POSIX-GAP-ACCOUNTING.md) account for
*interface* coverage: which of 1177 POSIX interfaces exist, and which
have been read clause by clause. Nothing accounts for *verification*
coverage: which code is exercised by which mechanism, in which
environment, and what is therefore checked nowhere.

That is the expensive blind spot. Nine gate stages were found reporting
success having checked nothing. The real-Windows CI leg was, until
`d36b07c`, capable of running zero tests and exiting 0. In every case
the gap was invisible *because something reported success*.

This document is the inventory. It complements
[verification-measures.md](verification-measures.md) (measures against
ten human-found defects) and
[verification-measures-2.md](verification-measures-2.md) (measures
against the tagged fence corpus); those two ask "what would catch
this?", this one asks "what is not being looked at?".

Everything was measured on a fresh clone of `origin/main` at `d307704`,
x86_64, clang 18.1.3. **Every table below names the command that
regenerates it.** A hand-maintained list of 106 fences is wrong within
a week; a documented regeneration command is not.

## Contents

- [0. How the fences are counted, and why three agents got three answers](#0-how-the-fences-are-counted-and-why-three-agents-got-three-answers)
- [1. Fuzzing coverage: 8 harnesses, 31 modules](#1-fuzzing-coverage-8-harnesses-31-modules)
- [2. Verified only on Server 2025](#2-verified-only-on-server-2025)
- [3. The `rc=77` skip matrix](#3-the-rc77-skip-matrix)
- [4. All 32 `BUG:` fences](#4-all-32-bug-fences)
- [5. All 44 `UNIMPL:` fences](#5-all-44-unimpl-fences)
- [6. All 30 `N/A:` fences: permanent versus conditional](#6-all-30-na-fences-permanent-versus-conditional)
- [7. How each gap became invisible](#7-how-each-gap-became-invisible)
- [8. Ranking](#8-ranking)

## 0. How the fences are counted, and why three agents got three answers

Three counts of the `BUG:` fences have been circulated -- 24, 27 and 30
-- and all three are wrong. The cause is that the fence syntax is not
uniform, and every naive grep misses a different subset.

The canonical command, used for every count in this document:

```sh
git grep -nE '#if 0[[:space:]]*/\*[[:space:]]*(BUG|UNIMPL|N/A)' -- '*.c' '*.h'
```

| Tag | Count | |
|-----|-------|---|
| `BUG:` | **32** | a reproduced, unfixed spec violation |
| `UNIMPL:` | **44** | the function or flag does not exist yet |
| `N/A:` | **30** | a mechanism makes the requirement unobservable |
| | **106** | all in `test/`; none in `src/`, `include/` or `tools/` |

Three ways to get a different number, all of them real:

1. **Tabs.** Six fences use a tab after `#if 0`
   (`posix-signal.c:1087,1147,1200`; `posix-unistd.c:1041,1242,1455`).
   `git grep '#if 0 /\* BUG:'` misses every one.
2. **Parenthetical tags.** Three fences qualify the tag rather than
   ending it with a colon: `BUG (knowing deviation, ...)`
   (`posix-dl.c:1068`), `BUG (latent)` (`posix-glob.c:1160`),
   `BUG (self-documented in src/regex/regex.c's banner)`
   (`posix-glob.c:2093`). A `BUG:`-anchored pattern misses all three.
   These are still fences and still describe real defects.
3. **`*.md`.** Dropping the `-- '*.c' '*.h'` pathspec adds three
   matches from `POSIX-COVERAGE.md`, which quotes the fence syntax as
   prose (1 `BUG`, 2 `N/A`). That is how "32 N/A" arises: the true
   code count is 30.

`verification-measures-2.md`'s table (30 / 43 / 31, at `06f3203`) is
short for reasons 2 and 3 and has drifted by two `BUG` and one `UNIMPL`
since. Its **classification** of the fences is unaffected and is not
re-derived here.

Everything else in this document is derived from that one command, so
the counts move together when the tree moves.

## 1. Fuzzing coverage: 8 harnesses, 31 modules

```sh
find src -mindepth 1 -maxdepth 1 -type d | wc -l        # 31, not 28
ls fuzz/fuzz_*.c                                        # 8 harnesses
grep -n '^HARNESSES' fuzz/Makefile
```

There are **31** directories under `src/`, not 28. One of them
(`src/setjmp`) contains no `.c` at all -- it is `arch/`-dispatched
assembly -- so 30 are C modules. Eight harnesses exist and, between
them, name **six** entry points in **four** modules.

| Module | .c files | Harness | Entry point fuzzed |
|--------|---------:|---------|--------------------|
| `src/stdio` | 8 | `fuzz_printf`, `fuzz_scanf` | `snprintf`, `sscanf` |
| `src/stdlib` | 21 | `fuzz_strtod`, `fuzz_strtol`, `fuzz_utf` | `strtod/f/ld/ll`, `strtol/ul/ll/ull/imax/umax`, `mbrtowc` |
| `src/time` | 19 | `fuzz_strftime`, `fuzz_strptime` | `strftime`, `strptime` |
| `src/internal` | 9 | `fuzz_path`, `fuzz_utf` | `__ntpath`, `__ntpath_at`, `__utf8_to_utf16`, `__utf16_to_utf8`, `__utf16_to_utf8_buf` |
| `src/ctype` | 34 | -- | none |
| `src/dirent` | 8 | -- | none |
| `src/dlfcn` | 1 | -- | none |
| `src/env` | 3 | -- | none |
| `src/exit` | 3 | -- | none |
| `src/fcntl` | 3 | -- | none |
| `src/file` | 1 | -- | none |
| `src/fnmatch` | 1 | -- | none |
| `src/ftw` | 1 | -- | none |
| `src/glob` | 1 | -- | none |
| `src/ioctl` | 1 | -- | none |
| `src/malloc` | 1 | -- | none |
| `src/math` | 33 | -- | none |
| `src/misc` | 12 | -- | none |
| `src/process` | 6 | -- | none |
| `src/regex` | 1 | -- | none |
| `src/search` | 4 | -- | none |
| `src/select` | 2 | -- | none |
| `src/setjmp` | 0 | -- | n/a (asm only) |
| `src/sh` | 4 | -- | none |
| `src/signal` | 1 | -- | none |
| `src/socket` | 10 | -- | none |
| `src/stat` | 5 | -- | none |
| `src/string` | 65 | -- | none directly |
| `src/termios` | 1 | -- | none |
| `src/unistd` | 24 | -- | none |
| `src/wordexp` | 2 | -- | none |

`src/string` is reached incidentally by every harness (`memcpy`,
`memcmp`, `strlen`) but no harness drives a string function with
adversarial input, so it counts as unfuzzed for this purpose.

**26 of 30 C modules have no harness.**

### The sharp finding, verified independently

`verification-measures-2.md` claims 15 of the 30 `BUG:` fences sit in
`src/regex`, `src/wordexp`, `src/glob`, `src/fnmatch` -- four modules
with no harness. **The claim holds; the denominator is 32, not 30.**

```sh
git grep -nE '#if 0[[:space:]]*/\*[[:space:]]*BUG' -- '*.c' | wc -l   # 32
```

Attributing each fence to the module it indicts:

| Module | `BUG:` fences | Fuzzed? |
|--------|--------------:|---------|
| `src/regex` | 6 | no |
| `src/glob` | 4 | no |
| `src/wordexp` | 4 | no |
| `src/fnmatch` | 1 | no |
| `src/math` | 3 | no |
| `src/signal` | 3 | no |
| `src/unistd` | 3 | no |
| `src/misc` (`pwd.c`, `grp.c`) | 2 | no |
| `src/stdio` | 2 | **yes** |
| `src/internal` (`rpath.c`) | 2 | partly (`path.c`, `utf.c` only) |
| `src/dlfcn` | 1 | no |
| `src/search` | 1 | no |

**15 of 32 (47%) of the confirmed defects are in four unfuzzed
pattern-matching modules that hold 5 of 30 modules' worth of code.**
Two more (`src/search`'s `hcreate` integer overflow, `src/regex`'s
unbounded recursion) are classic fuzzer finds sitting in unfuzzed code.

Ranked by evidence, not by size, the harnesses worth writing are:

1. **`fuzz_regex`** -- 6 fences, and `posix-glob.c:1925` is an
   unbounded-recursion process kill, which is the single defect class
   libFuzzer finds most reliably. `regcomp` + `regexec` over one
   pattern/subject split of the input; `src/regex/regex.c` is 732 lines
   and self-contained.
2. **`fuzz_fnmatch`** -- 1 fence, but the fence
   (`posix-glob.c:999`, unterminated `[`) is a *scanner walking off the
   end of the pattern*, and `src/fnmatch/fnmatch.c` is 153 lines with a
   pure `(pattern, string, flags)` signature. Cheapest harness in the
   tree by a wide margin.
3. **`fuzz_wordexp`** -- 4 fences, but see the caveat below.
4. **`fuzz_hsearch`** -- 1 fence, an unchecked `nel + nel/2 + 8`
   overflow. A five-line harness reproduces it deterministically; it
   arguably belongs in `test/` as a regression case rather than in
   `fuzz/`.
5. **`fuzz_glob`** -- 4 fences, but every one of the four is a
   *semantic* disagreement with a clause, which no fuzzer can see
   without an oracle glob. Lowest value of the five.

**What a regex/fnmatch harness would not catch.** All six `src/regex`
fences and all four `src/glob` ones are `verification-measures-2.md`'s
class F4: "a plausible reading of a clause that is not the clause".
A fuzzer finds crashes, hangs and sanitizer reports. Of the 15 fences
in the four modules, a harness plausibly finds **2** on its own
(`posix-glob.c:1925` recursion, `posix-glob.c:999` overrun -- if it
overruns into unmapped memory rather than just misparsing). The other
13 need a differential oracle against a reference implementation, which
is `verification-measures-2.md`'s measure, not this one's. Say so when
proposing the work: a regex harness is worth writing for the crash
class, not as a way of closing 6 fences.

### Two harness-infrastructure gaps, measured

**No corpus survives a run.** `.github/workflows/fuzz.yml:13-20` states
this as a deliberate decision -- libFuzzer reads a corpus through the C
library it is linked against, which is ntlibc, whose `open`/`readdir`
reach NT calls a native build does not have. So the nightly job starts
cold every time and re-derives the same shallow inputs forever. A
separate agent is working this; it is recorded here as a gap, not
solved here. Its cost is real: `fuzz_printf` reached 31 features and
`fuzz_scanf` 25 in a 30-second cold run, against `fuzz_path`'s 264 --
the format-string harnesses are exactly the ones that need a seeded
corpus to get past the first `%`.

**Per-module fuzz coverage is now measurable: `make -C fuzz
coverage`.** It was not when this section was first written, and the
account given here of *why* was wrong in its second half. Both halves
are restated below as measured, because the wrong one blamed a piece of
`src/` that turns out to be innocent.

- `-print_coverage=1` is **not cumulative**, and this stands.
  libFuzzer clears the 8-bit counters between inputs to collect
  per-input features, so what survives to the end describes the *last*
  input. Re-measured on `e382f97`: `fuzz_printf -max_total_time=15
  -print_coverage=1` emits 1104 `*COVERED_FUNC` lines of which exactly
  **6** are `COVERED_FUNC` and **none** is in `src/stdio` -- for a
  harness that had just executed `snprintf` about 100000 times. It also
  calls `RtlAllocateHeap` uncovered, which is plainly false. Not
  fixable from outside libFuzzer, and now moot: the profile route below
  answers the same question correctly.
- `-fprofile-instr-generate -fcoverage-mapping` **did not write a
  `.profraw`, but not for the reason given here.** The original claim
  was that `src/exit/exit.c`'s `exit()` ends the process with
  `NtTerminateProcess` and so bypasses the host `atexit` chain that
  flushes the profile. Measured under gdb, every step of that is
  wrong:

  - libFuzzer ends a timed run with `exit(0)`, not `_Exit(0)`, and
    that call binds to **ntlibc's** `exit()` (`FuzzerDriver` ->
    `exit` at `src/exit/exit.c:49` -> `__nt_exit`).
  - The profile runtime registers its flush with plain `atexit`
    (`nm libclang_rt.profile-x86_64.a` shows `U atexit` and no
    `__cxa_atexit`), which likewise binds to ntlibc's. Registration
    succeeds: at `__funcs_on_exit()` the handler count is 2, well
    under `ATEXIT_MAX`, and `writeFileWithoutReturn` is entered.
  - So the flush **does run**. `__llvm_profile_write_file` reaches
    `writeFile("default.profraw")`, which calls `fopen`.

  The two real causes are both about *where the bytes go*, and both
  live in `fuzz/ntstubs.c`, not in `src/`:

  1. The runtime picks its output path in an `.init_array`
     constructor from `getenv("LLVM_PROFILE_FILE")`. That `getenv` is
     ntlibc's, reading ntlibc's `environ`, which `ntstubs.c`'s own
     constructor deliberately replaces with an empty one so a native
     test does not inherit the harness's real environment. So
     `LLVM_PROFILE_FILE` is invisible however it is set and the
     runtime falls back to `default.profraw`.
  2. That `fopen` is also ntlibc's, so it goes to `NtCreateFile` in
     `ntstubs.c` -- into the **simulated in-memory volume**, which is
     ordinary process memory and vanishes when the process exits. The
     profile was written every time, into a file system that does not
     outlive the run.

  **ntlibc has no defect here.** `exit()`, `atexit()` and
  `__funcs_on_exit()` behave correctly and are left untouched;
  `src/exit/exit.c` shows 36% region coverage in every harness's
  report, i.e. it is still code under test.

The fix is entirely in `fuzz/`: `-fprofile-instr-generate=<path>` bakes
the output path into the binary (so no `getenv`), and the path is put
inside `NTLIBC_FUZZ_MIRROR`, the seam `ntstubs.c` already provides for
making one host directory visible in the simulated volume. See the
block comment above `coverage:` in `fuzz/Makefile`.

```sh
make -C fuzz coverage                        # every harness, per file
make -C fuzz coverage HARNESSES=printf FUNCS=1 MODULE=src/stdio
```

Measured on `e382f97`, 10s per harness, cold (no corpus):

| Harness | Its own module | A sibling's | An unfuzzed module |
|---------|---------------:|------------:|-------------------:|
| `fuzz_printf` | `src/stdio/printf.c` **87.9%** | `src/stdio/scanf.c` 0.0% | `src/regex/regex.c` 0.0% |
| `fuzz_scanf` | `src/stdio/scanf.c` **65.1%** | `src/stdio/printf.c` 25.0% | `src/regex/regex.c` 0.0% |
| `fuzz_strftime` | `src/time/strftime.c` **91.9%** | `src/time/strptime.c` 0.0% | `src/regex/regex.c` 0.0% |
| `fuzz_utf` | `src/internal/utf.c` **86.7%** | -- | `src/regex/regex.c` 0.0% |

The number discriminates, which is the point: a harness that never
enters the module it names reads 0.0%, indistinguishable from not
existing. **Anyone adding a harness should run this and check for a
non-zero figure against the file they meant to fuzz** -- a harness that
compiles, runs and reports no crashes without entering its module is a
vacuous pass, and this table is the only thing that tells the two
apart.

The module table above is still derived from the harnesses' *named
entry points* rather than from these numbers; regenerating it from
`make -C fuzz coverage` is left to whoever next touches it.

## 2. Verified only on Server 2025

```sh
grep -n 'TEST_RUN' Makefile          # Makefile:282
ls test/*-win.c
```

`Makefile:282` is:

```make
TEST_RUN = $(filter-out %-win.exe,$(TEST_EXES))
```

Four tests are built and never run by `make check`, because stock apt
Wine has no `RtlCloneUserProcess` and a program that forks under it
**hangs** rather than failing -- a hang, not a failure, is why they are
filtered out rather than allowed to fail loudly.

| Test | Lines | Sole verifier of |
|------|------:|------------------|
| `test/fork-win.c` | 51 | `fork()` returning twice at all; the child seeing the parent's stack, static and heap values; `getppid()` in the child equalling the parent's `getpid()`; `waitpid()` of a forked (not spawned) child |
| `test/fork-handles-win.c` | 136 | what `src/process/fork.c` does to the `__children` table across a clone -- whether a pre-existing child's non-inheritable process handle comes back as empty, as a recycled unrelated object, or as a *wrong status silently reported as success*; also that the fork child's own pipe fds and its own post-fork children survive |
| `test/fork-cloexec-exec-win.c` | 105 | the reported `fork()`+`O_CLOEXEC` handle-reuse defect: a cloexec handle's number is freed in the clone while its `__fds` table entry is duplicated, so `__spawn` can be handed that number back and `__fd_close_all_cloexec()` closes the grandchild's process handle out from under `__children` |
| `test/process-win.c` | 402 | the exec family end to end (`execv`/`execve`/`execvp` after a real fork), `wait()`, `waitpid()` over 320 children (past `CHILD_MAX_`, so a regression to a fixed-size child table shows up), `getrusage()` after a fork, and `src/process/spawn.c`'s argument-quoting rules driven from a forked child |

### The measured version of "only on Server 2025"

Comments are not evidence, so this was measured: strip comments and
`#if 0` blocks from every `test/*.c` and list which files call each
process primitive in live code.

```sh
# strip /*...*/, // and #if 0..#endif from every test/*.c, then list
# which files still contain a live call to each primitive:
python3 - <<'EOF'
import re, glob, collections
res = collections.defaultdict(list)
pat = r'\b(fork|execl|execle|execlp|execv|execve|execvp|wait|waitpid|getrusage)\s*\('
for f in sorted(glob.glob('test/*.c')):
    s = re.sub(r'/\*.*?\*/', '', open(f).read(), flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    out, depth = [], 0
    for line in s.split('\n'):
        t = line.strip()
        if depth:
            if re.match(r'#\s*if', t):    depth += 1
            elif re.match(r'#\s*endif', t): depth -= 1
            continue
        if re.match(r'#\s*if 0\b', t): depth = 1; continue
        out.append(line)
    for m in set(re.findall(pat, '\n'.join(out))):
        res[m].append(f)
for k in sorted(res): print(f"{k:10s} {' '.join(res[k])}")
EOF
```

| Primitive | Live callers |
|-----------|--------------|
| `fork` | `fork-win.c`, `fork-handles-win.c`, `fork-cloexec-exec-win.c`, `process-win.c` -- **all four are `-win`** |
| `execl`/`execle`/`execlp` | `exec.c` (runs under Wine) |
| `execv`/`execve`/`execvp` | `exec.c`, plus the two `-win` files |
| `wait` | `posix-sysmisc.c`, `process-win.c` |
| `waitpid` | 21 files, most of them non-`-win` |
| `getrusage` | `exec.c`, `posix-grp.c`, `posix-sysmisc.c`, `time.c`, `process-win.c` |

A naive grep finds `fork` in 17 non-`-win` test files. Every one of
those hits is in a comment. **`fork()` is called in live test code by
exactly four files, and all four are excluded from `make check`.**

So `src/process/fork.c` -- 1461 lines in `src/process` overall, and the
module the tree's most recently reported downstream bug lives in -- is
verified by exactly one mechanism: the `windows-test` job in
`.github/workflows/ci.yml`, on `windows-latest`.

### What happens when that leg is not green

- **Red:** the board names it, which is the working case.
- **Skipped or cancelled:** `windows-test` has no `concurrency:` block
  (deliberately -- `ci.yml:364-367` records that a hang once blocked
  main for 25 minutes), but it `needs: build-test-exes`. If that job
  fails for any reason -- a toolchain fetch, an artifact upload --
  `windows-test` never runs, and `fork()` is verified by nothing at
  all, on any machine, in that run.
- **Green having run nothing:** this was true until `d36b07c`. The
  PowerShell loop iterates over `test-exes/*.exe`; an empty directory
  ran the body zero times, left `$failed` false, and exited 0. It is
  now guarded by a `$ran -eq 0` floor (`ci.yml:425-429`) and by
  `if-no-files-found: error` on the upload side. **That guard is the
  only thing standing between a rename of the artifact and `fork()`
  being unverified with a green board**, and no test asserts the guard
  itself.

None of this is visible to a developer running `make check`. The
command prints no line at all for the four excluded tests -- they are
filtered out of `TEST_RUN` before `tools/runtests.sh` ever sees them,
so they are not "skipped", they are absent. Compare the adjacent case
one line above in the same target, which *does* print
`SKIP delayall.exe (...)` when `$(CC)` lacks the flag.

**A one-line `@echo` in the `check:` recipe naming the four excluded
tests would close the entire local-visibility half of this gap.** It is
not proposed here (this document changes nothing), but it is the
cheapest item in section 8.

## 3. The `rc=77` skip matrix

```sh
grep -rn 'return 77' test/*.c
grep -rn 'SKIP' test/*.c
```

`rc=77` is the runner convention for "ran, and everything it could
check passed, but some assertion group was not exercised here". It is
recognised by `tools/runtests.sh:160-169`, `tools/asan-build.sh:344`
and `ci.yml:399-406`, each reporting it as a third bucket.

**Four files can exit 77, not five.** `test/posix-glob.c:2712` is a
`return 77` inside an `ftw()` callback -- a distinctive non-zero value
chosen to prove `ftw()` propagates a callback's return -- and
`posix-glob.c`'s `main()` returns only 0 or 1. It is a false positive
of the obvious grep.

Environments: **W** = stock apt Wine (what `make check` uses locally
and in CI's `test` job), **P** = the locally patched Wine with the
`RtlCloneUserProcess` fixes, **N** = real Windows (`windows-latest`,
the `windows-test` job). A fourth column, **A**, is `make asan`'s
native build against `fuzz/ntstubs.c`'s simulated volume, because two
of these sites fire only there.

| # | Site | Condition | W | P | N | A |
|---|------|-----------|:-:|:-:|:-:|:-:|
| 1 | `posix-socket.c:197` | `socket()` fails -- no `\Device\Afd\Endpoint` at all | no | no | no | **skip** |
| 2 | `posix-socket.c:259` | `socket()` fails in `network_probe` | no | no | no | **skip** |
| 3 | `posix-socket.c:273` | `bind()` fails -- `IOCTL_AFD_BIND` not understood | **skip** | **skip** | **contested, see below** | **skip** |
| 4 | `posix-socket.c:282` | `listen()` fails | no | no | no | n/a |
| 5 | `posix-select-socket.c:170` | `socket()` fails | no | no | no | **skip** |
| 6 | `posix-select-socket.c:175` | `bind()` fails -- same AFD ioctl | **skip** | **skip** | **contested, see below** | **skip** |
| 7 | `posix-select-socket.c:184` | `listen()` fails | no | no | no | n/a |
| 8 | `sh-main.c:423` | `../sh/sh.exe` not found relative to `argv[0]` | no | no | **skip** | **skip** |
| 9 | `spawn-stdhandle-attr.c:423` | ntdll exports no `NtCreateUserProcess` | no | no | no | **skip** |
| 10 | `spawn-stdhandle-attr.c:428` | `getcwd()` fails | no | no | no | no |
| 11 | `spawn-stdhandle-attr.c:277` | `__ntpath()` of the child image fails (per variant) | no | no | no | -- |
| 12 | `spawn-stdhandle-attr.c:284` | out of memory (per variant) | no | no | no | -- |
| 13 | `spawn-stdhandle-attr.c:298` | `RtlCreateProcessParametersEx` fails (per variant) | no | no | no | -- |
| 14 | `spawn-stdhandle-attr.c:347` | `NtCreateUserProcess` fails (per variant) | **skip D?** | **skip D?** | no | -- |
| 15 | `spawn-stdhandle-attr.c:357` | the child wrote no report (per variant) | no | no | no | -- |
| 16 | `spawn-stdhandle-attr.c:450` | the GUI copy could not be built (variant D only) | no | no | no | -- |
| 17 | `spawn-stdhandle-attr.c:456` | *no* variant reported -> `return 77` | no | no | no | **skip** |

Sites 11-16 are per-variant: each reduces `done` and only site 17
converts "every variant failed" into `rc=77`. Site 14 is marked
`skip D?` because variant D deliberately flips the PE subsystem byte to
GUI, and whether Wine's `NtCreateUserProcess` accepts that has not been
measured here -- it is the one cell in this table that is inferred
rather than observed, and it is worth ten minutes of somebody's time.

### What this says

Two rows are the whole story, and one of them may be a hole with
nothing under it.

#### Rows 3 and 6: `src/socket`'s live path may be verified nowhere

Both Wine environments skip it. Whether **real Windows** skips it too
is **contested by two comments in this tree, and I could not settle it
from here.**

- `test/posix-socket.c:263-267` (commit `f65bc0a`, "posix-socket: stop
  blaming Wine for a bind() failure it cannot diagnose") says the SKIP
  message "used to blame Wine's AFD, which was true of the environment
  it was written in and **is false on the real-Windows CI legs, where
  socket() now succeeds and the same line prints**". Read plainly: the
  bind SKIP line prints on `windows-test` too, so `bind()` fails there.
- `test/posix-select-socket.c:52-53` (commit `0e3aefa`, 26 commits
  later) says of real Windows: "**works, and is the authority for
  everything in this file**".
- `test/posix-socket.c:67-71` -- the older banner -- says the probe on
  real Windows is "untestable here, reasoned about only" and "expected
  to succeed".

The later statement is the optimistic one, and the older banner admits
its own version was reasoning rather than measurement. Meanwhile the
`windows-test` leg was, until `d36b07c`, capable of exiting 0 having
run nothing -- so a green board is not evidence that these assertions
ever ran there.

If `f65bc0a`'s reading is right, then **every assertion downstream of
`bind()` in `posix-socket.c` and `posix-select-socket.c` -- the
loopback round trip, `connect`/`accept`, `select()`/`poll()` readiness
tracking, orderly shutdown -- is verified in no environment at all**,
while three runners each report a tidy `UNVERIFIED (rc=77)`. That is
1033 lines of `src/socket` and 682 of `src/select` whose end-to-end
behaviour would be checked by nothing.

**Resolving this is one grep of one `windows-test` job log** for the
string `SKIP posix-socket network tests`. It is the highest-value
unknown in this document and it costs a minute. Until somebody does it,
the honest answer to "is anything verified nowhere?" is *possibly,
and it is the whole network stack*.

Narrowing, in fairness: `test/posix-socket-bind.c`, `posix-socket-ea.c`,
`posix-socket-connect.c` and `posix-socket-poll.c` assert the AFD
requests' *byte layouts* with no device involved, and those do run
everywhere. What is unverified is whether a real AFD accepts them --
which is precisely the thing the ReactOS-versus-real-Windows choice was
made to get right.

#### Row 8: `sh-main.c` skips on real Windows, and nothing says so

`find_sh()` (`test/sh-main.c:65-90`) walks two path components up from
`argv[0]` and appends `sh/sh.exe`. On the Windows runner the binaries
are unpacked flat into `test-exes/`, and `ci.yml:237-243` uploads only
`obj/test/*.exe` and `obj/test/*.dll` -- `obj/sh/sh.exe` is in neither
glob. So `sh-main.exe` prints `SKIP` and returns 77 on every
`windows-test` leg, for all three matrix names, on every run. The shell
*utility's* argument handling, exit status and diagnostics are
therefore verified under Wine only. `test/sh-engine.c` covers the
engine and does run everywhere; `tools/asan-build.sh:298` already
documents the same skip for the native build, in detail, and reaches
the conclusion "covered by `make check` under Wine (and real Windows
CI)" -- the last four words of which are, on this evidence, false.

Row 8 is the model failure this document exists to catch: a skip that
is **correct**, **documented**, and **documented wrongly**, in a file
whose comment is otherwise among the most careful in the tree.

## 4. All 32 `BUG:` fences

```sh
git grep -nE '#if 0[[:space:]]*/\*[[:space:]]*BUG' -- '*.c'
```

Each fence carries its own citation in-tree; this table is the index,
not a replacement for reading them.

| # | Site | Module | Requirement violated |
|---|------|--------|----------------------|
| 1 | `posix-dl.c:974` | `src/internal/rpath.c` | `dlerror()` must return NULL when no error has occurred since the last call; a *successful* `dlopen()` of a bare name leaves a pending error, because `set_err()` fires on every rpath entry that misses |
| 2 | `posix-dl.c:1019` | `src/dlfcn` | `dlopen(NULL)` shall return a global symbol-table handle for the running image; it does not |
| 3 | `posix-dl.c:1068` | `src/internal/rpath.c` | a `file` argument containing a `/` shall be used as the pathname; it is joined to the rpath instead. Tagged as a *knowing deviation*, recorded rather than changed |
| 4 | `posix-glob.c:999` | `src/fnmatch` | XCU 2.13.1: an unterminated `[` shall match the bracket character itself; the scanner walks to end-of-pattern and returns accumulated state |
| 5 | `posix-glob.c:1072` | `src/glob` | `GLOB_APPEND`: new pathnames "are not sorted together with the previous pathnames"; the whole vector is re-sorted every call |
| 6 | `posix-glob.c:1108` | `src/glob` | `GLOB_NOMATCH` for a pattern matching nothing; an *empty* pattern takes a branch that assumes mid-recursion state |
| 7 | `posix-glob.c:1130` | `src/glob` | `GLOB_MARK` is about what the pathname *is*; a trailing-slash pattern matching a directory comes back unmarked |
| 8 | `posix-glob.c:1160` | `src/glob` | XCU 2.13.3: a `/` inside an unclosed `[` makes the bracket literal. Tagged **latent** |
| 9 | `posix-glob.c:1330` | `src/wordexp` | XCU 2.6 step 2: field splitting acts on the *results* of expansion, not the input text |
| 10 | `posix-glob.c:1375` | `src/wordexp` | XCU 2.6.5: `IFS` is the delimiter set, and a null `IFS` means no splitting; space/tab/newline are hardcoded |
| 11 | `posix-glob.c:1415` | `src/wordexp` | XCU 2.6: an empty field from a complete expansion shall be deleted unless the word was quoted |
| 12 | `posix-glob.c:1462` | `src/wordexp` | on failure, `we_wordc` shall be 0; it is left unset for every error but `WRDE_NOSPACE` |
| 13 | `posix-glob.c:1925` | `src/regex` | `regexec()` shall return zero or non-zero. `run()` recurses per `I_SPLIT`/`I_SAVE` and **terminates the process** on a deep repeat |
| 14 | `posix-glob.c:1974` | `src/regex` | XBD 9.3.3: `*` is ordinary as the first character of a BRE "after an initial `^`, if any" -- after the anchor, not after the first atom. The code's own comment quotes the rule it does not implement |
| 15 | `posix-glob.c:2005` | `src/regex` | `REG_ICASE` must fold both directions inside a bracket expression; the set builder folds to lowercase and the tester does not |
| 16 | `posix-glob.c:2046` | `src/regex` | a BRE ending in an unescaped `\` is `REG_EESCAPE`, not `REG_EPAREN` |
| 17 | `posix-glob.c:2068` | `src/regex` | a missing `\}` is `REG_EBRACE`; `REG_EBRACE` is never assigned anywhere |
| 18 | `posix-glob.c:2093` | `src/regex` | XBD 9.1 leftmost-longest, including per-subpattern. Tagged **self-documented in `src/regex/regex.c`'s banner** |
| 19 | `posix-glob.c:2432` | `src/search` | `hcreate()` shall return 0 if it cannot allocate; `cap = nel + nel/2 + 8` overflows `size_t` unchecked |
| 20 | `posix-grp.c:421` | `src/misc/grp.c` | `[ERANGE]` is listed only for `getgrgid_r`/`getgrnam_r`; the non-`_r` forms set it |
| 21 | `posix-math.c:1807` | `src/math/fenv.c` | `FE_DFL_ENV` is the environment installed at program startup; `0x037F` is hardcoded (musl's Linux x86-64 value), NT starts at `0x027F` |
| 22 | `posix-math.c:1852` | `src/math/fenv.c` | `fegetenv()` shall *store*; a bare `FNSTENV` masks every FP exception as a side effect, so the getter changes what it read |
| 23 | `posix-math.c:1898` | `src/math/fenv.c` | `feholdexcept()` inherits both of the above and returns 0 unconditionally |
| 24 | `posix-signal.c:1087` | `src/signal/signal.c:309-310` | `sighold`/`sigrelse` shall fail `[EINVAL]` on an illegal signal; they ignore `sigaddset()`'s `-1` |
| 25 | `posix-signal.c:1147` | `src/signal` | `sigset()` shall return `SIG_HOLD` for a blocked signal; `<signal.h>` does not define `SIG_HOLD` and `sigset` is a bare alias of `signal()` |
| 26 | `posix-signal.c:1200` | `src/signal/signal.c:305` | `siginterrupt()` shall fail `[EINVAL]` on an invalid signal; it discards `sig` |
| 27 | `posix-stdio.c:1036` | `src/stdio` | `snprintf` shall fail `[EOVERFLOW]` when `n > INT_MAX`; it formats instead |
| 28 | `posix-stdio.c:1086` | `src/stdio` | the `[CX]` `'` flag is unrecognised, `%'d` is emitted literally, **and no argument is consumed** -- every later conversion reads the wrong one. A silent argument-stream desync |
| 29 | `posix-unistd.c:1041` | `src/unistd/unlink.c` | `unlinkat()` shall fail `[EINVAL]` on undefined flag bits; it masks them |
| 30 | `posix-unistd.c:1242` | `src/unistd/sysconf.c` | `confstr()` shall return 0 and set `[EINVAL]` for an invalid `name`; it falls through the empty-value path |
| 31 | `posix-unistd.c:1455` | `src/unistd/ttyname.c:23-24` | `tcgetpgrp`/`tcsetpgrp` shall fail `[EBADF]`; they discard `fd` |
| 32 | `pwd.c:437` | `src/misc/pwd.c` | `[ERANGE]` is listed only for `getpwuid_r`/`getpwnam_r`; the non-`_r` forms set it |

Of these, **one (#28) is a silent wrong-answer defect in code that
`make check` runs on every developer's machine and that two fuzz
harnesses target.** It survived both. That is the strongest single
argument in this document for the differential-oracle measures in
`verification-measures-2.md` over more harnesses.

## 5. All 44 `UNIMPL:` fences

```sh
git grep -nE '#if 0[[:space:]]*/\*[[:space:]]*UNIMPL' -- '*.c'
```

`UNIMPL` currently covers two different backlogs, and the tag does not
distinguish them:

- **D -- deliberate scope decision.** Somebody decided not to. The
  fence should be read as a record, not a task.
- **R -- not yet reached.** Implementable, with the NT mechanism
  usually already named in the fence.
- **F -- fixture-limited.** The *function* is not the problem; the
  fence exists because the test cannot be *constructed* in the
  environments available. These are mis-tagged: they describe the
  environment, which is what `rc=77` is for, not the implementation.

| # | Site | Class | What is unimplemented |
|---|------|:-----:|-----------------------|
| 1 | `posix-dl.c:266` | R | `mmap` `MAP_PRIVATE` -- `NtCreateSection` + `NtMapViewOfSection` named |
| 2 | `posix-dl.c:287` | R | `mmap` `MAP_SHARED` -- same section, `PAGE_READWRITE` |
| 3 | `posix-dl.c:355` | R | `mprotect` -- `NtProtectVirtualMemory`, already declared at `src/internal/nt.h:1058` |
| 4 | `posix-dl.c:384` | R | `munmap` -- `NtUnmapViewOfSection`, not yet declared |
| 5 | `posix-dl.c:402` | R | `msync` -- kernel32 `FlushViewOfFile` via the existing `LdrLoadDll` pattern |
| 6 | `posix-dl.c:425` | R | `mlock`/`munlock` -- kernel32 `VirtualLock`, semantics close but not identical |
| 7 | `posix-dl.c:494` | R | `tcgetattr`/`tcsetattr` round-tripping `ICANON`/`ECHO` -- `ENABLE_LINE_INPUT`/`ENABLE_ECHO_INPUT` are exact matches |
| 8 | `posix-dl.c:562` | R | `tcflush` `TCIFLUSH` -- `FlushConsoleInputBuffer` is an exact match |
| 9 | `posix-dl.c:707` | R | `posix_spawn_file_actions_*` -- the file demonstrates the equivalent by hand |
| 10 | `posix-dl.c:754` | R | `POSIX_SPAWN_SETSCHEDPARAM`/`SETSCHEDULER` -- `__spawn` already creates the child suspended |
| 11 | `posix-glob.c:451` | **F** | `GLOB_ERR`/`errfunc` -- "fixture not constructible under Wine" |
| 12 | `posix-glob.c:499` | **F** | `GLOB_NOESCAPE` -- "unreachable fixture on NTFS" |
| 13 | `posix-glob.c:2122` | **D** | BRE back-references `\1`..`\9` -- `src/regex/regex.c` rejects them with `REG_ESUBREG` and a documented rationale |
| 14 | `posix-glob.c:2785` | **F** | `nftw` `FTW_PHYS` -- "fixture needs a working `symlink()`" |
| 15 | `posix-socket.c:413` | **D** | `sendto`/`recvfrom`/`sendmsg`/`recvmsg`/`socketpair` -- staged in `networking-audit.md` sec 6 stages 5/6/7 |
| 16 | `posix-socket.c:425` | **D** | IPv6 (`sockaddr_in6`, `in6_addr`, `IN6_IS_ADDR_*`) -- scoped out with AF_INET/SOCK_STREAM only |
| 17 | `posix-socket.c:435` | **D** | `getsockname`/`getpeername` -- out of this stage's declared scope; the file works around it with a fixed `TEST_PORT` |
| 18 | `posix-stdio.c:1440` | R | `scanf` `%m` assignment-allocation for `%c`/`%s`/`%[` |
| 19 | `posix-termios.c:229` | R | the six `[XSI]` output delay masks (`NLDLY`, `CRDLY`, `TABDLY`, `BSDLY`, `VTDLY`, `FFDLY`) -- the tree compiles `-D_XOPEN_SOURCE=700` and defines the other six `[XSI]` names |
| 20 | `posix-time.c:531` | **D** | `getdate()` error 1 for an unset `DATEMSK` -- explicitly "not N/A: a deliberate design choice in `src/time/getdate.c`", two lines to change |
| 21-24 | `posix-wchar.c:795,823,845,872` | R | `fgetwc`/`getwc`/`getwchar`; `fputwc`/`putwc`/`putwchar`; `fgetws`; `fputws` |
| 25-26 | `posix-wchar.c:891,916` | R | `ungetwc`; `fwide` |
| 27-28 | `posix-wchar.c:936,958` | R | the six `fwprintf` forms; the six `fwscanf` forms |
| 29 | `posix-wchar.c:976` | R | `open_wmemstream` |
| 30 | `posix-wchar.c:1124` | R | `wcwidth`/`wcswidth`, BMP subset |
| 31-35 | `posix-wchar.c:1168,1180,1201,1222,1232` | R | `wcsstr`; `wcspbrk`/`wcscspn`/`wcsspn`; `wcstok`; `wcsdup`; `wcsnlen` |
| 36-38 | `posix-wchar.c:1244,1262,1279` | R | `wcpcpy`/`wcpncpy`; the four `wcscasecmp` forms; `wcstol`/`wcstoll`/`wcstoul`/`wcstoull` |
| 39-42 | `posix-wchar.c:1299,1319,1329,1348` | R | `wcstod`/`wcstof`/`wcstold`; `wcscoll`/`wcscoll_l`; `wcsxfrm`/`wcsxfrm_l`; `wcsftime` |
| 43-44 | `posix-wchar.c:1375,1389` | R | `mbsnrtowcs`; `wcsnrtombs` |

| Class | Count |
|-------|------:|
| **R** not yet reached | 36 |
| **D** deliberate scope decision | 5 (#13, #15, #16, #17, #20) |
| **F** fixture-limited, mis-tagged | 3 (#11, #12, #14) |

Two observations worth acting on:

- **24 of the 44 are `posix-wchar.c`.** That is one coherent piece of
  work -- the wide-character stdio and string families -- not 24
  independent gaps. It should be read as a single backlog item, and the
  fence-count metric flatters or damns it wrongly either way.
- **The three `F` fences are the vocabulary gap.** `posix-glob.c:451`,
  `:499` and `:2785` say nothing about `src/glob` or `src/ftw`; they
  say "this environment cannot build the fixture". That is exactly what
  `rc=77` expresses at the runner level, and there is no fence-level
  equivalent -- so the closest available tag was used. Three sites is
  not enough to justify a fifth tag; naming the environment in the
  fence text (two of the three already do) is enough. It is worth a
  line in whichever file documents the tag vocabulary.

## 6. All 30 `N/A:` fences: permanent versus conditional

```sh
git grep -nE '#if 0[[:space:]]*/\*[[:space:]]*N/A' -- '*.c'
```

`N/A` means a real mechanism makes a clause inapplicable. But a
mechanism can stop being true, and an `N/A` whose mechanism has expired
is a silent defect with no line changed. **This is the
highest-value section of this document.**

Result: **4 permanent, 25 conditional** (was 5 permanent; the
`posix-glob.c` row below was audited out -- see the note).

**Audited out (N/A -> live assertion):** `posix-glob.c:525`, tilde
expansion. The mechanism was stated as "out of scope for base `glob()`
by the specification itself". That is true of tilde *expansion* and is
exactly why literal tilde *matching* is mandatory: `glob()` matches by
XCU 2.13 Pattern Matching Notation, in which `~` is an ordinary
character, so "a leading `~` is matched literally" is a required,
observable clause, not an absent one. It is now asserted for real in
`test_glob_tilde_is_ordinary()`. Note also that the fenced test could
not have discriminated: it used `GLOB_NOCHECK`, which returns the
original pattern string verbatim, so a tilde-expanding implementation
would have passed it. The live test matches a real file named `~`
instead, which a mutated tilde-expanding `glob()` fails.

### Permanent (4) -- the mechanism cannot change

| Site | Mechanism |
|------|-----------|
| `posix-misc.c:279` | `readdir` `[ENOENT]` is a POSIX **may fail**; optional by definition, and no conformant implementation is required to detect it |
| `posix-wchar.c:1140` | `wcwidth()` on a non-BMP character: `wchar_t` is 16 bits by the NT ABI and the function takes one unit, so it is handed one surrogate half at a time. No implementation on this platform can do better |
| `posix-dl.c:313` | `MAP_FIXED` atomic replacement: `NtMapViewOfSection` with an overlapping `BaseAddress` returns `STATUS_CONFLICTING_ADDRESSES` rather than replacing, and the unmap-then-map sequence has a TOCTOU gap POSIX's contract does not |
| `posix-dl.c:737` | `POSIX_SPAWN_RESETIDS`: an NT access token has a SID set and privileges, with no real/effective/saved triple for the flag to reset |

### Conditional (25) -- with the condition named

Five conditions account for 21 of the 25. **None of them is written
down as an expiry anywhere in the tree today.**

#### C1 -- "the only tty fd class is `__FD_CONSOLE`" (10 fences)

`src/unistd/isatty.c` recognises only `__FD_CONSOLE` as a terminal, so
every serial-line concept is unreachable. **Expires the day a COM-port
or pty fd class is added** -- and every one of these fences argues from
*serial-line* semantics, which is precisely what a COM port is. The
mechanism `GetCommState`/`SetCommState` over a `DCB` is named inside
`posix-dl.c:538` as the thing that would make it observable.

| Site | Clause |
|------|--------|
| `posix-dl.c:514` | `c_cc[]` special characters (`VINTR`, `VEOF`, ...) |
| `posix-dl.c:538` | `cfgetispeed`/`cfsetospeed` -- baud rate |
| `posix-dl.c:572` | `tcflush` `TCOFLUSH`/`TCIOFLUSH` |
| `posix-dl.c:588` | `tcdrain` |
| `posix-dl.c:601` | `tcsendbreak` (spec-sanctioned no-op for a terminal with no break condition) |
| `posix-dl.c:617` | `c_cflag` `CS5`-`CS8`, `PARENB`/`PARODD`, `CSTOPB`, `CRTSCTS` |
| `posix-termios.c:475` | `tcdrain` blocking until transmitted |
| `posix-termios.c:491` | `tcflow` `TCOOFF`/`TCOON`/`TCIOFF`/`TCION` -- and the fence itself notes this page grants *no* implementation-defined escape, so the unconditional `0` return is a platform argument rather than a spec-sanctioned one |
| `posix-termios.c:533` | `c_cflag` wire encoding |
| `posix-termios.c:553` | `c_cc[]` reprogramming |

`posix-termios.c:512` (`tcflush`) sits in this group but has a
**second, much cheaper expiry**: the fence says the input half
(`TCIFLUSH`) *is genuinely implemented* via `FlushConsoleInputBuffer`
and is unobservable only because ntlibc does not wrap kernel32's
`WriteConsoleInput`, which would let a test inject into its own console
input queue. That is one wrapper away from being verifiable, and it is
the single most actionable entry in this section.

#### C2 -- "no DACL / security-descriptor storage anywhere in the tree" (4 fences)

`FILE_ATTRIBUTE_READONLY` is the only storable permission bit, and the
read/execute bits are compile-time constants in `mode_from_attrs()`.
**Expires on any real NT ACL support.**

| Site | Clause |
|------|--------|
| `posix-stdlib.c:415` | `mkstemp()` creating with mode `0600` |
| `posix-unistd.c:572` | `chmod(path, 0)` leaving the read bits set (needs DENY ACEs) |
| `posix-unistd.c:599` | `chmod`'s `0111` bits -- note NT *does* have a `FILE_EXECUTE` access right; the fence's "NT has no execute-permission attribute" is true of the *attribute* word, not of the security descriptor |
| `posix-unistd.c:635` | `S_IWGRP`/`S_IWOTH` distinct from `S_IWUSR` -- `chmod_handle()` tests `mode & 0222` as one aggregate |

#### C3 -- "no process groups, no job control, `kill(SIGSTOP)` terminates" (3 fences)

| Site | Clause | Note |
|------|--------|------|
| `posix-dl.c:799` | `POSIX_SPAWN_SETPGROUP` | expires if process groups are ever invented for this platform |
| `posix-sysmisc.c:1348` | `waitid` `WSTOPPED`/`WCONTINUED` | `kill(pid, SIGSTOP)` is `NtTerminateProcess`; NT does have `NtSuspendProcess`, so this is a design choice as much as a platform fact |
| `posix-dl.c:777` | `POSIX_SPAWN_SETSIGDEF`/`SETSIGMASK` | **arguably already expired.** The stated reason is "there is no channel to hand a chosen initial mask/disposition to a child that has not yet run its own startup". The tree has exactly such a channel: `RTL_USER_PROCESS_PARAMETERS`'s `RuntimeData`, which `test/spawn-runtimedata-stress.c` exercises, and which `src/process/spawn.c:18` already uses to hand the child a block its `crt1` reads back before `main()` |

#### C4 -- "exactly one uid, 1000, and `setuid` is a no-op" (1 fence)

`posix-unistd.c:710` -- `kill()`'s `[EPERM]` uid-mismatch case.
`src/unistd/ids.c` returns 1000 unconditionally. **Expires if uids are
ever derived from the token's SIDs.**

#### C5 -- one-offs (4 fences, and these are the interesting ones)

| Site | Clause | Condition, and how close it is to expiring |
|------|--------|--------------------------------------------|
| `posix-dl.c:162` | `RTLD_LOCAL` module-scoped symbol tables | Conditional on `dlsym()` using the NT loader's process-wide export resolution. `src/internal/pe.c` already has `ntlibc_pe_find_export`, a private export walker; a `dlsym` rebuilt on it with per-handle scoping makes `RTLD_LOCAL` observable. The fence argues from what *the NT loader* cannot do, but ntlibc is not obliged to use the NT loader |
| `posix-grp.c:788` | `readv`/`writev` atomicity vs. concurrent `read`/`write` (XBD 2.9.7) | Conditional on "NT's only scatter/gather primitives are page-granular". True, but atomicity can also be obtained by serialising -- and `2c40c74` has just added real mandatory NT byte-range locks to this tree (`src/file`). The premise is weaker than when the fence was written |
| `posix-signal.c:781` | `SIGBUS` default disposition from `EXCEPTION_DATATYPE_MISALIGNMENT` | Conditional on **two** facts: that `EFLAGS.AC` is never set on this target, and that the target is x86/x86_64. An AArch64 port makes unaligned scalar access trap by configuration, and the fence becomes false |
| `posix-misc.c:249` | `readdir` `[EOVERFLOW]` (a **shall fail**) | Conditional on `ino_t` and `off_t` both being 64-bit. The fence cites `include/bits/alltypes.h`, but that file is generated per-arch from `arch/$(ARCH)/bits/alltypes.h.gen`, and **`arch/i386` exists**. If i386's `off_t` is ever narrowed the clause becomes live and the fence becomes a silent `BUG` -- exactly the failure mode this section is about. (At `d307704` both are 64-bit on both arches; the point is that nothing checks) |

### The one that is not a mechanism argument at all

`posix-stdio.c:415` -- `popen()`'s `[EMFILE]` at `{STREAM_MAX}`. Read
the fence: its reason is that driving the process to `STREAM_MAX` "would
just be an expensive, redundant repeat of that generic exhaustion test
under a different function name". That is a **test-economy decision**,
not a platform fact. It may well be the right decision. But it is
wearing an `N/A` tag, which reads as "unobservable on NT", and the
clause is a POSIX *shall fail* that is verified nowhere and recorded as
inapplicable. Of the 30, this is the one whose tag is simply wrong.

### A note on the model case

The brief for this document cited a `stdio` audit recording the
`flockfile` family as `N/A (conditional)` -- inapplicable because there
is no `<pthread.h>` and `libpthread.a` is an empty archive, with the
expiry written down. **That fence is not in the tree at `d307704`.**
`posix-stdio.c` has exactly one `N/A` and it is the `popen` one above;
there is no `pthread.h` and no `libpthread.a` anywhere in the tree
(`find . -name 'pthread.h' -o -name 'libpthread*'` returns nothing).
Whatever recorded it is on an unmerged branch. It is nonetheless the
right pattern, and this section is an argument for applying it to all
25 conditional fences: **an `N/A` with an unstated expiry is a defect
with a timer on it.**

## 7. How each gap became invisible

The transferable part. Each of these is a distinct mechanism, and each
one made something *look* checked.

1. **Filtered out before the reporter sees it.** The four `-win` tests
   are removed from `TEST_RUN` in the Makefile, so `tools/runtests.sh`
   never receives their names and prints nothing about them. There is
   no "skipped" line because there is no skip -- there is an absence.
   Contrast the `delayall.exe` case eight lines earlier in the same
   recipe, which does echo a `SKIP`. *Signature: the gap is upstream of
   the thing that reports.*
2. **A loop with no floor.** `windows-test` iterated over an artifact
   directory; empty directory, zero iterations, `$failed` still false,
   exit 0. Fixed at `d36b07c` with a `$ran -eq 0` check. *Signature:
   success is the default and failure has to be actively reached.*
3. **A skip that fires everywhere it matters.** `sh-main.c` returns 77
   on the Windows leg every single run, because the artifact glob does
   not include `obj/sh/sh.exe`. `rc=77` is reported honestly in its own
   bucket -- but "1 unverified" in a run summary is indistinguishable
   from a legitimately environment-limited skip, and nobody reads which
   one. *Signature: the honest report is too coarse to be actionable.*
4. **A comment that was true when written.** `tools/asan-build.sh:298`
   says `sh-main` is "covered by `make check` under Wine (and real
   Windows CI)". The parenthetical is false, and the file is one of the
   most carefully written in the tree. *Signature: documentation is not
   executable, so it decays silently.*
5. **A tag that describes the wrong thing.** Three `UNIMPL` fences
   describe fixtures that cannot be built, not functions that do not
   exist; one `N/A` fence describes a test that was judged not worth
   writing, not a clause that cannot be observed. The counts in
   `POSIX-GAP-ACCOUNTING.md` treat all of them as what their tag says.
   *Signature: a taxonomy with no slot for the real reason forces a
   wrong answer.*
6. **A premise that stopped being true.** 25 `N/A` fences rest on facts
   about the tree, not about NT. Two of them (`posix-dl.c:777`,
   `posix-grp.c:788`) are already weaker than when they were written,
   because of changes made in this repository since. *Signature: the
   fence is correct, the reasoning is correct, and the world moved.*
7. **A measurement that reports something adjacent.** libFuzzer's
   `-print_coverage=1` prints the last input's coverage, and reads
   exactly like cumulative coverage. Anyone using it to answer "what do
   the harnesses reach?" gets a plausible, wrong, and *stable* answer.
   *Signature: the tool answers a question you did not ask, in the
   format of the question you did.*

The common shape: **every one of these produced output.** None of them
was silent. Six of the seven produced output that a reasonable reader
would take as evidence of coverage.

## 8. Ranking

By expected defects caught per unit of work, with what each would
**not** catch stated, because that is the part that gets omitted.

| # | Work | Expected yield | Would not catch |
|---|------|----------------|-----------------|
| 0 | **Grep one `windows-test` job log for `SKIP posix-socket network tests`** | Settles whether `src/socket` and `src/select`'s live paths are verified anywhere at all (section 3). One minute of work against a possible 1715-line hole | Nothing by itself. It tells you which of the next two items you need |
| 1 | Add `obj/sh/sh.exe` to the `build-test-exes` upload glob, or make `sh-main.c` report *why* it skipped in a form the runner surfaces | Restores real-NT verification of the shell utility's argv/exit-status/diagnostic surface -- currently Wine-only, believed to be both. One artifact-path change | Nothing about `src/sh`'s engine, which `sh-engine.c` already covers everywhere |
| 2 | Write the expiry condition into each of the 25 conditional `N/A` fences, in the `N/A (conditional): ... expires when X` form | Converts 25 silent future defects into 25 grep-able ones. Highest ratio in this document: it is typing, not engineering | Nothing today. Its entire value is deferred, which is exactly why it does not get done |
| 3 | One `@echo` in `check:` naming the four `-win` tests as not run | Makes the largest single coverage hole in the tree visible to every developer on every run | Nothing new is verified. It changes who knows |
| 4 | `fuzz_regex` + `fuzz_fnmatch` | 2 of the 15 fences in the four unfuzzed pattern modules (`:1925` recursion kill, `:999` scanner overrun), plus the unknown crash class in 885 lines of parser that has never seen adversarial input | The other 13 -- all semantic misreadings of clauses, invisible without a differential oracle |
| 5 | Re-tag `posix-stdio.c:415` from `N/A` to a `BUG`-or-backlog entry, and the three `F`-class `UNIMPL` fences with their environment named | Corrects 4 of 106 fence classifications, which is the input to two ledgers | No code changes; the clauses stay unverified either way |
| 6 | Corpus persistence for the nightly fuzz job | Directly measured: the format-string harnesses reach 25-31 features cold against `fuzz_path`'s 264. A seeded corpus is the difference between fuzzing `snprintf` and fuzzing its argument parser's first branch | Nothing in the 26 unfuzzed modules. Owned by another agent |
| 7 | Harness-side `__llvm_profile_write_file()` so per-module fuzz coverage becomes measurable | Turns section 1's entry-point table into a measured one, and would settle whether `src/string` is genuinely exercised | No defects directly. It is instrumentation for deciding items 4 and 6 |
| 8 | A second verifier for `fork()` -- the patched Wine at least, run in CI | `src/process/fork.c` currently has exactly one verifier, one CI job wide, and it is the module with the most recent downstream bug report | Real-NT-only behaviour, which is most of what the four tests are for. This buys redundancy, not new coverage |

### Where the gaps are acceptable, and why

Saying so matters; an accounting that reads as an indictment gets
ignored.

- **The four `-win` tests being excluded from `make check` is correct.**
  Stock Wine has no `RtlCloneUserProcess` and a forking program *hangs*.
  A hang in a test suite is worse than an absence: it blocks, it has no
  diagnostic, and it took 25 minutes of a CI runner once already. The
  filter is right. Only its invisibility is wrong.
- **The `rc=77` convention is working as a mechanism**, in three
  independent runners, and it is the reason the socket question in
  section 3 is answerable at all rather than hidden behind a green
  `PASS`. What it does not do is distinguish "skipped here, checked
  there" from "skipped everywhere" -- a run summary reports both as
  `N unverified`. That distinction is not a defect in the convention;
  it needs a cross-environment view, which is what section 3 is.
- **5 permanent `N/A` fences are genuinely permanent** and need
  nothing. `wcwidth()` on a surrogate half cannot be fixed by anyone.
- **24 `UNIMPL` fences in `posix-wchar.c` are one backlog item**, well
  described, with the implementation approach written down for each.
  That is a healthy state, not a gap.
- **Fuzzing 4 of 30 modules is a reasonable allocation** *if* the four
  are the highest-entropy input parsers, which `strtod`, `printf`,
  `scanf` and `__ntpath` are. The argument for `regex`/`fnmatch` is not
  that 26 modules are unfuzzed; it is that these two are also
  adversarial-input parsers and have 7 fences between them.
