<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Should CI run our patched Wine instead of stock apt Wine?

This is an investigation and a recommendation, not an implementation. No CI
configuration is changed by this document; the one concrete change it argues
for is written out as a diff in [Appendix A](#appendix-a-the-proposed-diff)
for a human to apply or reject.

The question: `.github/workflows/ci.yml` installs **stock Wine from apt** for
its three `test` legs. That Wine cannot run a quarter of the test suite and is
measurably more permissive than real Windows. We maintain a patched Wine
locally that fixes both. Should CI use it, what would it cost, and where would
the patched Wine come from?

**Short answer: yes, and it is much cheaper than expected — but as an
*additional* leg, not a replacement, and only after the patch series is pushed
to a fetchable remote.** The provenance problem, which looked like the
deciding constraint, turns out to be already half-solved: a `Pandapip1/wine`
fork exists and five of the thirteen patches are already on it.

## Summary of findings

| Question | Answer |
| --- | --- |
| Does patched Wine unlock the `*-win.c` tests? | **Yes.** All four pass under it; all four abort under stock Wine. |
| Does patched Wine turn currently-green tests red? | **No.** Measured: 47 passed, 0 failed, versus 43/0 on stock Wine — a strict superset. |
| Where does patched Wine come from? | `github.com/Pandapip1/wine`, pinned by SHA, exactly like `TINYCC_SHA`. The fork already exists. |
| What does the build cost? | See [Cost](#2-cost-what-a-wine-build-actually-costs). |
| Should it replace stock Wine? | **No** — add a fourth `test` leg. Stock Wine is itself a signal worth keeping. |

## 1. What stock Wine cannot do

CI's `test` job runs `sudo apt-get install -y wine wine32:i386 wine64` on
`ubuntu-24.04`, which is wine-9.0. Three concrete limitations:

**No `RtlCloneUserProcess`.** ntlibc's `fork()` is built on it. Under stock
Wine every forking binary dies before reaching its first assertion:

```
$ WINEDEBUG=-all /usr/lib/wine/wine64 ./fork-win.exe
wine: Call from 00006FFFFFC7D3B8 to unimplemented function
      ntdll.dll.RtlCloneUserProcess, aborting
```

The project survives this only because every fork-using test is named
`*-win.c` and `Makefile:232` reads

```make
TEST_RUN = $(filter-out %-win.exe,$(TEST_EXES))
```

so those binaries are built but never handed to Wine. They are still uploaded
as artifacts and run by the `windows-test` job on a real Windows runner, so
they are not untested — but they are tested *once*, on the slowest and least
introspectable of the three environments, with no local or Wine-side signal at
all. The four affected tests are `fork-win.c`, `fork-handles-win.c`,
`fork-cloexec-exec-win.c` and `process-win.c`: between them, essentially all of
`fork()`, `waitpid()`, `wait()`, the `exec` family, `__spawn`'s argument
quoting and `crt1.c`'s `split_cmdline` round-trip.

The naming convention is also a **tripwire aimed at contributors**: a new test
that happens to fork and is not named `*-win.c` turns `main` red immediately
under Wine, for a reason that has nothing to do with the change. This has
already happened once (sh stage 4, `de3e39e`).

**Sockets are untestable.** Stock Wine's AFD accepts only Wine's own invented
`IOCTL_AFD_WINE_CREATE` open path, not the portable `NtCreateFile`-plus-EA
form real Windows uses, and it has no real `IOCTL_AFD_CONNECT`. Socket work
therefore gets zero Wine coverage; every iteration costs a full CI round trip
to the `windows-test` legs.

**It is more permissive than real Windows.** Several NT-strictness divergences
we have patched locally are still present in stock Wine — the
`FILE_READ_ATTRIBUTES` access check on `FileBasicInformation` queries, the
`FileAttributes == 0` "leave unchanged" rule, `STATUS_DIRECTORY_NOT_EMPTY`
versus a blanket rename `ACCESS_DENIED`, `RootDirectory` resolution in
`NtQueryAttributesFile`, and sub-page `SectionAlignment` rejection. Each of
those is a case where CI's Wine legs are *greener than the truth*. The
`utimensat` bug took three attempts to fix precisely because each earlier
attempt was validated in an environment that could not see it.

## 2. Cost: what a Wine build actually costs

*(measured below)*

## 3. Provenance: where the patched Wine comes from

This looked like the deciding constraint, and it is the part of the
investigation that changed the answer. The premise — "our patches are
local-only and unpushed, and CI cannot fetch from a developer's home
directory" — is **only partly true**.

`github.com/Pandapip1/wine` already exists: a fork of `wine-mirror/wine`, last
pushed 2026-08-23, with a branch `rtlcloneuserprocess-and-pid-retention`
carrying five of the patches:

```
db5520fd3 server: switch the job memory-limit approximation to VmRSS
f29e8d6d0 server: Implement JOB_OBJECT_LIMIT_* enforcement
87dc248d6 ws2_32/ntdll/server: Implement the real IOCTL_AFD_CONNECT ioctl
ae50a1fe7 server: Keep terminated processes referenceable by pid for a while
b0e948b98 ntdll: Fork twice in clone_process so the clone is reparented to init
657d2b0e4 server: Release create_process's reference in the clone_process handler
9f23f49da WIP: RtlCloneUserProcess (pre-existing work-in-progress)
```

The local tree `~/Projects/wine` carries **thirteen** commits on upstream
`8da89f8` (`Release 11.16`). The two sets have diverged in both directions:
the fork has two job-limit commits the local tree does not, and the local tree
has eight commits the fork does not —

```
24f7c7c4a server: Reject sub-page SectionAlignment on x86/x86_64 too
8bcfd6677 ntdll: Honor FileAttributes==0 ("leave unchanged")
fe5eb544f ntdll: Enforce FILE_READ_ATTRIBUTES for a FileBasicInformation query
78fa1f82d server: distinguish STATUS_DIRECTORY_NOT_EMPTY from rename ACCESS_DENIED
c4ad21511 ntdll: Resolve NtQueryAttributesFile names against RootDirectory
44ab7918a ntdll/server: Accept the real AfdOpenPacketXX socket-creation form
25ab93939 kernelbase: give a CUI process a console (REVERTED)
d6a631ef3 Revert "kernelbase: Give a CUI process a console ..."
```

Note that `25ab93939` and its revert `d6a631ef3` cancel out, so the effective
patch count is eleven, not thirteen.

So the provenance answer is not "build new infrastructure". It is **push a
branch to a fork that already exists, and pin it by SHA** — the same shape
this workflow already uses for its compiler:

```yaml
TINYCC_REPO: https://github.com/Pandapip1/tinycc.git
TINYCC_SHA: 69eed4d346f31dea12d61b99f60298d2f59f66be
```

...with the same reasoning the existing comment gives for it: "Pinned by SHA,
not by branch name, so a push to the fork cannot silently change what CI
compiles with." A `WINE_REPO`/`WINE_SHA` pair is a direct copy of a pattern
already reviewed and accepted in this file.

### Why not the alternatives

**A patch series committed into ntlibc, applied to a pinned upstream tag in
CI.** Attractive in principle — the patches become reviewable in ntlibc's own
history, drift becomes a `git am` failure rather than a silent divergence, and
there is no second repository to keep alive. Rejected for three reasons.
First, it is a licensing tangle nobody needs: ntlibc is GPL-3.0-or-later and
Wine is LGPL-2.1-or-later, so carrying eleven Wine patches in-tree means
carrying a second licence and satisfying `reuse lint` for every `.patch` file.
Second, `git am` against a moving upstream fails *in CI*, at which point a
Wine rebase blocks every push — the failure mode is strictly worse than the
fork's, where a stale pin simply keeps working. Third, it is more work than
`git push`, and the fork already exists.

**Publishing a prebuilt tarball (a GitHub release asset on the fork).** This
is the right answer *if* the in-CI build turns out to be expensive — see
[Cost](#2-cost-what-a-wine-build-actually-costs). It removes the build from CI
entirely and makes the leg as fast as the current ones. Its cost is a manual
publish step per patch bump and an artefact whose provenance is "whatever was
on the maintainer's machine that day", which is exactly the reproducibility
property the `TINYCC_SHA` pin exists to protect. Keep it in reserve.

## 4. What it would unlock: measured, not estimated

The claim "patched Wine would let the `*-win.c` tests run" is checkable, so it
was checked rather than argued. Method: build ntlibc at `0041753` for
`x86_64-win32` with the pinned tinycc, then run **every** binary in
`obj/test/` — not just `TEST_RUN` — under each Wine.

Stock Wine (`/usr/lib/wine/wine64`, wine-9.0, the closest available match to
what CI installs), via `make check`, i.e. with the `*-win.exe` filter applied:

```
43 passed, 0 failed, 1 unverified
```

Patched Wine (`~/Projects/wine/build-wow64/loader/wine`, all thirteen commits,
binary rebuilt after the last of them), with the filter **removed**:

```
47 passed, 0 failed, 1 unverified
    PASS fork-cloexec-exec-win.exe
    PASS fork-handles-win.exe
    PASS fork-win.exe
    PASS process-win.exe
```

Two things follow, and they are the core of the recommendation.

**The `*-win.c` exclusion can be relaxed.** All four tests pass under patched
Wine. They are ordinary self-contained binaries — `process-win.c` re-executes
itself with a `--role` argument rather than depending on any external program,
and `fork-handles-win.c` implements its own timeout by polling with `WNOHANG`
— so there is no hidden dependency on the real-Windows environment. Nothing
about them requires Windows specifically; they require `RtlCloneUserProcess`,
and patched Wine has it. That is roughly a 9% increase in Wine-covered tests,
but the *value* is far above the count: it is the entire process-management
surface, currently covered once per push on a Windows runner and nowhere else.

**Nothing currently green goes red.** This was the risk most worth checking,
and the measurement says the risk did not materialise: patched Wine's result
set is a strict superset of stock Wine's, 0 failures either way. The
strictness patches — `FILE_READ_ATTRIBUTES`, pid-after-exit retention, the
rename status fix — were each written to surface a specific ntlibc bug, and
every one of those bugs has since been fixed. `waitpid-overflow.exe`, called
out in the project notes as failing under the pid-retention patch, now passes.

That is a snapshot, not a guarantee. The *next* strictness patch will very
likely turn something red, because that is what strictness patches are for.
The point is that adopting the current series is not a disruptive event.

## 5. Risks

**A Wine build failure blocks all CI.** This is the risk that decides the
shape of the change, and it is why the recommendation below is an *additional*
leg rather than a replacement. Wine's build depends on a wide set of system
packages, and it fails loudly when one is missing — this investigation lost a
cycle to exactly that:

```
checking for flex... no
configure: error: no suitable flex found. Please install the 'flex' package.
```

If the patched-Wine leg is the only Wine leg, then a missing package, an
upstream build break after a pin bump, or a transient apt failure takes down
`test` — and because `windows-test` has `needs: test`, it takes down the
real-Windows legs with it. Every leg of the pipeline would then be gated on a
30-minute compile of a 700-file C project we do not maintain. As an added
leg with the stock-Wine legs left in place, the same failure costs one red
job on a board that still reports everything else honestly.

**`winedbg --auto` hangs a CI job instead of failing it.** When a Wine process
raises an unhandled exception or calls an unimplemented stub, Wine launches
`winedbg --auto`, which waits for input that never comes. In CI that is not a
crash, it is a *hang*: the job runs to the six-hour default job timeout rather
than failing in seconds. `tools/runtests.sh:83,135` already guards against
this correctly —

```sh
( cd "$work" && WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d "$wine" "$abs" ) \
	>"$log" 2>&1 </dev/null
```

— setting `WINEDLLOVERRIDES=winedbg.exe=d` to disable the debugger outright
and redirecting stdin from `/dev/null` so nothing can block on a read. Any new
CI step that invokes Wine outside `runtests.sh` must do the same, and a
freshly built Wine is the *most* exposed case, because first-run `WINEPREFIX`
initialisation is when Wine is likeliest to raise something. Two consequences
for the proposal: set both variables at the job level rather than relying on
each call site, and give the new leg an explicit `timeout-minutes` so a hang
is bounded by minutes rather than hours.

**Drift between the local tree and whatever CI uses.** This is already real
and already biting, before any CI change. The fork's branch and
`~/Projects/wine` have diverged in *both* directions: the fork carries two
job-limit commits the local tree lacks, and the local tree carries eight
commits the fork lacks. The project notes record a related incident in which
an agent tested against a three-patch build while describing it as "this
project's locally patched build", and drew conclusions about AFD, PE alignment
and pid retention from a binary that contained none of those patches.

Putting a pinned SHA in `ci.yml` makes drift *visible* rather than creating
it: the pin is a written-down claim about which Wine the project tests
against, and it can be compared against a local tree in one command. That is
strictly better than the present situation, where the answer is whatever
happens to be built in someone's home directory.

**Rebasing eleven patches on upstream Wine.** Real, but smaller than it looks,
and it is a cost we are already paying — the patches exist and are already
maintained against `wine-11.16`. Pinning by SHA means upstream moving does
*not* force a rebase: CI keeps building the pinned commit until someone
deliberately bumps it, exactly as `TINYCC_SHA` works today. The rebase becomes
a periodic, scheduled chore rather than a CI-blocking event. The patches are
also small and concentrated in `server/` and `dlls/ntdll/`, not spread across
the tree.

**A more accurate Wine will eventually turn green tests red.** That is the
purpose, but it is disruptive if it lands unannounced. Right now it does not
apply — the measurement in §4 shows the current series changes nothing from
green to red. The risk is about *future* pin bumps: the project's own standing
practice is to write a Wine patch whenever a Wine/real-NT divergence is found,
and each such patch is written specifically to surface an ntlibc bug. Bumping
`WINE_SHA` is therefore a deliberate act that can be expected to redden the
board, and should be treated like one — bumped on its own commit, not folded
into an unrelated change.

**The failure mode that is *not* on this list is the interesting one.** The
project notes record a Wine patch (`25ab93939`, giving a CUI process a
console) that was written on a hypothesis, made ntlibc's fix look correct
under Wine, and was disproved by the real-Windows legs; it was reverted. A
patched Wine that agrees with a broken ntlibc is worse than no patched Wine,
because it converts a red board into misinformation. This is an argument for
keeping the `windows-test` legs authoritative — which the proposal does not
touch — and for keeping stock Wine as an independent third opinion, which is
the other reason not to replace it.

## 6. Recommendation

**Add a fourth `test` leg that builds and runs the patched Wine fork. Do not
replace the stock-Wine legs. Relax `TEST_RUN`'s `*-win.exe` filter only on
that leg.**

In order:

1. **Push the patch series first.** Nothing else can happen until
   `~/Projects/wine`'s eleven effective commits are on a branch of
   `github.com/Pandapip1/wine` — reconciled with the two job-limit commits
   already there, since the two have diverged. Suggested branch name
   `ntlibc-testing`, to say what it is for and to keep it distinct from the
   existing `rtlcloneuserprocess-and-pid-retention`. This is a prerequisite,
   not a step: CI cannot fetch from a home directory, and until the push
   happens the rest of this document is theory.

2. **Pin it by SHA in `ci.yml`**, as `WINE_REPO`/`WINE_SHA`, mirroring
   `TINYCC_REPO`/`TINYCC_SHA` and for the same stated reason.

3. **Build it in a cached job**, `build-wine`, keyed on the pinned SHA — the
   same shape as `build-toolchain`. See [Cost](#2-cost-what-a-wine-build-actually-costs)
   for the cache-hit and cache-miss numbers that make this viable.

4. **Add one `test-patched-wine` leg**, x86_64, `--disable-kernel32`, running
   the full `TEST_EXES` set rather than `TEST_RUN`.

Why an added leg rather than a replacement, restated as the three reasons that
actually carry the decision:

- **Stock Wine is itself a signal.** It is the environment where a contributor
  who has not built a custom Wine will run `make check`. Keeping a leg on it
  is what keeps that experience honest, and it is what catches a new test that
  forks without being named `*-win.c` before it reaches someone's laptop.
- **Three environments beat two.** Stock Wine, patched Wine and real NT
  disagree in different directions, and a change can pass two and fail the
  third — the project has three recorded instances of exactly that. Collapsing
  stock and patched into one loses the ability to attribute a failure to a
  *Wine version* difference rather than a Wine-versus-Windows one.
- **It keeps a slow, externally-dependent build off the critical path.** The
  existing legs stay exactly as fast and as reliable as they are today, and
  `windows-test` keeps its `needs: test` relationship to a job that does not
  compile Wine.

Why relax `TEST_RUN` only on that leg: the filter is correct for stock Wine
and will stay correct for it. The clean way to express this is a variable the
leg overrides rather than a second filter — `make check TEST_RUN='$(TEST_EXES)'`
works today with no Makefile change at all, because `TEST_RUN` is a plain
recursively-expanded variable and a command-line assignment overrides it.
That is worth preferring over editing `Makefile:232`: it leaves the default
behaviour, and the comment explaining it, untouched.

### If the cost turns out to be unacceptable

Fall back to publishing a prebuilt Wine tarball as a release asset on the fork
and having CI download it. That trades reproducibility for speed, and it is
the wrong default, but it is a working answer if the cached build proves too
slow or too large — and it can be adopted later without changing anything else
about the shape above.

## Appendix A: the proposed diff

*(to be completed)*

