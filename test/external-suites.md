<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# External conformance suites, measured against this tree

Whether an established POSIX test suite would tell us something
`test/POSIX-COVERAGE.md` and `test/POSIX-GAP-ACCOUNTING.md` do not, and
what hooking one in would cost.

Everything below was **measured**, not estimated. Method for every
number: the suite's own test sources were compiled and linked against a
fresh clone of `origin/main` at **`d36b07c`**, built `./configure
--host=x86_64-win32 CC=x86_64-win32-tcc && make -j8`, using the exact
flags of the `Makefile`'s `obj/test/%.exe` rule, and the resulting PEs
were run under stock apt Wine with `WINEDEBUG=-all
WINEDLLOVERRIDES=winedbg.exe=d`, stdin from `/dev/null`, `timeout -k 2`
per test. Base revisions of every external tree are quoted as SHAs, per
the standing rule about branch names in shared checkouts.

## The two questions, and why they have different answers

There are two entirely separate things an external suite can be for, and
conflating them produces the wrong verdict:

- a **correctness oracle** — does it catch defects in what we have?
- a **gap oracle** — does it independently measure what we *don't* have,
  and weight that absence by how much conformance surface it costs?

This document originally asked only the first question, using "would it
have caught each of the five defects our own audits fenced today?" as
the test. That is a fair correctness test and the Open POSIX Test Suite
scores **zero** on it. That finding is real and is kept below. But it is
not what OPTS is good for, and by the second question the same
measurements read completely differently: **the 1019 tests that fail to
compile are not the reason to reject OPTS, they are its output.**

So:

| suite | job | verdict |
|---|---|---|
| **Open POSIX Test Suite** | **gap oracle** | **adopt, as a generated report — not a pass/fail gate stage** |
| **musl `libc-test`** | **correctness oracle** | **adopt, as a gate stage** |
| LTP proper | either | unusable — framework needs `/proc`, root, mount, cgroups |
| VSX-PCTS / VSC-PCTS | correctness | unobtainable (free version 404s; current version needs a signed licence) |
| pjdfstest | correctness | unusable — needs root, perl, and a real POSIX shell |

They are not competitors and should not be ranked on one axis. OPTS
answers "how big is the hole, and which header is the biggest lever";
`libc-test` answers "is what we built right". Adopting one is not an
argument against the other.

## Contents

