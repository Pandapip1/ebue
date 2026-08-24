<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# External conformance suites, measured against this tree

Whether an established POSIX test suite would find things
`test/POSIX-COVERAGE.md` and `test/POSIX-GAP-ACCOUNTING.md` do not, and
what hooking one in would cost.

Everything below was **measured**, not estimated. Method for every
"could it run" number: the suite's own test sources were compiled and
linked against a fresh clone of `origin/main` at **`d36b07c`**, built
`./configure --host=x86_64-win32 CC=x86_64-win32-tcc && make -j8`, using
the exact flags of the `Makefile`'s `obj/test/%.exe` rule
(`Makefile:284-285`), and the resulting PEs were run under stock apt
Wine with `WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d`, stdin from
`/dev/null`, `timeout -k 2` per test. Base revisions of every external
tree are quoted as SHAs, per the standing rule about branch names in
shared checkouts.

## Contents

- [The answer](#the-answer)
- [The suites](#the-suites)
  - [S1: Open POSIX Test Suite (in LTP)](#s1-open-posix-test-suite-in-ltp)
  - [S2: LTP proper](#s2-ltp-proper)
  - [S3: The Open Group VSX-PCTS / VSC-PCTS](#s3-the-open-group-vsx-pcts--vsc-pcts)
  - [S4: musl `libc-test`](#s4-musl-libc-test)
  - [S5: pjdfstest](#s5-pjdfstest)
- [What fraction can run here](#what-fraction-can-run-here)
- [Would it find anything new](#would-it-find-anything-new)
- [The build assumptions that break](#the-build-assumptions-that-break)
- [Recommendation and cost](#recommendation-and-cost)
- [Integration sketch](#integration-sketch)
- [Reproducing these numbers](#reproducing-these-numbers)

## The answer

Two different answers, because the two obvious candidates are not
comparable.

**The Open POSIX Test Suite is not worth integrating.** 36.7% of it
compiles here, but 71% of *that* is a single mechanically generated
directory (`sigaction/`), and once Wine's missing `RtlCloneUserProcess`
and this library's documented no-alternate-stack decision are subtracted
there are **nine distinct behaviours** left that it finds — most of them
already named in the ledger. It contains **no test at all** for any of
the five defects this tree's own audits fenced, and its subject matter
(pthreads, message queues, realtime signals, `aio`, `mmap`,
semaphores) is exactly the half of POSIX this library does not have.

**musl's `libc-test` is worth integrating, and it is the better fit by
a wide margin.** 61% of its functional+regression corpus links against
this tree with a five-function shim, it runs in **4 seconds**, and on
first run it found **22 distinct defects**, eleven of them in `fnmatch`
and `inet_pton`/`inet_addr` — two interfaces sitting in the ledger's
*implemented, not clause-audited* bucket, i.e. exactly the hole
`test/POSIX-GAP-ACCOUNTING.md` says it has. Its 174 linkable math tests
add a further set, including a clear-cut one (`fma()` is not fused) that
no clause audit of `math.h` would have phrased as an assertion.

The suite that *would* have caught a named fenced defect is LTP proper,
and LTP proper cannot run here at all. That is worth stating plainly:
[see below](#l1-the-one-named-defect-an-external-suite-catches).

## The suites

### S1: Open POSIX Test Suite (in LTP)

Lives inside the Linux Test Project at
`testcases/open_posix_testsuite/`; measured at LTP **`4c0cfb8`**
(`https://github.com/linux-test-project/ltp`). It is also mirrored
standalone at `https://sourceforge.net/projects/posixtest/`, which has
not moved since the LTP merge.

| | |
|---|---|
| licence | `testcases/open_posix_testsuite/COPYING` line 1-2: *"All sourcecode generated from scratch by Ngie Cooper is BSD 2-clause licensed. All legacy openposix test suite code is GPLv2+ licensed."* `README` §1 adds *"All code is distributed under the GNU General Public License v2."* |
| size | 14 MB; 1610 `.c` under `conformance/`, 15 under `functional/`, 50 under `stress/` |
| shape | 191 directories under `conformance/interfaces/`, one per POSIX interface, plus an `assertions.xml` per directory tying each test file to a numbered assertion of that page |
| harness | essentially none. `include/posixtest.h` is a handful of `PTS_*` return-code and attribute macros; `lib/common.c` is **12 lines** — a `main()` that calls `test_main()`. Each test is one self-contained `.c` with its own executable. |
| build | needs no autotools run to compile an individual test; `configure.ac` and the `Makefile` exist for the full-tree run |

The harness being that thin is the suite's best property and it is worth
saying so: nothing had to be ported to compile these. Every failure
below is a missing header or a real behavioural difference, never build
machinery.

What it actually tests, by directory count: **95** `pthread_*`, 10
`mq_*`, 9 `sem_*`, 8 `sched_*`, 7 `aio_*`, 6 `clock_*`, 5 `timer_*`, 2
`shm_*`, `mmap`/`munmap`/`mlock`/`mlockall`/`munlock`/`munlockall`, the
signal family, and eleven stragglers (`asctime`, `ctime`, `difftime`,
`gmtime`, `localtime`, `mktime`, `time`, `strchr`, `strcpy`, `strlen`,
`strncpy`, `strftime`, `getpid`, `fsync`, `fork`, `access`). That
distribution is the whole finding: the suite is a **POSIX.1b/POSIX.1c
realtime-and-threads suite** with a signal chapter attached, which is
the complement of what this library implements.

### S2: LTP proper

Same clone, `4c0cfb8`. `COPYING` is GPLv2. 385 directories under
`testcases/kernel/syscalls/`, 1396 `.c` files, of which **1122 include
`tst_test.h`** — the LTP framework, `include/tst_test.h` at 849 lines
plus `lib/tst_test.c` at 2089.

The framework is not portable and is not trying to be. `lib/*.c` reads
**19 distinct `/proc` paths** by literal string —
`/proc/self/mounts`, `/proc/self/maps`, `/proc/cpuinfo`,
`/proc/sys/kernel/pid_max`, `/proc/self/oom_score_adj`,
`/proc/sys/kernel/tainted`, `/proc/self/uid_map` among them — and
`lib/tst_test.c` includes `<sys/mount.h>` and forks in 12 places. It
also wants loop devices, cgroups and root. Porting the framework is not
a smaller job than writing the tests.

LTP is a **kernel** test suite. That matters for scope as much as for
portability: it tests `unlinkat(2)` the syscall, not `unlinkat()` the
POSIX interface, and roughly a third of `testcases/kernel/syscalls/` is
Linux-only calls (`bpf`, `add_key`, `arch_prctl`, `clone3`,
`close_range`, `cachestat`, …) with no POSIX counterpart at all.

### S3: The Open Group VSX-PCTS / VSC-PCTS

These are the official certification suites, and the honest finding is
that the free one is **no longer downloadable** and the current one
requires a signed agreement.

The Open Group's own index
(`https://posix.opengroup.org/testsuites.html`, "Current Test Suites")
lists VSX-PCTS2016 v1.15 and VSC-PCTS2016 v3.1 (System Interfaces, and
Shell & Utilities respectively), plus the 2003 pair. Each is gated on a
PDF **"Time-Limited License Agreement"**; the page states that
organisations developing open source implementations qualify for *"a
twelve month free license"*, obtained by applying, and that the realtime
suites (VSPSE54-2003, VSPSE52-2003) require contacting
`obconformance@opengroup.org`. So: obtainable in principle, on
application, for twelve months, not by download.

The obsolete **VSX-PCTS 4.4.4** (November 1999, POSIX.1-1990 with
FIPS 151-2 and POSIX.1-1996 modes) *is* free software. Its licence page
(`https://pubs.opengroup.org/onlinepubs/063392154/Licence.html`) says
*"This program is free software; you can redistribute it and/or modify
it under the terms of the 'The Open Group Test Suite License'"* — OGTSL,
OSI-approved — with a preamble requiring that **the original test modes
be preserved** in any modified version. Its FAQ
(`https://www.opengroup.org/testing/downloads/vsx-pcts-faq.html`) says
it holds *"over 6,000 tests for IEEE Standard POSIX 1003.1-1990"* and
*"uses the industry standard Test Environment Toolkit (TET) as its
harness."*

But the three tarballs the download page
(`https://pubs.opengroup.org/onlinepubs/063392154/`) still links —
`vsx-pcts-4.4.4.tar.Z` (4 MB), `vsxgen-os-1.4A.tar.Z` (570 KB),
`vsx-vtools-1.4.tar.Z` (132 KB) — **all return HTTP 404** as of
2026-08-24, from both `pubs.opengroup.org` and
`www.opengroup.org/testing/downloads/`. The Wayback Machine's
availability API reports **no archived snapshot** for any of the three.
Only `README` and `Licence.html` from that directory still resolve (200).

Even if a copy surfaced, three things would sink it here: it targets
POSIX.1-**1990**, which is two revisions behind the ledger's
POSIX.1-2017 baseline; it is TET-harness-based, so unlike OPTS it needs
a real port before a single test runs; and OGTSL's preserve-the-original-
test-modes clause is a redistribution constraint a GPL-3.0-or-later tree
would have to keep vendored code separated for. **Rejected on
obtainability**, not on merit.

### S4: musl `libc-test`

Upstream `git://repo.or.cz/libc-test.git` at **`68edb8b`**. This is the
suite musl itself develops against; `README` opens *"libc-test is
developed as part of the musl project."*

| | |
|---|---|
| licence | MIT (`COPYRIGHT`: *"libc-test is licensed under the following standard MIT license"*) — the only candidate with no copyleft friction against this tree |
| size | 14 MB; `src/functional` 77 `.c`, `src/regression` 69, `src/math` 206, `src/api` 79 (compile-only declaration checks), `src/common` 10 helpers |
| shape | one test per file, own `main`, returns 0 or non-0, prints via `t_error()` |
| harness | `src/common/test.h` + `print.c`. Design goals in `README` state it outright: *"tests should be easy to run and build even a single test in isolation"*, *"the test system should have minimal dependency (libc, posix sh, gnu make)"*, *"the test system should run on all archs and libcs"* |

It is not a POSIX-clause conformance suite in the OPTS sense — there is
no assertion-to-specification-line traceability, which is precisely what
`test/POSIX-COVERAGE.md` provides and what this suite does not. What it
is instead is a **regression corpus**: `src/regression/` is 69 files
each named for a specific bug (`lrand48-signextend`,
`ftello-unflushed-append`, `printf-fmt-g-round`, `memmem-oob-read`,
`inet_pton-empty-last-field`), each carrying the musl commit that fixed
it. That is a different and complementary kind of evidence from a clause
audit: the ledger proves the specification was read, this proves the
mistakes other libcs actually made were checked for.

### S5: pjdfstest

`https://github.com/pjd/pjdfstest` at **`85a8aea`**, 1.5 MB, BSD-licensed,
actively maintained (HEAD is a `utimensat` Y2038 fix). 238 `.t` files
covering `chmod`, `chown`, `link`, `mkdir`, `mkfifo`, `open`, `rename`,
`rmdir`, `symlink`, `truncate`, `unlink`, `utimensat` — genuinely the
filesystem corner of POSIX this tree cares about.

Unusable here, for reasons that are structural rather than fixable:

- `README` "Prerequisites": *"You must be root when running these
  testcases"*, and *"perl"* plus *"TAP-Harness (perl package)"*; the
  driver is `prove -rv`.
- Every test is a POSIX shell script. `tests/unlink/01.t` alone uses
  backtick command substitution (`` n0=`namegen` ``), `dirname $0`, dot-
  sourcing `../misc.sh`, and shell functions. `sh/main.c` as landed has
  none of those — no command substitution, no control flow, `cd` as its
  only builtin — and `tests/misc.sh` additionally branches on
  `$(id -u)`.
- The assertions are about POSIX uid/gid/sticky-bit/setgid semantics,
  which the NT security model does not present through this library.

Recorded so nobody re-investigates it.

## What fraction can run here

The decisive number, counted three ways so the shape is visible.

**By interface.** Of OPTS's 191 `conformance/interfaces/` directories,
**42 name something `include/` declares**; 149 do not. That 42 already
counts interfaces that are declared-but-degenerate (`sigwait`,
`sigaltstack`).

**By compile.** All 1610 conformance tests were compiled and linked
against `lib/libc.a`:

| | count | share |
|---|---|---|
| compile **and** link | **591** | 36.7% |
| blocked on an absent header | 873 | 54.2% |
| other compile/link error | 146 | 9.1% |

Absent-header breakdown, first error per file: `pthread.h` **519**,
`mqueue.h` 126, `sys/mman.h` 97, `aio.h` 71, `semaphore.h` 56,
`langinfo.h` 2, `sys/ipc.h` 1, `nl_types.h` 1. The remaining 146 are
mostly `struct sigevent`/`timer_t` incomplete-type errors (27),
unresolved references at link (25) and undeclared identifiers (15) in
the `timer_*` and `sched_*` directories.

The 36.7% is misleading on its own, and this is the number that decides
it: **420 of the 591 are the `sigaction/` directory**, which is a
mechanically expanded matrix — 501 files for 25 numbered assertions,
most of them the same test repeated per signal. Outside `sigaction/`,
**171 tests across 44 interfaces link**. Weighted by *distinct
behaviours checked* rather than by file, the runnable share of OPTS is
in the low single digits.

**By execution.** All 591 were then run under Wine:

| outcome | count |
|---|---|
| `PTS_PASS` | 357 |
| `PTS_FAIL` | 176 |
| `PTS_UNSUPPORTED` | 17 |
| `PTS_UNRESOLVED` | 8 |
| `PTS_UNTESTED` | 6 |
| exit 255 (test called `exit(-1)`) | 26 |
| timeout at 15 s | 1 (`nanosleep/10000-1.c`) |

Of the 176 failures, **134 are one thing**: `wine: Call from ... to
unimplemented function ntdll.dll.RtlCloneUserProcess, aborting`. They
are `fork()`-based tests, and note that under this configuration they
**abort rather than hang** — the 15-second `timeout` was never the thing
that saved the run, Wine's own abort was. (That is not a licence to drop
the timeout; one test still hit it.)

Subtract those and the 26 `exit(-1)`s resolve as follows.

`libc-test` for comparison, same method:

| corpus | files | link | pass | fail |
|---|---|---|---|---|
| `src/functional` + `src/regression` | 146 | **89 (61%)** | 62 | 27 |
| `src/math` (top level) | 199 | **174 (87%)** | 92 | 82 |

Linking `libc-test` needed one thing OPTS did not: five helper functions
(`t_vmfill`, `t_memfill`, `t_setrlim`, `t_fdfill`, `t_setutf8`) whose
real implementations in `src/common/` include `<sys/mman.h>` and
`<langinfo.h>`. Stubbing them to return failure — 6 lines — took the
link rate from 0/146 to 89/146. Absent headers still block the other 57:
`pthread.h` 23, `langinfo.h` 3, `semaphore.h`/`resolv.h`/`iconv.h` 2
each, and one apiece for `tgmath.h`, `sys/syscall.h`, `sys/shm.h`,
`sys/sem.h`, `sys/msg.h`, `sys/mman.h`, `spawn.h`, `mntent.h`,
`crypt.h`.

## Would it find anything new

### The five fenced defects

Each named defect, against each suite, by grep and by reading the tests
that matched. Note that only two of the five are fenced in the tree at
`d36b07c` (`unlinkat` at `test/posix-unistd.c:1041`, and `newlocale`
visible directly at `src/misc/locale.c:58` as `(void)mask;`); the other
three are presumably in flight elsewhere. The question of whether a
suite *contains such a test* is answerable regardless.

| fenced defect | OPTS | LTP proper | `libc-test` |
|---|---|---|---|
| `newlocale` ignores `category_mask`, invalid mask returns success not `[EINVAL]` | no test — no `newlocale` directory, zero references in 1876 `.c` | no test | **no** — `newlocale` appears only as a *helper* in `src/regression/uselocale-0.c` and in the declaration-only `src/api/locale.c`; nothing checks the mask |
| `snprintf` does not fail `[EOVERFLOW]` for `n > INT_MAX` | no test (14 files *call* `snprintf`, none test it) | no test | **partial, and it passes.** `src/functional/snprintf.c:171-176` tests the *other* `EOVERFLOW` path — output length, `snprintf(NULL, 0, "%.*u ", 2147483647, 0)` must return -1 with `EOVERFLOW`. ntlibc passes that. The `n` parameter is untested. |
| `uselocale` never returns `LC_GLOBAL_LOCALE` | no test | no test | **no.** `src/regression/uselocale-0.c` checks only that `uselocale(0)` returns the locale previously set and does not change it. `LC_GLOBAL_LOCALE` appears only in `src/api/locale.c`, a declaration check. ntlibc passes `uselocale-0`. |
| `regexec()` unbounded recursion on `(a*)*b` | no test | no test | **no.** Six regex regressions (`regex-backref-0`, `regex-bracket-icase`, `regex-ere-backref`, `regex-negated-range`, `regex-escaped-high-byte`, `regexec-nosub`); none is about nested-quantifier blowup, and grepping the whole tree for `(a*)*` finds nothing. All six were run; five pass. |
| `unlinkat` masks undefined flag bits and deletes the file | no test | **yes** | **no** — `unlinkat` appears only in `src/api/unistd.c` |

#### L1: the one named defect an external suite catches

`testcases/kernel/syscalls/unlinkat/unlinkat01.c` at LTP `4c0cfb8` is a
table-driven test whose fifth case is, verbatim:

```c
{0, testfile, 9999, EINVAL},
```

— fd, filename, flag, expected errno. An implementation that masks the
undefined bits out of `9999` and unlinks the file fails that row. That
is the fenced defect exactly.

It is also the only one, out of five, in any of the three suites; and it
sits in the one suite whose framework (`tst_test.h`, `/proc`, root, loop
devices) cannot be brought up here. The honest reading is that **an
external suite would have caught 1 of 5**, at a porting cost far above
writing the assertion by hand.

### The reverse direction: what the suites test that the ledger does not

This is where the value is, and it is not small.

**OPTS, after subtracting environment noise.** 176 fails − 134
`RtlCloneUserProcess` aborts = 42, plus the 26 `exit(-1)`s. Grouped by
what they are actually about:

| what | tests | already in the ledger? |
|---|---|---|
| `SA_ONSTACK` / alternate signal stack not honoured (`sigaltstack` returns 0 while doing nothing — `src/signal/signal.c:306`) | 26 (`sigaction/12-*`) + 7 (`sigaltstack/*`) | **yes** — `POSIX-GAP-ACCOUNTING.md:439` records it as a deliberate degenerate stub, `POSIX-COVERAGE.md:602` marks it N/A |
| `nanosleep`/`clock_nanosleep` return before the requested interval is observable on the clock | 10 + 9 | **no**, but the tests request 3 ns and check the clock advanced; a 100 ns tick fails them. POSIX permits `_POSIX_CLOCKRES_MIN` of 20 ms, so this is the suite being stricter than the standard, not a defect |
| `sigsuspend`, `sigwait`, `sigrelse`, `sighold` | 4 + 3 + 1 + 1 | **yes** — `sigwait` is a documented permanent stub (`include/signal.h:223-225`); the rest follow from the no-asynchronous-delivery model that header states |
| `kill`/`killpg` to something other than self | 3 + 2 | **yes** — `include/signal.h`: *"kill() can only end a process, not interrupt it"* |
| `raise` | 1 | worth one look |

So OPTS's net contribution over the ledger is roughly **one open
question** (`nanosleep` resolution, probably a non-finding) and a
confirmation that four documented stubs are in fact stubs. That is the
case against integrating it, stated as a measurement.

**`libc-test`, same treatment.** 27 fails; 5 are environment
(`fcntl`, `vfork`, `fflush-exit` abort on `RtlCloneUserProcess`;
`execle-env` gets `ECHILD`; `popen` has no shell to run
`read a ; test "x$a" = xhello`). The other **22 are behavioural**, and
several land squarely in ledger blind spots:

| test | what it reports | ledger status |
|---|---|---|
| `functional/fnmatch` | 5 failures: `[[?*\]` vs `\`, `[/b` literal, `[![:d-d]` malformed-class handling under `FNM_PATHNAME` | `fnmatch` is in **implemented, not clause-audited** (`POSIX-GAP-ACCOUNTING.md:264`) — no assertion exists |
| `functional/inet_pton` + 2 regressions | 6+3 failures: `inet_pton(AF_INET,"1.2.03.4")` accepts a leading zero; `inet_addr("1.2. 3.4")` accepts embedded space; `inet_pton(AF_INET6,"::")` fails; `1:2:3:4:5:6:7::` mis-parsed; `inet_ntop` v4-mapped | `arpa/inet.h` is **implemented, not clause-audited** (`:242`) |
| `functional/memstream` | `fseek(f,6,SEEK_CUR)` past end of an `open_memstream` buffer, then `ftell` returns 11 vs 5; content `h104o` vs `hello104` | ledger mentions memstream twice, has no clause row |
| `functional/random` | `setstate()` does not restore the sequence — 6 mismatches | ledger row exists (`POSIX-COVERAGE.md:227`) but asserts only the **return value**, not that the restored state reproduces the stream |
| `regression/lrand48-signextend` | `lrand48()` from a known seed gives 366850414 / 1610402240 / 206956554, want 0 / 2116118 / 89401895 | ledger row (`:221`) asserts only the `[0,2^31)` range. The three expected values are fixed by POSIX's specified 48-bit LCG, so this is a real divergence |
| `functional/strftime` | `%03C`, `%+3C`, `%01C`, `%012F`, `%+10F` emitted literally (flags/width unimplemented); `%c` for year 10009 omits the `+` | ledger row (`:286-287`) covers `%U %W %V %G %g` pass-through, says nothing about flags or field width |
| `functional/strptime` | `%C` alone yields 1800 not 1856; `%s` unparsed | not covered |
| `functional/sscanf` | `%8c%8c` on a 13-byte input returns 2 fields, expected 1 | not covered |
| `functional/wordexp` | 6 failures, all field splitting: `$FOO` where `FOO="bar baz"` yields one word, not two | ledger has 12 field-splitting mentions and `test/posix-glob.c:1226,1271` fences some of this already — **overlaps existing fenced work** |
| `functional/utime` | `utimensat(AT_FDCWD, "/dev/null/invalid", …, 0)` gives `ENOENT`, POSIX wants `ENOTDIR` | not covered |
| `functional/setjmp` | `siglongjmp` does not restore the signal mask | ledger mentions `siglongjmp` twice; this specific behaviour untested |
| `regression/ftello-unflushed-append` | `ftello` on an append stream before flush: 3, want 7 | ledger has `ftello` rows; this case is not among them |
| `regression/regex-escaped-high-byte` | `regcomp("\\\xfc")` returns 0, want `REG_BADPAT` | `REG_BADPAT` appears **nowhere** in either ledger or any `test/*.c` |
| `regression/statvfs` | `/` reports 0 file nodes (`f_files`) | ledger mentions `f_files`; assertion is weaker |
| `regression/sigprocmask-internal` | `sigaddset(&s, 32..34)` accepts signals musl reserves; blockable | design difference (`__libc_current_sigrtmin()` returns 35), not a defect — worth a documented divergence note |
| `regression/rlimit-open-files` | `setrlimit(RLIMIT_NOFILE, 42)` returns `EINVAL`; the limit is not enforced | plausibly a documented platform gap; needs triage |
| `regression/printf-fmt-n` | `%n` mismatch | needs triage; the reported values print identically |
| `regression/malloc-oom`, `setenv-oom` | both depend on the stubbed `t_vmfill` | **must be reported unverified**, not failed — see below |

**`libc-test` math.** 82 of 174 fail, and this corpus needs care: musl's
math tests are deliberately stricter than the standard (crlibm reference
values, per-rounding-mode ULP bounds), and mainstream libcs fail a
fraction of them too. Triaged, three classes are unambiguous and cheap:

- **spurious exception flags.** `logb(±inf)` raises `FE_DIVBYZERO`;
  `exp(inf)` raises `FE_DIVBYZERO`; `pow` raises `FE_DIVBYZERO` on
  ordinary finite arguments (`pow(0x1.cfdd8p+17, 3)`). POSIX specifies
  no exception for any of those. Nothing in `test/posix-math.c` checks
  the *absence* of a flag on an ordinary call.
- **`fma()` is not fused.** `src/math/sanity/fma.h` cases fail at
  0.5-0.8 ULP. A correct `fma` is exact — 0 ULP by definition. This is
  a real defect and one a clause audit is unlikely to phrase as an
  assertion, because the specification sentence ("computed as if to
  infinite precision") does not look like a testable predicate until
  someone hands you the vectors.
- **`isless`/`islessequal`/`islessgreater`/`isunordered` raise
  `FE_INVALID` on NaN.** These macros are specified not to.

The rest (large-argument `sin`/`cos` reduction, subnormal `UNDERFLOW`
signalling in `nextafter`, long-double behaviour) is a triage backlog,
not a gate.

## The build assumptions that break

Concretely, with how many tests each blocks.

1. **Absent headers.** The single biggest factor, and it is not a
   porting problem — it is the gap accounting restated as a build error.
   OPTS: 873 of 1610 (54%) die at `#include`. `libc-test`
   functional+regression: 57 of 146 (39%). No amount of harness work
   moves these; only implementing `pthread.h` would.
2. **`fork()` under stock apt Wine.** 158 of the 591 linkable OPTS tests
   call `fork()`; 134 aborted on `RtlCloneUserProcess`, as did 3
   `libc-test` tests. **Measured behaviour is abort, not hang**, at
   ~0.3 s each — but only with `WINEDLLOVERRIDES=winedbg.exe=d` set. Run
   without it and Wine launches `winedbg --auto`, which is where the
   hang comes from. Any integration must export that override, not pass
   it per-command.
3. **No `/bin/sh` on the target.** Blocks `libc-test`'s `popen` and
   `system`-adjacent tests (2 files), and all 238 pjdfstest scripts.
   `sh/main.c` cannot close this gap: pjdfstest needs command
   substitution, `case`, and functions on line 10 of its first test.
4. **`t_vmfill`/`t_setrlim`.** `libc-test`'s OOM-behaviour helpers need
   `<sys/mman.h>` and `setrlimit` enforcement. Stubbing them unblocks
   the link for the whole corpus but makes 2 tests (`malloc-oom`,
   `setenv-oom`) structurally unable to check anything. They must report
   *unverified*.
5. **`/tmp` and filesystem layout.** Only 2 OPTS tests reference `/tmp`;
   `libc-test` uses `t_pathrel()` against `argv[0]`, which works. Not a
   real obstacle for either.
6. **`langinfo.h` in a shared helper.** `libc-test`'s
   `src/common/utf8.c` includes it, so it must be dropped from the
   helper set; nothing in the linkable corpus needed it.
7. **LTP's framework.** `/proc` in 19 places, `sys/mount.h`, root, loop
   devices, cgroups. Blocks 1122 of 1396 syscall tests outright, and the
   remaining 274 are not organised to run without it.

Not an obstacle, and worth recording because it was the expected one:
**the compiler.** `x86_64-win32-tcc` compiles and links one of these
tests in **6 ms**. All 1610 OPTS tests build from clean in **0.78 s**
wall at `-P8`. Build cost is not a factor in any decision below.

## Recommendation and cost

**Adopt `libc-test`; do not adopt the Open POSIX Test Suite; record
VSX-PCTS and pjdfstest as investigated and rejected.**

The case for `libc-test` in one line: it costs a 6-line shim and 4
seconds of wall clock, and it found 22 defects on its first run,
including eleven in two interfaces the gap accounting explicitly lists
as un-audited.

The case against OPTS in one line: 71% of what compiles is one generated
directory, and net of Wine and of stubs the ledger already documents, it
contributes one open question about clock resolution.

Cost estimate, in the units that matter here:

| item | cost |
|---|---|
| vendor or submodule `libc-test` at a pinned SHA | 1 h — MIT, no REUSE friction, `LICENSES/MIT.txt` |
| the helper shim (`t_vmfill`, `t_setrlim`, `t_fdfill`, `t_setutf8`) + drop `utf8.c` | 30 min, already prototyped |
| build/run driver reusing `tools/runtests.sh`'s rc contract | 2 h |
| the expected-failure ledger (see below) — the real work, because each of the 22 has to be triaged into defect / documented divergence / suite-is-stricter-than-POSIX | **1-2 days** |
| math corpus triage (82 failures) | a further 1-2 days, and it should be a separate, later step |
| wiring into `tools/gate.sh` | 1 h |

The triage line is the honest cost. Wiring the suite up is an afternoon;
deciding what its 22 red lines *mean* is the part that takes days, and
skipping it produces exactly the failure mode this project spent today
fixing — a stage that is green because nobody looked.

## Integration sketch

Only for `libc-test`. Sketch, not a patch; `tools/`, `Makefile` and
`ci.yml` are owned elsewhere right now.

### Where it lives

- `third_party/libc-test/` — vendored at a pinned SHA, keeping upstream
  copyright and `MIT` SPDX per `tools/install.sh`'s vendoring
  convention. `reuse` needs `LICENSES/MIT.txt` and a `.reuse/dep5`
  stanza; upstream ships `COPYRIGHT`, not per-file headers.
- `test/libc-test-shim.c` — ours, GPL-3.0-or-later, the five helpers.
- `test/libc-test-expected.txt` — the ledger. One line per test:
  `name  status  reason`, where status is `pass`, `xfail-<ledger-ref>`
  (a defect or divergence already recorded, with the
  `POSIX-COVERAGE.md` or `POSIX-GAP-ACCOUNTING.md` section it points
  at), or `unverified-<reason>`.
- `tools/libc-test.sh` — the driver.

### The rc contract, and how a partial run stays honest

This is the part that must not be got wrong. `tools/runtests.sh`
already defines the three outcomes (`tools/runtests.sh:155-176`): 0
pass, 77 *ran but declined to check something*, anything else fail. The
driver maps onto that, per test:

- **links, runs, matches `expected.txt`** → pass.
- **links, runs, does not match** → fail, and print the test's own
  output, which is already self-describing (`t_error` prints
  `file:line: what failed`).
- **does not link** → **77, never pass.** The reason is the exact
  first compiler error, which is always the missing header. A test
  blocked on `pthread.h` is not evidence of anything and must never be
  counted as evidence of correctness.
- **links but its helper is stubbed** (`malloc-oom`, `setenv-oom`) →
  **77**, reason `t_vmfill stubbed: needs sys/mman.h`.
- **aborts on `RtlCloneUserProcess`** → **77**, reason
  `fork unavailable under this wine`. Detected by grepping the output
  for the literal Wine string, not by guessing from the exit code. On
  the real-Windows CI job these same tests must run for real, so the
  77 has to be environment-derived, not baked into `expected.txt`.

The summary line then reads, e.g., `62 passed, 22 known, 5 unverified,
57 unbuildable` — and **`unbuildable` is printed even when it is the
largest number**, because "39% of this suite cannot be compiled against
this library" is the most useful single fact the stage produces. A run
where everything is `unverified` must exit non-zero, the way
`tools/runtests.sh` already refuses a run that launched nothing
(`tools/runtests.sh:180+`). That is measure M1 ("a floor on every stage
that reports a result", `test/verification-measures.md`) applied here:
the floor is *at least N tests must have actually executed*, pinned as a
number that only moves deliberately.

`expected.txt` must also fail **when a test starts passing** — an
`xfail` that goes green is a fixed defect and should force someone to
update the ledger, the same way `tools/lint-undefined.sh` treats a stale
exception.

### Where it runs

**In the gate.** 4 seconds of Wine at `-P4`, plus sub-second build.
`tools/gate.sh` runs stages concurrently and the budget is the slowest
stage (`tools/gate.sh:6-9`), which this is nowhere near. It belongs
beside `check-x86_64`, not after it.

The **math corpus** does not go in the gate on day one. It runs in 2 s,
so cost is not the reason — the reason is that 82 red lines nobody has
triaged is a stage that will be ignored, and an ignored stage is worse
than no stage. It goes in on demand (`tools/libc-test.sh math`) until
`expected.txt` covers it, then joins the gate.

**Real Windows.** CI's PowerShell loop should run the same driver, and
the `fork`-blocked tests will move from 77 to a real pass or a real
fail there. That is the same asymmetry `*-win.c` tests already have, and
it is the strongest single argument for deriving the 77s from the
environment at run time rather than from a static list.

## Reproducing these numbers

Base revisions: ntlibc `d36b07c`; LTP `4c0cfb8`; `libc-test` `68edb8b`
(`git://repo.or.cz/libc-test.git`, not the `jart` GitHub fork, which is
a different tree); pjdfstest `85a8aea`.

Compile line used for every external test, matching `Makefile:285` plus
the suite's own include dir:

```
x86_64-win32-tcc -std=c99 -nostdinc -fno-builtin -Wall \
  -Wno-unused-function -D_GNU_SOURCE \
  -I<tree>/arch/x86_64 -I<tree>/arch/generic \
  -I<tree>/obj/include -I<tree>/include -I<suite>/include \
  -nostdlib -o out.exe <tree>/lib/crt1.o <test>.c <harness>.c \
  -L<tree>/lib -lc -lntdll
```

Run line:

```
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d \
  timeout -k 2 15 wine out.exe </dev/null
```

Both overrides are mandatory, not cosmetic: without them a
`RtlCloneUserProcess` abort opens `winedbg --auto` on the developer's
desktop and blocks.
