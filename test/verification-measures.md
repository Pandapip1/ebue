<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# What else should be checked automatically

An evaluation of additional technical measures for catching ntlibc bugs
without a human noticing them first. It is written backwards from
defects that actually happened in this tree, not forwards from a list of
tools, and every measure below names the specific defects it would have
caught -- or says plainly that it would have caught none of them.

Everything measured here was measured on a fresh clone of `origin/main`
at 082ed2c, x86_64, clang 18.1.3 / gcc 13, and the numbers are quoted as
measured rather than estimated. Two of the "would have caught" claims
turned out to be "does catch, right now, on main" -- see [Live
findings](#live-findings-turned-up-while-writing-this) at the end.

## Contents

- [The defects this is written from](#the-defects-this-is-written-from)
- [The bug classes](#the-bug-classes)
- [Measures](#measures)
- [Rejected, with reasons](#rejected-with-reasons)
- [Ranking](#ranking)
- [Live findings turned up while writing this](#live-findings-turned-up-while-writing-this)

## The defects this is written from

| # | Defect | Where it was caught |
|---|--------|---------------------|
| 1 | `make asan` reported success having run zero tests: a missing `fuzz/ntstubs.c` stub made every test binary unlinkable, `nolink` was counted and printed but never checked, and the exit test `[ "$((passed + unverified))" = "$ran" ]` is `0 = 0` when `ran=0`. | by reading the summary |
| 2 | `test/posix-socket.c` printed SKIP lines and exited 0, so the harness recorded PASS over a subsystem that worked nowhere. Fixed by introducing `rc=77`. | by reading the summary |
| 3 | `IOCTL_AFD_SELECT` is `METHOD_BUFFERED` and aliased one buffer for request and reply; a zero-event poll read the request mask back and reported every socket ready. | real-Windows CI only |
| 4 | `src/socket/accept.c` leaves `AFD_RECEIVED_ACCEPT_DATA` uninitialised and indexes it without checking what the driver returned. | by reading the code |
| 5 | `3UL * sizeof(HANDLE)` multiplies in 32 bits and widens afterwards (LLP64). | clang-tidy, pinned lint stage |
| 6 | `char path[64]` with `snprintf("%s.flocktest", argv[0])`, silently truncated at long paths until the test opened the running executable. | a long clone directory name, by luck |
| 7 | `chdir()` was the only caller with a correct length check, because it carried a private copy of it, and `test/unistd.c` pinned `chdir()` -- so the shared layer's gap went unnoticed in every other caller. | a clause-by-clause audit |
| 8 | 112 implemented functions had no assertion anywhere. Writing the first one, for `vdprintf`, immediately exposed a leak. | an audit |
| 9 | A findings backlog listed 8 items as open that had all been fixed within a day. | two wasted agent cycles |
| 10 | `objcopy` silently dropped every relocation converting ELF32 to PE32. | by inspecting the output |

Defect 10 is toolchain-side; `objcopy` appears nowhere in this
repository (`grep -rn objcopy` is empty). It is included because its
*shape* -- a converter that reports success while destroying its input
-- is the shape the PE-header guard in `tools/linkcheck.sh` (9f14224)
already answers, and because that guard is incomplete in a way relevant
to exactly this defect. See [M9](#m9-extend-the-pe-header-guard-to-relocations).

## The bug classes

Four classes, not ten defects.

**A. A check that cannot detect that it checked nothing.** Defects 1
and 2, and -- as measured below -- at least six more places in the tree
that have the same property today. The distinguishing feature is that
the *reporting* is correct: `make asan` printed `0/0 tests passed [...]
49 unlinkable`, which is the truth. Nothing consumed it. A gate whose
exit status is a function of `failures` alone is green whenever the work
is empty, and the work goes empty for reasons that have nothing to do
with the gate: a stub goes missing, a tool is not installed, a `find`
matches nothing, an artifact download yields no files.

**B. Reading a reply the callee never wrote.** Defects 3 and 4, and they
are the same defect twice. In both, an NT out-parameter buffer is read
at an index without consulting the count the driver actually returned;
in defect 3 the buffer was additionally aliased with the request, so the
stale bytes were the caller's own question handed back as an answer.
This class is invisible to every local *sanitizer*, for a reason worth
stating precisely: the code does not execute locally at all. `socket()`
fails in the native build (`fuzz/ntstubs.c`'s stub volume has no
`\Device\Afd` node) and `test/posix-socket.c` exits 77 there; under Wine
the same portable open path is rejected. The bytes only exist on real
Windows.

**C. Narrowing and truncation that fails silently.** Defects 5, 6 and 7.
`USHORT` wrap (7), 32-bit multiply before widening (5), `snprintf`
truncation with a discarded return value (6). Each has a different
detector and they do not substitute for each other -- notably,
`-Wconversion` does not detect any of the three.

**D. A record of what has been checked that is not itself checked.**
Defects 8 and 9. A per-function coverage claim that no machine verifies,
and a findings list that no machine re-evaluates. Both decayed into
false confidence, and in both cases the cost was paid by a later reader.

## Measures

### M1: a floor on every stage that reports a result

**Catches: 1 (proven), 2, and six more live instances found while
measuring.**

Generalise defect 1's fix rather than repeating it. Every runner already
counts its own work; none compares that count against a floor. The
places, all verified against 082ed2c:

| Where | How it passes having done nothing |
|-------|-----------------------------------|
| `tools/asan-build.sh:374` | `[ "$((passed + unverified))" = "$ran" ]` is `0 = 0`; `nolink` never consulted. **Live: `make asan` exits 0 today with `0/0 tests passed [...] 49 unlinkable`.** |
| `tools/runtests.sh:74,128,177` | `[ -z "$wine" ]` writes `skip` for every test; the final test is `test "$fail" -eq 0`. `make check` with an empty `WINE` is green having launched nothing. |
| `tools/linkcheck.sh:558,566` | prints `$checked symbol(s) checked [...] out of $total declared` and exits on `failed` alone. No floor on `checked`; `scan`'s exit status at line 275 is not checked either. |
| `tools/lint.sh:234` | the `warn` stage does not use `require_tool`; with neither gcc nor clang installed it prints SKIP per file and returns 0 *even at the default `LINT_ALLOW_MISSING=0`*. |
| `tools/lint.sh:272,333` | `ls "$pardir"/*.log >/dev/null 2>&1 && cat ...` -- no logs means zero diagnostics means pass. clang-tidy's stderr is discarded at line 318, so a tidy that fails to run per file produces an empty log rather than a finding. |
| `tools/hdr-hygiene.sh:185-190` | the `cxx` stage `continue`s past a missing `g++`/`clang++`, leaves `ok=1`, and increments `pass`. A machine with no C++ compiler gets a passing C++-compatibility check. |
| `tools/gate.sh:242` | `[ -f "$rcfile" ] || continue` -- a stage whose `.rc` was never written (subshell killed, `run_stage` never reached) vanishes from the summary without setting `fail`. |
| `.github/workflows/ci.yml:360-390` | `Get-ChildItem -Path test-exes -Filter *.exe | ForEach-Object { ... }` over an empty download runs the body zero times, leaves `$failed` false, and exits 0. This is the leg that caught defect 3, and it is the one with no floor at all. |

The measure is not eight separate patches. It is one convention: every
runner prints a machine-readable line naming what it did
(`ntlibc-work: stage=asan ran=48 nolink=0`), and one committed baseline
file gives the minimum for each. A run below the floor fails and says
which stage went empty and by how much. A deliberate reduction -- a test
removed, a header retired -- is a one-line baseline edit in the same
commit, which is exactly the visibility that was missing.

Take the floors from counts, not percentages, and set them slightly
below today's numbers so ordinary churn does not trip them. `nolink` and
its equivalents get a floor of zero: a binary that stopped linking is
never acceptable, and treating it as merely "not counted" is what
defect 1 *was*.

**Cost.** Zero wall clock -- these are comparisons on numbers the
scripts already have. Roughly 40 lines across seven files plus a
baseline file. Maintenance is one line per intentional change to the
work set, and that line is the audit trail.

**Honest limit.** This catches nothing about correctness. It only
guarantees that the other measures are running. That is precisely why it
is first: every other entry in this document is worthless behind a gate
that cannot tell it stopped running.

### M2: declared, implemented, and never referenced by a test

**Catches: 8, directly and mechanically.**

`tools/linkcheck.sh` and `tools/lint-undefined.sh` already extract every
function a public header declares, and `lint-undefined.sh` already
computes the set of names `src/`, `arch/` and `crt/` define. The missing
third set is "names some test actually references", and the native ASan
build supplies it: compile each `test/*.c` to an object with the flags
`tools/asan-build.sh:315` already uses, take `nm --undefined-only` over
those objects, and subtract.

Prototyped on 082ed2c. Measured, end to end:

```
tools/lint-undefined.sh's scanner (826 declared)      1.3 s
clang -c over test/*.c (52 objects, 2 refusals)       1.6 s
nm --undefined-only + sort + comm                     0.3 s
                                                    ------
                                                      3.2 s
```

Result: **826 declared, 730 referenced, 203 declared-but-unreferenced,
of which 165 are also implemented** -- 75 in the `math.h` `f`/`l` tail
and 90 elsewhere, including `puts`, `getc`, `mkdirat`, `linkat`,
`fchownat`, `mkfifo`, `mknod`, `_Fork`, `fexecve`, `pause`, `alarm`,
`gets`, `getlogin` and the whole `*_unlocked` family. That is the same
population `test/POSIX-GAP-ACCOUNTING.md:695` describes in prose as "the
remaining 112 implemented-but-unasserted functions", now enumerable in
three seconds instead of by audit.

Two known false-positive sources, both cheap to handle by name:
`test/rpath.c` and `test/delayall.c` do not compile natively (PE-only),
so the `ntlibc_rpath_*` and `ntlibc_delayLoadHelper2` names they
reference appear unreferenced; a textual pass over those two files, or
an exclusion list of four names, removes it. A function that a header
also defines as a macro would be a third source, but this project
defines none -- `getc` is a real prototype at `include/stdio.h:90` and
is genuinely never called; only `fgetc` is.

Symbol-level beats the textual grep the earlier audit used (48d4025,
"grepped against the concatenation of all `test/*.c`"): a name in a
comment is not a reference. `alarm` appears in `test/fork-handles-win.c`
only in a sentence explaining that it is a stub.

**Cost.** 3.2 s, once, in a stage that can run concurrently with
everything else -- so approximately zero added wall clock to a
concurrent gate. Ship it report-only against a committed baseline count
first, so the number can only go down; converting to a hard failure on
new unreferenced functions is a later, separate decision.

**Honest limit.** "Referenced" is not "asserted". A test that calls a
function and ignores the result satisfies this check. It is still the
right first cut, because 165 functions no test even *mentions* is a
larger and more tractable problem than the assertion-quality one behind
it.

### M3: run the two bespoke lints that already exist

**Catches: the class of 7, at 0.5 s.**

`tools/lint-ushort.sh` was written *specifically* to prevent defect 7
recurring (4f02ef3, "lint: add a check for unguarded (USHORT)
truncation"). It is dispatchable as `tools/lint.sh ushort`
(`tools/lint.sh:418`) and it is in **none** of: `lint.sh`'s default
stage list (`tools/lint.sh:392` -- `warn analyze cppcheck shell
undefined`), `tools/gate.sh`'s `ALL_STAGES`, or `.github/workflows/
ci.yml`'s lint matrix (`warn analyze cppcheck shell undefined`).

Run against 082ed2c it takes 0.48 s and reports 3 findings:
`src/internal/path.c:118`, `src/ioctl/ioctl.c:121`,
`src/termios/termios.c:175`. On inspection all three are bounded --
two narrow `strlen()` of a string literal, one re-narrows a value that
was already a `USHORT` -- so the work is to add the USHORT-safe markers
the script already understands, not to fix three bugs. But nobody knew
that, because the check has never run in a gate.

**Cost.** 0.48 s and three comment markers. This is the cheapest item in
this document by an order of magnitude, and it is the direct product of
a previous bug hunt being left unwired.

### M4: device-free contract tests for every NT ioctl

**Catches: 3 and 4 -- the only measure here that could have caught
either one locally.**

The pattern already exists and already worked. `test/posix-socket-poll.c`
opens no socket and touches no device: it builds the exact byte image
`src/select/select.c` sends, asserts its layout, then hands the
interpreter the byte image *the bug produces* (the request with
`Handles[0].PollEvents = 0x1FF`, first 16 bytes overwritten with
`NumberOfHandles = 0` -- bit-for-bit what `METHOD_BUFFERED` leaves in an
aliased buffer when the driver completes with `Information == 16`) and
requires it to be rejected. Commits 03db676, a9ac939 and aca3222 are
three instances; aca3222 records that the battery reports five failures
against the pre-fix code and passes against the fix.

The generalisation is a convention plus a check:

1. **Convention.** Every ioctl wrapper splits into a pure request
   builder, a pure reply parser, and the `NtDeviceIoControlFile` call.
   The builder and parser are separately callable and are tested with no
   device, which means they run under `make check` on both arches, on
   real Windows, *and* under `make asan` with the sanitizers on -- the
   only place any of this logic gets sanitizer coverage at all.
2. **Check.** A grep-shaped lint in the style of `tools/lint-ushort.sh`:
   every caller of `__afd_ioctl` / `NtDeviceIoControlFile` /
   `NtFsControlFile` must (a) zero-initialise any out-parameter struct
   and (b) bound every read of it by the returned count or
   `IoStatus.Information`, or carry an explicit marker saying why not.
   There are three such call sites today (`src/unistd/link.c`,
   `src/socket/afdsupport.c`), so the check is trivially fast and its
   false-positive surface is three lines wide.

Defect 4 is still live on `origin/main`: `src/socket/accept.c:34`
declares `AFD_RECEIVED_ACCEPT_DATA recvd;` uninitialised, and
`__afd_ioctl` does not return the number of bytes the driver wrote, so
the caller could not check even if it wanted to. The lint would flag it;
the convention would have prevented it; a signature change to
`__afd_ioctl` making the written length an out-parameter would make the
class unrepresentable, which is better than either.

**Cost.** The lint: under a second, ~60 lines. The convention: real
authoring cost, per ioctl, paid once each. No added gate wall clock --
these tests are among the fastest in the suite because they open
nothing.

### M5: one checked contract for `rc=77`, not one shared implementation

**Catches: none of the ten. Prevents the next one.**

`rc=77` is honoured in three places that were each taught it
independently: `tools/runtests.sh:160-176`, `tools/asan-build.sh:340-351`,
and `.github/workflows/ci.yml:367-375` (PowerShell). Unifying the
*implementations* is not possible in any pleasant way -- they are three
languages in three environments, and the CI leg cannot source a shell
function. Unifying the *contract* is possible and is the part that has
value:

- one table, in `CONTRIBUTING.md`, of the exit codes and their buckets;
- a fixture: three tiny programs exiting 0, 1 and 77, plus one that
  prints SKIP lines and exits 0 (defect 2's exact shape);
- a conformance test that pushes all four through each of the three
  runners and asserts the bucket and the overall exit status.

The fourth fixture is the one that matters: it is the check that a
runner cannot be re-taught to accept "printed SKIP, exited 0" as a pass.

**Cost.** Under a second locally; the CI leg's third of it costs one
extra job step. ~80 lines. Rank it below M1-M4: it hardens a convention
rather than finding a defect.

### M6: pin the check *list*, not just the tool version

**Catches: 5, in the plain stage as well as the pinned one.**

The premise needs correcting first. `.clang-tidy` at the tree root is
the sole source of checks for both lint stages -- `tools/lint.sh:317`
invokes `"$tidy" --quiet "$f" -- $target "$@"` with no `--checks=`
override, and `tools/gate.sh:212` differs from `lint-plain` only in
which binary it runs.
`bugprone-implicit-widening-of-multiplication-result` is not named
individually anywhere; it arrives through the `bugprone-*` wildcard at
`.clang-tidy:17` and has existed since LLVM 13. So the asymmetry that
made defect 5 pinned-only is not a check-list difference. It is one of
two things:

1. `tools/lint.sh:320-332`: with no clang-tidy installed and
   `LINT_ALLOW_MISSING=1`, `analyze` silently degrades to `clang
   --analyze`, which runs **none** of `bugprone-*`. The stage still
   reports a finding count and still passes.
2. Version drift under the wildcards, which
   `.github/workflows/ci.yml:422-427` already documents as the reason
   the CI stage is pinned to clang-tidy 18.

Both are instances of class A -- a check that cannot tell it did less
work than it claims. The fix is the same shape as M1: commit the
expected check list (`clang-tidy --list-checks` under the pinned tool,
filtered to the enabled families) and have `stage_analyze` fail if the
running binary does not offer every check on it. A newer tool offering
*more* is fine and is the intended drift; a tool offering fewer, or a
fallback offering none, is now a failure that names the missing checks.

**Cost.** One extra process per run (`--list-checks`, well under a
second), one committed baseline. Removes the silent `clang --analyze`
fallback as a passing outcome.

### M7: vary the path length the suite runs from

**Catches: 6. Nothing else does.**

I tested the compiler route and it does not work. GCC 13 with `-O2
-Wall -Wextra -Wformat-truncation=2` is silent on the exact pattern:

```c
char path[64];
snprintf(path, sizeof path, "%s.flocktest", argv[0]);
```

and on two variants of it (result assigned but unused; source a
`char[100]`). Clang has no such warning. `tools/lint.sh`'s `warn` stage
compiles with `-fsyntax-only`, which disables the analysis that would
drive it in any case. So no static check available here would have found
defect 6.

I also measured the grep route, in the style of `tools/lint-ushort.sh`:
"`snprintf` into a fixed-size array with the return value discarded" has
**88 hits** across `src/`, `test/` and `crt/` on 082ed2c, and the ones I
inspected are all bounded by construction (`test/spawn-stdhandle-attr.c`
formats a `cwd[4096]` plus a 22-character suffix into `report[4200]`).
88 sites needing markers to protect against one historical defect is a
bad trade. **Rejected.**

What remains is the environment. Defect 6 fired at roughly 63 characters
of `argv[0]` and hid under `tools/gate.sh` because every stage runs from
`/tmp/ntlibc-gate.XXXXXX/trees/<stage>/`. `make_tree` (`tools/gate.sh:77-88`)
already names that directory; padding *one* stage's name to a few
hundred characters, and leaving the others short, costs nothing and
exercises both regimes every run. Do the same in `tools/runtests.sh`,
whose per-test `mktemp -d` (`:81`) is the working directory each test
actually sees.

**Cost.** Zero wall clock. Two lines. Note that this catches such a
defect only *locally under Wine* -- and that is where defect 6 lived, so
that is enough.

**Honest limit.** One padded stage tests one length. This is a sampling
measure, not a proof, and it will find the next 64-byte buffer only if
that buffer is on a path the padded stage exercises.

### M8: make the open-findings list re-evaluate itself

**Catches: 9.**

Defect 9 was a list of 8 findings, all fixed, still recorded as open,
costing two agent cycles. The durable fix is not "keep it updated"; it
is to stop storing a claim and start storing a *predicate*. Each row of
the open-findings record names a command whose non-zero exit means the
finding is still open -- an assertion in a test, a `tools/lint-*.sh`
finding line, a `grep` for the offending construct. A script runs every
row and reports the ones that now pass, i.e. the rows that are stale.

This is exactly what `tools/lint-ushort.sh` does for one class already;
M8 is the same idea applied to the record rather than to the code.

The neighbouring proposal -- a script that refuses to run against a
clone whose `origin/main` is behind -- is worth having as a *warning*
and not as a refusal. `tools/gate.sh` gating on a network fetch makes an
offline gate impossible, and the failure mode it prevents (working from
a stale clone) is much less costly than the failure mode it introduces
(a gate that cannot run). One `git fetch --dry-run` and a printed line
saying how many commits behind, at the top of the summary.

**Cost.** The predicate runner costs the sum of the commands it names --
for a list of eight, seconds. The staleness warning costs one network
round trip and can be skipped with an environment variable.

### M9: extend the PE-header guard to relocations

**Catches: 10's class. Partially covered already.**

`pe_header_check()` (`tools/linkcheck.sh:426-476`) already asserts MZ and
PE signatures, `Magic` against the configured `ARCH`, `SectionAlignment
>= 0x1000`, and `SectionAlignment >= FileAlignment` -- with the real
measurement recorded in the message (real NT rejects a sub-page
`SectionAlignment` with `ERROR_BAD_EXE_FORMAT`; Wine loads it fine, so
the Wine suite can never catch it). That is the right answer to
"a converter reported success" and it is already in the tree.

It does not check the thing defect 10 destroyed. Add two fields from the
optional header's data directory: `IMAGE_DIRECTORY_ENTRY_BASERELOC` size
must be non-zero for an image that is not `IMAGE_FILE_RELOCS_STRIPPED`,
and the `.reloc` section header's size must agree with it. Both are
`od`-readable at fixed offsets, in the same style as the existing code.

**Cost.** Two more `pe_le` reads per checked image. Note that
`pe_header_check` is called once per *symbol* in a serial loop, so its
per-image cost is multiplied by several hundred; see the cost note on
`linkcheck` in [Ranking](#ranking).

## Rejected, with reasons

### MemorySanitizer -- rejected as a gate, worth one opt-in run

The bug it is supposed to catch is defect 4, and **it could not have
caught defect 4**, for a reason that has nothing to do with MSan's
quality: the code does not run. `socket()` fails in the native build
because `fuzz/ntstubs.c`'s stub volume has no `\Device\Afd` node, so
`test/posix-socket.c` exits 77 there -- `tools/asan-build.sh:340-351`
says so in its own comment. `accept()` is never reached, and an
instrument that is not pointed at the code sees nothing.

The secondary costs are real but they are not the argument. MSan cannot
share a binary with ASan, so it is a *third* full native build of
`src/*.c`; the ASan objects alone measured 12.8 s and a full `make asan`
24.3 s on this machine today (and that is with 49 of the 54 tests failing to
link -- the honest number once they link again is larger). Because
`tools/gate.sh` runs stages concurrently, the wall-clock cost of a new
stage is zero until it becomes the critical path, so the objection is
CPU and maintenance, not wall clock.

`CONTRIBUTING.md` already records the Valgrind rejection (62e1b43) and
names MSan as the cheaper route "if that check is ever wanted". I agree
with that framing and would go one step further: `tools/asan-build.sh`
already parameterises its sanitizer set (`SAN` at `:74`, `CONVSAN` at
`:105`, `INTSAN` at `:125`), so an `NTLIBC_MSAN=1` mode is roughly
fifteen lines. Add the mode, run it once, and let the result decide.
Promoting it to a gate should require a finding, not an argument.

I did check the usual objection empirically rather than repeating it:
MSan against an uninstrumented glibc is *not* automatically unusable --
a trivial program mixing `malloc`, `strlen`, `snprintf` and `printf`
under `-fsanitize=memory` runs clean, because MSan intercepts those. The
risk is confined to whatever in `fuzz/ntstubs.c` reaches an
uninstrumented path MSan does not intercept, which is measurable in one
run rather than arguable.

### Valgrind -- already rejected, and the rejection still holds

`CONTRIBUTING.md` records it: it cannot reach the PE/Wine target, it
does not coexist with ASan so it needs a third build anyway, and its one
distinctive check is the noisiest possible one against a `-nostdinc`
libc linked to a sanitizer runtime. Nothing measured here changes that.

### `-Wconversion` / `-Wsign-conversion` -- rejected for this purpose

Already available as `LINT_CONVERSION=1` (`tools/lint.sh:70,140-143`),
off by default with ~50 known findings, mostly deliberate mask idioms.
The relevant point is narrower: **it would not have caught defect 5.**
`3UL * sizeof(HANDLE)` is `unsigned long * size_t` with no conversion
anywhere; the defect is the *width the multiplication happens in*, which
is what `bugprone-implicit-widening-of-multiplication-result` detects and
what M6 keeps detectable. Nor would it have caught defect 7, which is an
*explicit* `(USHORT)` cast that no conversion warning fires on -- as
`tools/lint-ushort.sh:14-17` says in its own header, which is why that
script exists.

Leave it as the periodic-read option it already is.

### Line coverage as the answer to defect 7 -- rejected

Tempting and wrong. `chdir()` was covered. The other callers of the
shared layer were covered too. What was missing was not execution but an
*oversized input* reaching an assertion, and a line-coverage report would
have shown green across the whole path. Coverage answers "was this
executed", and defect 7's question was "was this executed with the input
that breaks it". The measures that address defect 7 are M3 (the lint
that exists for exactly this class) and a boundary-value convention in
the tests, not an instrumented build.

Coverage is still worth having, for a different reason -- see below.

### Fuzzing "should we start" -- the premise is stale, which is itself defect 9

The backlog records fuzzing as queued and never started. It is running.
There are eight harnesses (`fuzz/fuzz_strtod.c` and its
`printf`/`scanf`/`utf`/`path`/`strptime`/`strtol`/`strftime` siblings),
a 219-line differential oracle against glibc (`fuzz/host_oracle.c`), and
`.github/workflows/fuzz.yml` runs `tools/fuzz.sh` nightly on cron
`17 7 * * *` at 300 s per harness. `CONTRIBUTING.md` records that the
fuzzers, not the tests, are what found the `sprintf` byte leak, because
libFuzzer checks for leaks after every input rather than once at exit.

So the question is not whether to start. It was the one real gap:
**no corpus survived a run.** That has since been fixed, and the
explanation quoted here when it was written -- taken from
`tools/fuzz.sh:13-21`, which said a corpus directory means file I/O
through ntlibc's `open`/`readdir`, which reach `NtCreateFile`, which
`fuzz/ntstubs.c` answers `STATUS_NOT_IMPLEMENTED` -- was wrong, in
`tools/fuzz.sh`, in `.github/workflows/fuzz.yml`, and therefore here.
It is worth recording what the mechanism actually was, because this is
the third time in this document an inherited claim about the tree turned
out not to survive being run.

`fuzz/ntstubs.c` answers no `STATUS_NOT_IMPLEMENTED`. It implements a
complete in-memory volume -- `NtCreateFile`, `NtReadFile`,
`NtQueryDirectoryFile`, rename, delete. Measured on a fresh clone at
`d3c4f1f`:

```
stat("/tmp/.../corpus_strtod") = -1 errno=2   (ENOENT, not ENOSYS)
ERROR: The required directory "/tmp/.../corpus_strtod" does not exist
```

The directory was simply never put in the volume, which starts out
holding only `C:\work` and `C:\tmp`. And behind that sat a second,
independent defect that would have blocked a corpus in a directory that
did exist: libFuzzer is compiler-runtime code compiled against the
*host* headers, and its `IsDirectory()` calls `stat()`, which resolves to
ntlibc's. The two `struct stat`s disagree -- ntlibc puts `st_mode` at
offset 16 and `st_nlink` at 24, glibc/x86\_64 the other way round -- so
`S_ISDIR` is false for every directory in the volume. A host-headers
probe reading ntlibc's answer for a directory sees `st_mode = 01`, which
is `st_nlink`.

Both are fixed in `fuzz/` only: `NTLIBC_FUZZ_MIRROR` mirrors one named
host directory into the volume (reads, writes and unlinks), and
`-Wl,--wrap=stat` on the harness link translates the layout through
`fuzz/statshim.h`. Three consecutive 15 s runs, coverage libFuzzer
reports at `INITED` -- before it has fuzzed anything:

| harness | run 1 | run 2 | run 3 |
|---------|-------|-------|-------|
| `scanf` | 24 | 494 | 593 |
| `strftime` | 24 | 631 | 646 |
| `strtod` | 81 | 314 | 329 |
| `printf` | 24 | 559 | 559 |

Two further things the same work turned up, both of the vacuous-success
shape this document is about:

- **A differential mismatch produced no reproducer.** The harnesses
  report a wrong *result*, which no sanitizer sees, and end with the
  host's `abort()`; libFuzzer's own `SIGABRT` handler never fires,
  because it installs one with `sigaction()`, which resolves to ntlibc's,
  which delivers nothing natively. ASan does not handle `SIGABRT` by
  default. So the finding these fuzzers are uniquely good at was the one
  kind that left nothing behind. `ASAN_OPTIONS=handle_abort=1` fixes it.
- **`tools/fuzz.sh --repro`, the documented way to replay a finding,
  could never replay anything.** The harness reads the artefact through
  ntlibc, and a host path was not in the volume; every artefact answered
  `ERROR: The required directory "<file>" does not exist` and the script
  exited without running the input. Confirmed against a pristine
  checkout of `main`.

One smaller thing left: two comments still say "four harnesses"
(`tools/asan-build.sh:82`) where there are eight; the one in
`.github/workflows/fuzz.yml` is fixed. `FUZZ_TIME` was fixed separately
in `c533d9a`.

### Coverage measurement -- worth starting, report-only, not as a gate

`clang -fprofile-instr-generate -fcoverage-mapping` over the same native
build that `tools/asan-build.sh` already produces gives line and branch
coverage of `src/*.c` from the native test run, for roughly the cost of
one more `make asan` (24 s today). It would not have caught any of the
ten defects. What it is for is the *next* M2: once 165 functions have a
reference, coverage is the instrument that says which of their branches
no test reaches, and that is a question M2 structurally cannot answer.
Start it as a number printed on demand with no threshold. A coverage
*gate* is how projects acquire tests written to move a number, and this
project's tests are unusually good precisely because they are written to
clause citations instead.

### A commit-shaped "negative control required" rule -- rejected as written

"Every fix ships with a test verified to fail against the pre-fix code"
is the right practice; three commits do it today (03db676, a9ac939,
aca3222), and 4595214 and c3e2c67 record the equivalent for their own
fixes. But a pre-commit hook checking that a `src/` change is
accompanied by a `test/` change and a sentence in the message is a
process rule wearing a script's clothes: it can be satisfied without
verifying anything, and it fires on commits where it makes no sense.

The technical form is mutation testing over a narrow, already-known
slice: `tools/negcontrol.sh <commit>` reverts that commit's `src/` hunks
in a scratch tree, rebuilds, runs the tests the commit touched, and
requires at least one to fail. That is a real check with a real answer,
and it is the same "prove the instrument is pointed at the code" idea as
M1. Cost is one rebuild-and-run per commit examined -- about 25 s
natively, longer under Wine -- which makes it an on-demand tool and a
pull-request job, never a step in the 70-second gate.

## Ranking

By expected defects caught per unit of cost.

| Rank | Measure | Cost | Would have caught |
|------|---------|------|-------------------|
| 1 | **M1** floors on every stage | ~0 s, ~40 lines | 1 (proven live), 2, and 6 more live holes |
| 2 | **M3** wire up `lint-ushort` | 0.5 s, 3 markers | the class of 7 |
| 3 | **M2** declared-but-unreferenced | 3.2 s | 8 |
| 4 | **M4** device-free ioctl contract tests + lint | <1 s gate, real authoring | 3 and 4 -- the only local measure that could |
| 5 | **M6** pin the clang-tidy check list | <1 s | 5, in the plain stage too |
| 6 | **M7** vary the run path length | 0 s, 2 lines | 6 |
| 7 | **M8** self-evaluating findings list | seconds | 9 |
| 8 | **M9** relocations in the PE guard | 2 reads/image | 10's class |
| 9 | **M5** one checked `rc=77` contract | ~1 s, ~80 lines | none; prevents the next 2 |
| -- | fuzzing corpus persistence | real work, done | unknown, and that is the point; it found two vacuous-success defects on the way in |
| -- | coverage, report-only | 24 s on demand | none of the ten |
| -- | MSan, opt-in mode only | 15 lines | none of the ten |
| -- | `-Wconversion`, Valgrind, coverage-as-gate, snprintf grep, commit-shaped negative-control rule | -- | rejected above |

**If only one is adopted, adopt M1.** Not because it is the most
interesting -- it is the least interesting measure in this document --
but because it is the only one whose absence makes the others
worthless, and because it is not hypothetical: `make asan` on
`origin/main` exits 0 today having run zero of the 49 tests it
should have run, and the CI job
that runs it (`.github/workflows/ci.yml:489`) is green for the same
reason.

**If two, add M3**, on the grounds that half a second to run a check
somebody already wrote for a bug that already happened is the best trade
in the table.

One note on cost that cuts across the ranking. `tools/gate.sh` runs its
stages concurrently, so a new stage adds *zero* wall clock until it
becomes the critical path. The ~70 s figure is therefore a budget for
the slowest stage, not a sum, and the relevant question for any addition
is "is it slower than `linkcheck`", not "how many seconds does it add".
`linkcheck` itself is worth a look while nearby: it is strictly serial,
two `$CC` invocations plus four to six `od`/`tr`/`sed` process pairs per
symbol, over several hundred symbols, twice (once per arch). It would
parallelise trivially with the same `xargs -P` pattern
`tools/runtests.sh` and `tools/lint.sh` already use, and that would buy
more headroom than any measure here spends.

## Live findings turned up while writing this

Verified against a fresh clone of `origin/main` at 082ed2c. None of
these are proposals.

1. **`make asan` is green and runs nothing, right now.** `NtYieldExecution`
   is declared at `src/internal/nt.h:1248` and called from
   `src/misc/sched.c:27`, added by ad5305b ("sched: add sched_yield() and
   a minimal `<sched.h>`"), and `fuzz/ntstubs.c` has no stub for it. All
   49 of the 54 test links fail (5 more are excluded as not applicable
   natively); `tools/asan-build.sh` prints `asan: 0/0 tests
   passed, 0 unverified, 5 not applicable natively, 49 unlinkable` and
   exits **0**. 31 commits and about two hours between ad5305b and
   082ed2c. `.github/workflows/ci.yml:489-490` runs the same command and
   is green for the same reason. This is defect 1, unfixed, reproduced in
   24 seconds.
2. **`make check` with an empty `WINE` is green having run nothing**
   (`tools/runtests.sh:128`, `:177`).
3. **`tools/lint-ushort.sh` has never run in a gate** and reports 3
   unmarked findings today.
4. **The `cxx` stage of `tools/hdr-hygiene.sh` counts a pass when no C++
   compiler is installed** (`:185-190`).
5. **The `warn` stage of `tools/lint.sh` returns success with no compiler
   installed even at `LINT_ALLOW_MISSING=0`** (`:234`), because it is the
   one stage that does not call `require_tool`.
6. **`tools/gate.sh` drops a stage that never wrote an `.rc` file from
   its summary without failing** (`:242`).
7. **The real-Windows CI leg passes on an empty artifact download**
   (`.github/workflows/ci.yml:360-390`) -- the one leg that caught
   defect 3.
8. **`src/socket/accept.c:34`'s `AFD_RECEIVED_ACCEPT_DATA` is still
   uninitialised** and `__afd_ioctl` still returns no written length, so
   defect 4 is live.
9. **`FUZZ_TIME` is referenced at `Makefile:427` and never defined**, so
   `make fuzz` uses `tools/fuzz.sh`'s `${1:-60}` rather than any
   Makefile default. *(Fixed in `c533d9a`.)*