- [The two questions](#the-two-questions-and-why-they-have-different-answers)
- [The suites](#the-suites)
- [OPTS as a gap oracle](#opts-as-a-gap-oracle)
  - [The failure taxonomy is the deliverable](#the-failure-taxonomy-is-the-deliverable)
  - [The gap map, weighted by conformance surface](#the-gap-map-weighted-by-conformance-surface)
  - [Reconciliation against POSIX-GAP-ACCOUNTING.md](#reconciliation-against-posix-gap-accountingmd)
  - [Integration as a report, not a gate](#integration-as-a-report-not-a-gate)
  - [How the report stays honest](#how-the-report-stays-honest)
- [OPTS as a correctness oracle](#opts-as-a-correctness-oracle)
- [libc-test as a correctness oracle](#libc-test-as-a-correctness-oracle)
- [The build assumptions that break](#the-build-assumptions-that-break)
- [Cost](#cost)
- [Reproducing these numbers](#reproducing-these-numbers)

## The suites

### S1: Open POSIX Test Suite (in LTP)

Lives inside the Linux Test Project at
`testcases/open_posix_testsuite/`; measured at LTP **`4c0cfb8`**
(`https://github.com/linux-test-project/ltp`). Also mirrored standalone
at `https://sourceforge.net/projects/posixtest/`.

| | |
|---|---|
| licence | `testcases/open_posix_testsuite/COPYING` l.1-2: *"All sourcecode generated from scratch by Ngie Cooper is BSD 2-clause licensed. All legacy openposix test suite code is GPLv2+ licensed."* `README` §1: *"All code is distributed under the GNU General Public License v2."* |
| size | 14 MB; 1610 `.c` under `conformance/`, 15 under `functional/`, 50 under `stress/` |
| shape | **190** directories under `conformance/interfaces/` — **189 of them one per POSIX interface**, plus `testfrmw`, which is suite infrastructure — with an `assertions.xml` per directory tying each test file to a numbered assertion of that interface's specification page |
| harness | essentially none. `include/posixtest.h` is `PTS_*` return-code and attribute macros; `lib/common.c` is **12 lines** — a `main()` calling `test_main()`. Each test is one self-contained `.c`. |

The directory count above was **191** when this document was first
written, and is wrong: `ls conformance/interfaces` returns 191 entries
because one of them is a `Makefile`. The measured figure from
`find … -type d` is 190, of which `testfrmw` is one — so 189 interface
directories. The reconciliation table further down was always computed
from the interface directories and has always summed to 189, which is how
the discrepancy was caught; it is corrected here rather than in one place
so the document stops contradicting `tools/posix-gapmap.sh`, whose
`CENSUS_DIRS` invariant pins the same 190.

Two properties make it usable as a gap instrument specifically:

1. **The directory structure is itself an index of POSIX interfaces**,
   independently authored, with per-directory test counts that weight
   each interface by how much specification text it carries. That is a
   measure our ledger structurally cannot produce, because the ledger
   counts *interfaces* (1177 of them, each worth 1) while OPTS counts
   *assertions* (1610 tests, distributed by how much the standard
   actually says about each call).
2. **It already knows how to decline.** `README` §1 states the suite is
   written *"in a manner that is agnostic to any given implementation"*,
   and it backs that with `PTS_UNSUPPORTED`: tests gated on an option
   group compile to a `#else` branch that reports unsupported rather
   than failing. Measured here, **17 tests self-reported
   `PTS_UNSUPPORTED`** — every one of them because this library declines
   an option group on purpose (`_POSIX_SPORADIC_SERVER` in
   `sched_setparam`/`sched_setscheduler`/`sched_get_priority_*`,
   `_POSIX_CPUTIME` in `clock_getcpuclockid`). The suite got that right
   with no help from us. A gap report built on it inherits that.

What it covers, by directory: **95** `pthread_*`, 10 `mq_*`, 9 `sem_*`,
8 `sched_*`, 7 `aio_*`, 6 `clock_*`, 5 `timer_*`, 2 `shm_*`, the
`mmap`/`mlock` family, the signal family, and sixteen stragglers. It is
a POSIX.1b/POSIX.1c realtime-and-threads suite with a signal chapter
attached — which is to say, it is a **map of precisely the half of POSIX
this library does not have**. As a correctness oracle that is a
disqualification. As a gap oracle it is the point.

### S2: LTP proper

Same clone, `4c0cfb8`, GPLv2. 385 directories under
`testcases/kernel/syscalls/`, 1396 `.c`, of which **1122 include
`tst_test.h`** (`include/tst_test.h` 849 lines, `lib/tst_test.c` 2089).

The framework is not portable and is not trying to be: `lib/*.c` reads
**19 distinct `/proc` paths** by literal string (`/proc/self/mounts`,
`/proc/self/maps`, `/proc/cpuinfo`, `/proc/sys/kernel/pid_max`,
`/proc/self/oom_score_adj`, `/proc/sys/kernel/tainted`,
`/proc/self/uid_map`, …), includes `<sys/mount.h>`, forks in 12 places,
and wants loop devices, cgroups and root. It is also a *kernel* suite:
roughly a third of `testcases/kernel/syscalls/` is Linux-only calls
(`bpf`, `add_key`, `arch_prctl`, `clone3`, `close_range`, `cachestat`)
with no POSIX counterpart. Porting the framework is not smaller than
writing the tests.

### S3: The Open Group VSX-PCTS / VSC-PCTS

The Open Group's index (`https://posix.opengroup.org/testsuites.html`,
"Current Test Suites") lists VSX-PCTS2016 v1.15 and VSC-PCTS2016 v3.1
plus the 2003 pair, each gated on a PDF **"Time-Limited License
Agreement"**, with *"a twelve month free license"* for open source
implementations, obtained by applying. Realtime suites (VSPSE54-2003,
VSPSE52-2003) require contacting `obconformance@opengroup.org`.

The obsolete **VSX-PCTS 4.4.4** (Nov 1999) *is* free software:
`https://pubs.opengroup.org/onlinepubs/063392154/Licence.html` says
*"This program is free software; you can redistribute it and/or modify
it under the terms of the 'The Open Group Test Suite License'"* —
OGTSL, OSI-approved — with a preamble requiring **the original test
modes be preserved** in modified versions. Its FAQ
(`https://www.opengroup.org/testing/downloads/vsx-pcts-faq.html`) claims
*"over 6,000 tests for IEEE Standard POSIX 1003.1-1990"* and *"uses the
industry standard Test Environment Toolkit (TET) as its harness."*

All three tarballs the download page still links —
`vsx-pcts-4.4.4.tar.Z` (4 MB), `vsxgen-os-1.4A.tar.Z`,
`vsx-vtools-1.4.tar.Z` — **return HTTP 404** as of 2026-08-24 from both
`pubs.opengroup.org` and `www.opengroup.org/testing/downloads/`, and the
Wayback availability API reports **no archived snapshot** of any of
them. Only `README` and `Licence.html` still resolve.

Even given a copy: POSIX.1-**1990** is two revisions behind the ledger's
baseline, the TET harness is a real port (unlike OPTS), and OGTSL's
preserve-the-test-modes clause is a redistribution constraint against a
GPL-3.0-or-later tree. **Rejected on obtainability**, not on merit.
Worth re-checking annually — a 2016-vintage suite obtained under the OSS
licence would be a genuinely better gap oracle than OPTS, because it
covers the Shell and Utilities volume too.

### S4: musl `libc-test`

Upstream `git://repo.or.cz/libc-test.git` at **`68edb8b`** (not the
`jart` GitHub fork, which is a different tree). `README`: *"libc-test is
developed as part of the musl project."*

| | |
|---|---|
| licence | **MIT** (`COPYRIGHT`) — the only candidate with no copyleft friction |
| size | 14 MB; `src/functional` 77 `.c`, `src/regression` 69, `src/math` 206, `src/api` 79 (compile-only declaration checks), `src/common` 10 helpers |
| harness | `src/common/test.h` + `print.c`; one test per file, own `main`, `t_error()` prints `file:line: what failed` |

It is **not** a clause-traceable conformance suite — there is no
assertion-to-specification-line mapping, which is exactly what
`test/POSIX-COVERAGE.md` provides. What it is instead is a **regression
corpus**: `src/regression/` is 69 files each named for a specific bug
(`lrand48-signextend`, `ftello-unflushed-append`, `printf-fmt-g-round`,
`memmem-oob-read`, `inet_pton-empty-last-field`), each carrying the musl
commit that fixed it. Complementary evidence to a clause audit: the
ledger proves the specification was read; this proves the mistakes other
libcs actually made were checked for.

Its `src/api/` subtree deserves a separate note — 79 files that only
*compile*, checking that each header declares what POSIX says it
declares with the right types. That is a third kind of oracle again
(a declaration oracle), and it is the natural companion to
`test/POSIX-HEADER-INVENTORY.md`, which `POSIX-GAP-ACCOUNTING.md`
already flags as materially stale. Not evaluated in depth here; flagged
as the obvious follow-up.

### S5: pjdfstest

`https://github.com/pjd/pjdfstest` at **`85a8aea`**, 1.5 MB, BSD,
actively maintained. 238 `.t` files over `chmod`, `chown`, `link`,
`mkdir`, `open`, `rename`, `unlink`, `utimensat` — genuinely the
filesystem corner this tree cares about, and unusable for structural
reasons: `README` requires *"You must be root"*, *"perl"* and
*"TAP-Harness"*; every test is a POSIX shell script (`tests/unlink/01.t`
uses backtick command substitution, `dirname $0`, dot-sourcing and shell
functions by line 10, and `tests/misc.sh` branches on `$(id -u)`);
and the assertions are about POSIX uid/gid/sticky-bit semantics the NT
security model does not present through this library. Recorded so nobody
re-investigates it.

## OPTS as a gap oracle

### The failure taxonomy is the deliverable

All 1610 conformance tests compiled and linked against `lib/libc.a`.
591 succeeded. The interesting part is the 1019 that did not, and they
are **not one bucket** — they are three, and the three mean very
different things:

| class | tests | what it means |
|---|---|---|
| **A. header absent entirely** | 873 | `#include` fails. The honest failure: we do not claim the interface and a portable program finds out at compile time. |
| **B. header present, interface missing** | 146 | The `#include` *succeeds*. The call falls through to an implicit declaration and dies at link, or the type it needs is incomplete. **This is the worst failure mode of the three for a downstream consumer**, and it is the one our ledger is least able to see, because at interface granularity these look present. |
| **C. compiles and links** | 591 | usable as a correctness oracle; see below |

Class B breaks down further, and every line of it is a finding:

| B sub-class | tests | detail |
|---|---|---|
| unresolved at link | 94 | `timer_create` 49, `sigqueue` 15, `sigignore` 6, `sigwaitinfo` 5, `sigtimedwait` 5, `sched_getscheduler` 5, and one each of `timer_settime`, `timer_gettime`, `timer_getoverrun`, `timer_delete`, `sched_rr_get_interval`, `sched_get_priority_min`, `sched_get_priority_max`, `sched_getparam`, `pthread_sigmask` |
| incomplete type | 27 | `struct sched_param` — all in `sched_getparam`, `sched_setparam`, `sched_getscheduler`, `sched_rr_get_interval` |
| undeclared identifier | 21 | `SCHED_FIFO` 5, `SIG_HOLD` 3, `_SC_CPUTIME` 3, `_SC_THREAD_CPUTIME` 2, `_SC_MONOTONIC_CLOCK` 2, `SCHED_RR` 2, `SCHED_OTHER` 2, `_SC_SIGQUEUE_MAX` 1, `_SC_ASYNCHRONOUS_IO` 1 |
| `#error` from the suite | 3 | `_POSIX_SPORADIC_SERVER` not defined — the suite's own option-group probe firing |
| suite-side | 1 | `testfrmw/threads_scenarii.c` needs `pthread_attr_t` |

Read that as a work list and it is immediately actionable in a way the
ledger's prose is not. Three examples:

- **`sigignore` (6 tests).** The one hole in an otherwise-complete XSI
  signal family: `sighold`, `sigrelse`, `sigset` and `sigpause` all
  exist in `include/signal.h`, `sigignore` does not. The ledger already
  says this at `POSIX-GAP-ACCOUNTING.md:599` — *"one line over
  `sigaction`; obsolescent, and its four siblings … are already
  present"* — but says it as one row among 473. OPTS attaches a price
  to it: six tests, and a `signal.h` that is one line from complete for
  that family.
- **`SIG_HOLD` (3 tests), `_SC_CPUTIME`/`_SC_MONOTONIC_CLOCK`/
  `_SC_SIGQUEUE_MAX`/`_SC_ASYNCHRONOUS_IO` (7 tests).** Missing
  *macros*, not functions. The ledger is a function-interface ledger by
  construction; it has no row shape for a missing `sysconf` name or a
  missing `signal.h` constant. This class is invisible to it.
- **The `timer_*` family (54 tests).** Not declared anywhere, so the
  tests reach link via implicit declaration. That the failure surfaces
  at link rather than at `#include` is a fact about `time.h` being
  present-but-partial, and it is exactly the shape a configure probe
  gets wrong.

### The gap map, weighted by conformance surface

First-error-only counts are biased by include order, so the map below
parses each blocked test's full `#include` set (resolving the suite's
own headers one level) and asks the decision-useful question: **if we
added header X, how many tests stop being blocked?**

Tests naming each header ntlibc lacks (a test may name several):

| header | tests naming it | unblocked by that header **alone** |
|---|---|---|
| `pthread.h` | 524 | **437** |
| `mqueue.h` | 128 | 125 |
| `sys/mman.h` | 121 | 89 |
| `semaphore.h` | 110 | 52 |
| `aio.h` | 71 | 71 |
| `langinfo.h` | 2 | 2 |
| `nl_types.h`, `sys/ipc.h`, `sys/shm.h`, `mntent.h`, `sys/vfs.h`, `sys/mount.h`, `sys/sysinfo.h`, `sys/pstat.h`, `sys/sysctl.h` | 1-4 each | 0-3 |

Greedy closure — add headers best-first, and watch the residue:

```
+pthread.h     unblocks 437    still blocked: 434
+mqueue.h      unblocks 127    still blocked: 307
+sys/mman.h    unblocks 113    still blocked: 194
+semaphore.h   unblocks 107    still blocked:  87
+aio.h         unblocks  71    still blocked:  16
```

`tools/posix-gapmap.sh` implements this closure and reports slightly
different figures — 445 / 127 / 113 / 109 / 71, over **875** rather than
871 header-blocked tests, leaving a residue of 10:

```
+pthread.h     unblocks 445    still blocked: 430
+mqueue.h      unblocks 127    still blocked: 303
+sys/mman.h    unblocks 113    still blocked: 190
+semaphore.h   unblocks 109    still blocked:  81
+aio.h         unblocks  71    still blocked:  10
```

**Both sets of numbers are stated rather than one silently replacing the
other, because the conclusions are identical and the difference is
heuristic, not a correction.** The two differ only in how an `#include`
set is resolved — the *compiler's* verdict is identical to the test
(873/146/591, and the class B sub-classes 94/27/21/3/1, reproduce exactly)
— and the block above is the reproducible one, since a checked-in script
produced it and `--check` re-derives it on every push, whereas the table
above came from a one-off probe whose resolver's exact conditional and
recursion handling was not recorded. Ranking, magnitudes and the
five-header conclusion are the same either way; the tool's figures put
five headers at 865 of 1019 (85%) instead of 855 (84%).

**Five headers account for the great majority of the 1019 blocked tests
— 855 (84%) by the original probe, 865 (85%) by `tools/posix-gapmap.sh`.**
That
is the single most decision-useful number in this document, and it is
not derivable from `POSIX-GAP-ACCOUNTING.md`, which reports the same
five clusters as *102 + 10 + 14 + 9 + 8 = 143 interfaces* — a ranking
that puts `sys/mman.h` (14 interfaces) below `pthread.h` (102) by a
factor of 7, where the conformance-surface weighting puts it below by a
factor of 4, and puts `aio.h` (8 interfaces, 71 tests) *above*
`sys/mman.h` in tests-per-interface by an order of magnitude. Counting
interfaces and counting assertions give materially different priority
orders, and only the second one reflects how much the standard actually
says.

Caveat, stated because the number would otherwise be over-read: a header
appearing does not mean those tests would then *pass*, or even link —
they would still need the functions behind it. The closure measures
**how much of the suite becomes reachable**, which is the right question
for prioritising a header, and the wrong one for predicting a pass rate.

### Reconciliation against `POSIX-GAP-ACCOUNTING.md`

The independent check the ledger cannot perform on itself. Each of the
189 interface directories (excluding `testfrmw`, which is suite
infrastructure) was classified from the **build artifacts**, not from
the ledger's prose: *declared* = a prototype in `include/` or
`obj/include/` with comments stripped; *defined* = a `T`/`D`/`B`/`R`/`W`
symbol in `nm -g --defined-only lib/libc.a` (913 symbols).

| | dirs | tests |
|---|---|---|
| declared **and** defined | 43 | 705 |
| declared, **not** defined | 3 | 26 |
| defined but not declared | **0** | 0 |
| neither | 143 | 877 |

**The disagreement count is zero.** Every one of the three
declared-but-undefined interfaces — `sigqueue`, `sigwaitinfo`,
`sigtimedwait` — carries an `undefined-ok:` marker in
`include/signal.h`, which is precisely the ledger's *declared but
deliberately unimplemented* bucket. OPTS finds those three and no
others among the interfaces it covers. Nothing OPTS can reach is
present that the ledger calls absent; nothing is absent that the ledger
calls present.

That is a null result, and it is worth having. It is the first
externally-authored check on a 1177-row classification that was built by
reading the specification, and it says the classification survives
contact with an independent index over the 189 interfaces where the two
overlap. Recording a null result is the point of running the check;
had it come out any other way it would have been the most important
finding in this document.

Two second-order observations from the same pass, both about the
*measurement* rather than the tree:

- Six directories the tree classifies as **absent** nonetheless produce
  a linking executable: `sched_get_priority_max`,
  `sched_get_priority_min`, `sched_setparam`, `sched_setscheduler`,
  `timer_create`, `timer_getoverrun`. These are the option-group-gated
  tests — e.g. `sched_get_priority_max/1-3.c` compiles its `#else`
  branch and reports `PTS_UNSUPPORTED` because
  `_POSIX_SPORADIC_SERVER` is undefined. Correct behaviour, but it
  means **"links" is not a synonym for "the interface exists"**, and a
  report that conflated the two would overstate coverage.
- Four directories the tree classifies as **present** have zero linking
  tests: `access`, `fsync`, `getpid`, `sigpause`. `access` has no test
  files at all (only `assertions.xml`); the other three are blocked by
  `pthread.h` — which the tests include for reasons unrelated to their
  subject. So **"blocked" is not a synonym for "the interface is
  missing"** either. Both directions must be reported separately or the
  report lies in both directions at once.

### Integration as a report, not a gate

A gate stage answers yes/no. This instrument does not produce a yes/no
answer — its output is a distribution — so making it a gate would force
a threshold nobody can justify, and a threshold nobody can justify is
the "number nobody reads" failure mode by another route.

Proposal: `tools/posix-gapmap.sh` produces
**`test/POSIX-GAP-MAP.generated.md`**, checked in, regenerated on
demand and in a nightly job, with `tools/gate.sh` carrying only a cheap
*staleness* stage (below).

The generated file has four sections, in this order:

1. **Header lever table** — the greedy-closure table above. One row per
   absent header: tests naming it, tests unblocked by it alone, tests
   unblocked at its position in the closure, residue after. This is the
   section that answers "what do we build next", and it is the reason
   the report is worth generating at all.
2. **Class B: declared but not provided** — the 146, itemised by symbol
   and by missing macro, with each row annotated `undefined-ok` /
   `not-marked`. A `not-marked` row is a bug in either the tree or
   `tools/lint-undefined.sh`'s exception list, and it is the single
   highest-signal line the report can print.
3. **Reconciliation delta** — the four-cell table above, plus the two
   "links ≠ exists" / "blocked ≠ missing" exception lists. **Empty is
   the expected state and must be printed as an explicit
   `0 disagreements` line**, never as an absent section.
4. **Per-directory ledger** — 190 rows, `interface | tests | class |
   first blocking header or symbol`, so the file diffs meaningfully.
   This is the raw material; it goes last because nobody reads it
   directly, and it is what makes the diff of a regeneration reviewable.

Why checked in rather than produced as CI output: the value is in the
**diff**. `git diff test/POSIX-GAP-MAP.generated.md` after landing
`pthread.h` would show 437 tests move from blocked to attempted, and
that diff is a better changelog entry than any prose. It also means the
report cannot rot silently — a stale checked-in file is visible; a
CI artifact nobody downloaded is not.

Where it runs: **nightly, plus a `--check` mode in the gate.** The
generation itself costs almost nothing — all 1610 tests build from clean
in **0.78 s** at `-P8`, because `x86_64-win32-tcc` compiles and links
one of these in 6 ms — so cost is not the constraint. Keeping it *out*
of the gate is a deliberate choice about what a gate stage means, not a
performance decision.

### How the report stays honest

This is the part that matters, because a gap report has exactly the
failure mode this project spent a day removing: **if the measurement
silently stops working, the gap appears to close.** A driver that
mis-resolves the vendored path, or an `-I` that stops pointing at the
suite, produces "0 blocked tests" — indistinguishable from success and
strictly more dangerous, because it is *good news*.

Four invariants, all cheap, all checked by the `--check` stage in the
gate, any of which failing is a hard error rather than a warning:

1. **Census.** The number of `.c` files discovered under
   `conformance/interfaces/` must equal a pinned constant (1610) and the
   number of directories must equal 189 + `testfrmw` = 190. If the
   submodule moves or the glob breaks, this fires before anything else
   reports a number. Bumping the constant is a deliberate commit,
   reviewed alongside the suite SHA — same discipline as
   `test/verification-measures.md`'s M6 ("pin the check *list*, not just
   the tool version").
2. **Partition.** `A + B + C` must equal the census exactly. A test that
   fell out of classification is a bug in the classifier, not an
   absence in the library, and silently dropping it shrinks the gap.
3. **Floors, both directions.** `C` (links) must be **≥** a pinned floor
   and `A + B` (blocked) must be **≥** a pinned floor too. The first
   catches "the compiler stopped finding `libc.a`" — everything blocked,
   which would look like a catastrophe and so is self-announcing. The
   second catches the dangerous one: "the compiler stopped finding the
   *suite*", or an `-I` accidentally pointing at a host libc, which
   makes everything link and the gap read as zero. This is
   `test/verification-measures.md`'s M1 (a floor on every stage that
   reports a result) applied in the direction that is easy to forget.
   The lower floor moves **down** only in a commit that also shows the
   header or symbol that closed the gap.
4. **Positive control.** One canary test known to be blocked for a
   reason that will not be fixed (a `#include <pthread.h>` test) must
   still be classified `A`, and one known-linking test (`strlen/1-1.c`)
   must still be classified `C`. If both canaries agree with the pinned
   expectation, the classifier is discriminating rather than answering
   constantly. This is the specific check that distinguishes "the gap
   closed" from "the measurement stopped working": a closed gap moves
   the population, a broken measurement moves the canaries too.

And one process invariant, because tooling cannot enforce it: the
report's header must state the ntlibc SHA, the LTP SHA, and the date it
was generated, and the `--check` stage fails if the recorded ntlibc SHA
is not an ancestor of `HEAD`. That is what stops the file being quietly
months old while looking authoritative.

## OPTS as a correctness oracle

Kept because it is a real finding, and because it is the reason OPTS
should be a report rather than a test stage: **as a correctness oracle
it is close to worthless here**, and wiring it up as a pass/fail suite
would produce a large, noisy, permanently-red stage that measures Wine
and documented stubs.

Of the 591 that link, run under Wine: 357 `PTS_PASS`, 176 `PTS_FAIL`,
17 `PTS_UNSUPPORTED`, 8 `PTS_UNRESOLVED`, 6 `PTS_UNTESTED`, 26
`exit(-1)`, 1 timeout. Note **420 of the 591 are the `sigaction/`
directory alone** — 501 files for 25 numbered assertions, mostly one
test repeated per signal — so file counts badly overstate distinct
behaviours checked.

Of the 176 failures, **134 are one thing**: `wine: Call from … to
unimplemented function ntdll.dll.RtlCloneUserProcess, aborting`. They
are `fork()`-based, and note they **abort rather than hang**, at ~0.3 s
each — but only with `WINEDLLOVERRIDES=winedbg.exe=d` set. Without it,
Wine launches `winedbg --auto` and *that* is where the hang comes from.

The remaining 42, plus the 26 `exit(-1)`s:

| what | tests | already in the ledger? |
|---|---|---|
| `SA_ONSTACK` / alternate stack not honoured (`sigaltstack` returns 0 doing nothing, `src/signal/signal.c:306`) | 26 + 7 | **yes** — `POSIX-GAP-ACCOUNTING.md:439` records the deliberate stub; `POSIX-COVERAGE.md:602` marks it N/A |
| `nanosleep`/`clock_nanosleep` return before the interval is observable on the clock | 10 + 9 | **no** — but the tests request 3 ns and check the clock advanced; POSIX permits `_POSIX_CLOCKRES_MIN` of 20 ms, so this is the suite being stricter than the standard |
| `sigsuspend`, `sigwait`, `sigrelse`, `sighold` | 9 | **yes** — `sigwait` is a documented permanent stub (`include/signal.h:223-225`); the rest follow from the no-asynchronous-delivery model that header states |
| `kill`/`killpg` to something other than self | 5 | **yes** — `include/signal.h`: *"kill() can only end a process, not interrupt it"* |
| `raise` | 1 | worth one look |

Net contribution over the ledger, as a correctness instrument: **one
open question** (probably a non-finding) and confirmation that four
documented stubs are stubs.

### The five fenced defects

The original sharp test, kept intact. Only two of the five are fenced at
`d36b07c` (`unlinkat` at `test/posix-unistd.c:1041`; `newlocale` visible
directly at `src/misc/locale.c:58` as `(void)mask;`); the others are
presumably in flight elsewhere. Whether a suite *contains such a test*
is answerable regardless.

| fenced defect | OPTS | LTP proper | `libc-test` |
|---|---|---|---|
| `newlocale` ignores `category_mask`; invalid mask returns success not `[EINVAL]` | no — no `newlocale` directory, zero references in 1876 `.c` | no | **no** — `newlocale` appears only as a *helper* in `src/regression/uselocale-0.c` and in declaration-only `src/api/locale.c` |
| `snprintf` does not fail `[EOVERFLOW]` for `n > INT_MAX` | no (14 files *call* `snprintf`; none test it) | no | **partial, and it passes** — `src/functional/snprintf.c:171-176` tests the *output-length* `EOVERFLOW` path (`snprintf(NULL,0,"%.*u ",2147483647,0)` → -1). ntlibc passes. The `n` parameter is untested. |
| `uselocale` never returns `LC_GLOBAL_LOCALE` | no | no | **no** — `uselocale-0.c` only checks that `uselocale(0)` returns the previously-set locale and does not change it; ntlibc passes |
| `regexec()` unbounded recursion on `(a*)*b` | no | no | **no** — six regex regressions, none about nested-quantifier blowup; grepping the tree for `(a*)*` finds nothing |
| `unlinkat` masks undefined flag bits and deletes the file | no | **yes** | **no** — `unlinkat` appears only in `src/api/unistd.c` |

The one hit: `testcases/kernel/syscalls/unlinkat/unlinkat01.c` at LTP
`4c0cfb8` is table-driven and its fifth case is, verbatim,
`{0, testfile, 9999, EINVAL}` — fd, filename, flag, expected errno. An
implementation that masks the undefined bits out of `9999` and unlinks
the file fails that row. **One of five, in the one suite whose framework
cannot be brought up here.**

This is the finding that motivated the original "reject OPTS" verdict,
and it is why the verdict has to be qualified rather than kept: a score
of zero on a correctness test is not evidence about an instrument's
value as a gap measure. The two are unrelated axes, and OPTS is strong
on the second precisely *because* it is weak on the first — its subject
matter is the interfaces we do not have.

### Follow-up: the sweep above is now run in CI, and recorded per test

The distribution in this section came from **one** manual sweep. It was
never in CI, never recorded per test, and therefore could not be
compared against anything — so a test that started failing had nothing
to fail *against*. `tools/posix-optsrun.sh` and
`test/POSIX-OPTS-RUN.generated.md` close that: the same 591 tests run
on every push, each verdict recorded by name, the report checked in so
the diff is the signal, and the gate firing on **regression** — a test
moving `PASS` to anything else — rather than on any count. The "no
threshold on a distribution" argument this document makes is untouched
by that, because a per-test regression is not a threshold.

Two things the repeated sweep sees that a single sweep structurally
could not:

- **Intermittence.** Running each test three times found members of the
  `nanosleep`/`clock_nanosleep` family above sitting *on* the
  observability boundary rather than past it — passing about half the
  time. A one-shot sweep records whichever side of the coin it landed
  on. They are recorded `FLAKY`, a not-`PASS` outcome, in a bucket with
  a pinned ceiling so it cannot become a place to put failures.
- **That the `fork` problem is hidden behind the header gap.** The 134
  `RtlCloneUserProcess` aborts above are the ones that *reach*
  execution; almost every other `fork`-dependent test in the suite also
  pulls `pthread.h`, `mqueue.h`, `sys/mman.h` or `semaphore.h` and is
  therefore class A, never built, never run. Which means relocating a
  "fork-dependent subset" to the real-Windows `windows-test` leg —
  where `fork` works — would be infrastructure for almost no
  population. It is deliberately not done.

## `libc-test` as a correctness oracle

Unchanged by the reframing, and it is the suite that does this job.
Same method, same tree:

| corpus | files | link | pass | fail |
|---|---|---|---|---|
| `src/functional` + `src/regression` | 146 | **89 (61%)** | 62 | 27 |
| `src/math` (top level) | 199 | **174 (87%)** | 92 | 82 |

Linking needed one thing OPTS did not: five helpers (`t_vmfill`,
`t_memfill`, `t_setrlim`, `t_fdfill`, `t_setutf8`) whose real
implementations include `<sys/mman.h>` and `<langinfo.h>`. Stubbing them
to return failure — six lines — took the link rate from 0/146 to 89/146.
Absent headers still block 57: `pthread.h` 23, `langinfo.h` 3,
`semaphore.h`/`resolv.h`/`iconv.h` 2 each, one apiece for `tgmath.h`,
`sys/syscall.h`, `sys/shm.h`, `sys/sem.h`, `sys/msg.h`, `sys/mman.h`,
`spawn.h`, `mntent.h`, `crypt.h`.

Of the 27 failures, 5 are environment (`fcntl`, `vfork`, `fflush-exit`
abort on `RtlCloneUserProcess`; `execle-env` gets `ECHILD`; `popen` has
no shell to run `read a ; test "x$a" = xhello`). The other **22 are
behavioural**, and several land squarely in ledger blind spots:

| test | what it reports | ledger status |
|---|---|---|
| `functional/fnmatch` | 5 failures: `[[?*\]` vs `\`, `[/b` literal, `[![:d-d]` malformed-class under `FNM_PATHNAME` | `fnmatch` is **implemented, not clause-audited** (`POSIX-GAP-ACCOUNTING.md:264`) — no assertion exists |
| `functional/inet_pton` + 2 regressions | 9 failures: `inet_pton(AF_INET,"1.2.03.4")` accepts a leading zero; `inet_addr("1.2. 3.4")` accepts an embedded space; `inet_pton(AF_INET6,"::")` fails; `1:2:3:4:5:6:7::` mis-parsed; `inet_ntop` v4-mapped | `arpa/inet.h` is **implemented, not clause-audited** (`:242`) |
| `functional/memstream` | `fseek(f,6,SEEK_CUR)` past end of an `open_memstream` buffer, then `ftell` gives 11 vs 5; content `h104o` vs `hello104` | mentioned twice, no clause row |
| `functional/random` | `setstate()` does not restore the sequence — 6 mismatches | row exists (`POSIX-COVERAGE.md:227`) but asserts only the **return value** |
| `regression/lrand48-signextend` | from a known seed: 366850414 / 1610402240 / 206956554, want 0 / 2116118 / 89401895 | row (`:221`) asserts only the `[0,2^31)` range; the expected values are fixed by POSIX's specified 48-bit LCG, so this is a real divergence |
| `functional/strftime` | `%03C`, `%+3C`, `%01C`, `%012F`, `%+10F` emitted literally; `%c` for year 10009 omits the `+` | row (`:286-287`) covers `%U %W %V %G %g` pass-through, says nothing about flags or width |
| `functional/strptime` | `%C` alone yields 1800 not 1856; `%s` unparsed | not covered |
| `functional/sscanf` | `%8c%8c` on 13 bytes returns 2 fields, expected 1 | not covered |
| `functional/wordexp` | 6 field-splitting failures: `$FOO` where `FOO="bar baz"` yields one word | **overlaps existing fenced work** at `test/posix-glob.c:1226,1271` |
| `functional/utime` | `utimensat(AT_FDCWD,"/dev/null/invalid",…,0)` gives `ENOENT`, POSIX wants `ENOTDIR` | not covered |
| `functional/setjmp` | `siglongjmp` does not restore the signal mask | mentioned twice; this behaviour untested |
| `regression/ftello-unflushed-append` | `ftello` on an append stream before flush: 3, want 7 | `ftello` rows exist; not this case |
| `regression/regex-escaped-high-byte` | `regcomp("\\\xfc")` returns 0, want `REG_BADPAT` | `REG_BADPAT` appears **nowhere** in either ledger or any `test/*.c` |
| `regression/statvfs` | `/` reports 0 file nodes (`f_files`) | mentioned; assertion is weaker |
| `regression/sigprocmask-internal` | `sigaddset(&s,32..34)` accepts signals musl reserves | design difference (`__libc_current_sigrtmin()` returns 35) — a divergence to document, not a defect |
| `regression/rlimit-open-files` | `setrlimit(RLIMIT_NOFILE,42)` returns `EINVAL`; limit not enforced | needs triage |
| `regression/printf-fmt-n` | `%n` mismatch | needs triage; reported values print identically |
| `regression/malloc-oom`, `setenv-oom` | depend on the stubbed `t_vmfill` | **must report unverified**, not failed |

**Math.** 82 of 174 fail, and this corpus needs care: musl's math tests
are deliberately stricter than the standard (crlibm reference values,
per-rounding-mode ULP bounds) and mainstream libcs fail a fraction too.
Triaged, three classes are unambiguous:

- **spurious exception flags** — `logb(±inf)` and `exp(inf)` raise
  `FE_DIVBYZERO`; `pow` raises it on ordinary finite arguments
  (`pow(0x1.cfdd8p+17, 3)`). POSIX specifies no exception for any.
  Nothing in `test/posix-math.c` checks the *absence* of a flag.
- **`fma()` is not fused** — `src/math/sanity/fma.h` cases fail at
  0.5-0.8 ULP; a correct `fma` is exact, 0 ULP by definition. A real
  defect, and one a clause audit is unlikely to phrase as an assertion,
  because "computed as if to infinite precision" does not look like a
  testable predicate until someone hands you the vectors.
- **`isless`/`islessequal`/`islessgreater`/`isunordered` raise
  `FE_INVALID` on NaN** — specified not to.

The rest (large-argument `sin`/`cos` reduction, subnormal `UNDERFLOW`
signalling in `nextafter`, long-double behaviour) is a triage backlog,
not a gate.

### Integration as a gate stage

Runs in **4 seconds** under Wine at `-P4`, plus sub-second build;
`tools/gate.sh` budgets on the slowest stage, so it belongs beside
`check-x86_64`.

- `third_party/libc-test/` vendored at a pinned SHA, upstream copyright,
  `MIT` SPDX, `LICENSES/MIT.txt` + `.reuse/dep5` stanza (upstream ships
  `COPYRIGHT`, not per-file headers).
- `test/libc-test-shim.c` — ours, GPL-3.0-or-later, the five helpers.
- `test/libc-test-expected.txt` — one line per test:
  `name  status  reason`, status ∈ `pass`, `xfail-<ledger-ref>`,
  `unverified-<reason>`.

Mapping onto `tools/runtests.sh`'s existing three outcomes
(`tools/runtests.sh:155-176`):

- links, runs, matches expected → **pass**
- links, runs, does not match → **fail**, printing the test's own
  self-describing output
- **does not link → 77, never pass.** A test blocked on `pthread.h` is
  not evidence of correctness.
- links but its helper is stubbed (`malloc-oom`, `setenv-oom`) → **77**,
  reason `t_vmfill stubbed: needs sys/mman.h`
- output contains Wine's literal `RtlCloneUserProcess` string → **77**,
  reason `fork unavailable under this wine`. Detected at run time from
  the output, not baked into `expected.txt`, because the same tests must
  run for real on the Server 2025 CI job.

Summary reads e.g. `62 passed, 22 known, 5 unverified, 57 unbuildable`,
and **`unbuildable` prints even when it is the largest number**. An
all-unverified run exits non-zero, the way `tools/runtests.sh` already
refuses a run that launched nothing. `expected.txt` must also fail
**when an `xfail` starts passing** — a fixed defect should force a
ledger update, the way `tools/lint-undefined.sh` treats a stale
exception.

The **math corpus does not go in the gate on day one** — not for cost
(2 s) but because 82 untriaged red lines is a stage that will be
ignored, and an ignored stage is worse than none. On demand
(`tools/libc-test.sh math`) until `expected.txt` covers it.

## The build assumptions that break

Concretely, with what each blocks.

1. **Absent headers.** OPTS 873/1610 (54%); `libc-test`
   functional+regression 57/146 (39%). Not a porting problem — it is
   the gap accounting restated as a build error, and under the gap-oracle
   reading it is the product, not the obstacle.
2. **Headers present but incomplete.** OPTS 146. The failure surfaces at
   *link*, not at `#include`, which is the shape a configure probe gets
   wrong. Invisible at interface granularity.
3. **`fork()` under stock apt Wine.** 158 of the 591 linkable OPTS tests
   call `fork()`; 134 aborted on `RtlCloneUserProcess`, as did 3
   `libc-test` tests. Measured behaviour is **abort, not hang**, at
   ~0.3 s each — but only with `WINEDLLOVERRIDES=winedbg.exe=d`
   exported. Without it, `winedbg --auto` opens on the developer's
   desktop and blocks. Export it, do not pass it per-command.
4. **No `/bin/sh` on the target.** Blocks `libc-test`'s `popen` (2
   files) and all 238 pjdfstest scripts. `sh/main.c` cannot close the
   pjdfstest gap: it needs command substitution, `case` and functions on
   line 10 of its first test.
5. **`t_vmfill`/`t_setrlim`.** Stubbing unblocks the `libc-test` link
   but makes `malloc-oom` and `setenv-oom` structurally unable to check
   anything. They must report *unverified*.
6. **`langinfo.h` in a shared helper.** `libc-test`'s
   `src/common/utf8.c` includes it, so it must be dropped from the
   helper set; nothing in the linkable corpus needed it.
7. **LTP's framework.** `/proc` in 19 places, `sys/mount.h`, root, loop
   devices, cgroups. Blocks 1122 of 1396 syscall tests outright.

Not an obstacle, and worth recording because it was the expected one:
**the compiler.** `x86_64-win32-tcc` compiles and links one of these
tests in **6 ms**; all 1610 OPTS tests build from clean in **0.78 s**
wall at `-P8`. Build cost is a factor in none of the decisions above.

## Cost

| item | cost |
|---|---|
| **OPTS gap oracle** | |
| vendor OPTS at a pinned SHA (GPLv2+, compatible with this tree; REUSE stanza needed) | 2 h |
| `tools/posix-gapmap.sh` — classifier, closure computation, four-section emitter | 1 day |
| the four honesty invariants + `--check` stage | 3 h |
| first review of the generated report (the `not-marked` Class B rows are the part that needs a human) | 0.5 day |
| **`libc-test` correctness oracle** | |
| vendor at a pinned SHA (MIT, no friction) | 1 h |
| helper shim + drop `utf8.c` | 30 min, already prototyped |
| driver reusing `tools/runtests.sh`'s rc contract | 2 h |
| **triage of the 22 behavioural failures** into defect / documented divergence / suite-stricter-than-POSIX | **1-2 days** |
| math corpus triage (82) | a further 1-2 days, later, separately |
| gate wiring | 1 h |

The triage line is the honest cost of the correctness oracle, and the
report-review line is the honest cost of the gap oracle. Both are the
part that cannot be skipped: wiring either up is an afternoon; deciding
what its output *means* is the work, and skipping it produces exactly
the failure this project spent today fixing — a stage that is green, or
a report that is tidy, because nobody looked.

## Reproducing these numbers

Base revisions: ntlibc `d36b07c`; LTP `4c0cfb8`; `libc-test` `68edb8b`
(`git://repo.or.cz/libc-test.git`); pjdfstest `85a8aea`.

Compile line for every external test, matching `Makefile:285` plus the
suite's own include dir:

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
`RtlCloneUserProcess` abort opens `winedbg --auto` and blocks.

The reconciliation's *defined* set came from
`nm -g --defined-only lib/libc.a`, filtered to `T`/`D`/`B`/`R`/`W`
(913 symbols); the *declared* set from every `.h` under `include/` and
`obj/include/` with `/* */` comments stripped first — without the strip,
`include/sched.h`'s explanatory comment naming
`sched_setscheduler`/`sched_getparam`/etc. registers seven false
declarations and manufactures a disagreement that is not there.
