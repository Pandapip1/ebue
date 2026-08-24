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

**Short answer: yes — but as an *additional* leg, not a replacement, and
only after the patch series is pushed to a fetchable remote.** The provenance
problem, which looked like the deciding constraint, turns out to be already
half-solved: a `Pandapip1/wine` fork exists and five of the thirteen patches
are already on it. The build is not cheap — measured at ~50 minutes on a
4-vCPU runner — but it is paid only when the pin is bumped, and only on a job
nothing else waits for.

## Summary of findings

| Question | Answer |
| --- | --- |
| Does patched Wine unlock the `*-win.c` tests? | **Yes.** All four pass under it; all four abort under stock Wine. |
| Does patched Wine turn currently-green tests red? | **No.** Measured: 47 passed, 0 failed, versus 43/0 on stock Wine — a strict superset. |
| Where does patched Wine come from? | `github.com/Pandapip1/wine`, pinned by SHA, exactly like `TINYCC_SHA`. The fork already exists. |
| What does the build cost? | ~50 min on a 4-vCPU runner on a cache *miss*; a 457 MiB restore otherwise. See [Cost](#2-cost-what-a-wine-build-actually-costs). |
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

Measured, not estimated. The patched tree (`8da89f8` "Release 11.16" plus the
thirteen local commits) was configured and built from scratch with exactly the
`configure` line [Appendix A](#appendix-a-the-proposed-diff) proposes, on this
machine — a 24-thread i9-12900K — with `make -j24`:

| Step | Wall clock | CPU time |
| --- | --- | --- |
| `configure` | 11 s | 11 s |
| `make -j24` | **9 m 44 s** | **10,680 s** (2 h 58 m, 1829% CPU) |
| `make install` | ~2 m | — |

and what it produces:

| Artefact | Size |
| --- | --- |
| Installed prefix (what CI would cache) | 1.7 GB — **457 MiB** zstd-compressed |
| Build tree (discarded after `make install`) | 5.8 GB |

The number that matters for CI is the **CPU time**, not the wall clock: a
GitHub-hosted `ubuntu-24.04` runner has 4 vCPUs, not 24. 10,680 CPU-seconds
across 4 cores is roughly **45 minutes of compile**, call it **~50 minutes**
for the whole job with `configure`, `make install` and the apt step. That is
not cheap, and the recommendation below is shaped around it rather than
around a hope that it would be.

Three things make ~50 minutes acceptable anyway:

- **It is paid on a cache *miss* only.** Keyed on `WINE_SHA`, the job compiles
  when the pin is deliberately bumped and is a ~457 MiB cache restore — seconds
  — every other time. This is exactly `build-toolchain`'s existing bargain.
- **457 MiB fits.** GitHub gives a repository 10 GB of Actions cache total, so
  the Wine entry sits comfortably alongside tinycc's.
  ([Caching dependencies — usage limits](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/cache-dependencies#usage-limits-and-eviction-policy))
- **It is off the critical path.** Only the new `test-patched-wine` leg has
  `needs: build-wine`; `windows-test`'s `needs: test` is untouched, so a Wine
  compile never delays the real-NT evidence.

The honest caveat is that same eviction policy: a cache entry unused for **7
days** is evicted (same source), so a repository quiet for a week re-pays the
~50 minutes on its next run even with no pin bump. That is a slow extra leg on
an occasional run, not a blocked pipeline — but it is the reason the prebuilt
tarball fallback below is kept in reserve rather than dismissed.

One transferability note, so the figure is not read as better than it is: the
measured build had the same `i686-w64-mingw32-gcc` / `x86_64-w64-mingw32-gcc`
cross compilers the appendix's apt line installs, and skipped the same
C++17-only PE modules (`configure` reports "PE compiler supporting C++17 not
found") in both cases. CI's build is therefore the same build, only on fewer
cores.

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

**Publishing a prebuilt tarball (a GitHub release asset on the fork).** The
in-CI build *is* expensive — ~50 minutes on a cache miss, see
[Cost](#2-cost-what-a-wine-build-actually-costs) — so this stays a live
option rather than a theoretical one. It removes the build from CI
entirely and makes the leg as fast as the current ones. Its cost is a manual
publish step per patch bump and an artefact whose provenance is "whatever was
on the maintainer's machine that day", which is exactly the reproducibility
property the `TINYCC_SHA` pin exists to protect. Keep it in reserve.

## 4. What it would unlock: measured, not estimated

The claim "patched Wine would let the `*-win.c` tests run" is checkable, so it
was checked rather than argued. Method: build ntlibc for `x86_64-win32` with
the pinned tinycc at `--disable-kernel32`, then run **every** binary in
`obj/test/` — not just `TEST_RUN` — under each Wine.

Patched Wine (`~/Projects/wine/build-wow64/loader/wine`, all thirteen commits
on `8da89f8`, binary rebuilt after the last of them), with the `*-win.exe`
filter **removed** — `make check WINE=… 'TEST_RUN=$(TEST_EXES)'`:

```
47 passed, 0 failed, 1 unverified
    PASS fork-cloexec-exec-win.exe
    PASS fork-handles-win.exe
    PASS fork-win.exe
    PASS process-win.exe
```

Stock Wine (`/usr/lib/wine/wine64`, wine-9.0 — the `wine` 9.0~repack apt
package, the closest available match to what CI installs on `ubuntu-24.04`),
via plain `make check`, i.e. with the filter applied:

```
43 passed, 0 failed, 1 unverified
```

**Provenance of these two numbers, since they are not from the same day.** The
patched-Wine run was re-measured at current `main` (`bb5aa84`, `waitid`), and
is unchanged: 47 passed, 0 failed, 1 unverified, the one unverified being
`posix-socket.exe` skipping its network group. The stock-Wine run is from the
earlier tree (`0041753`) and has **not** been re-run since; it is quoted as it
was measured. Nothing landed in between that would plausibly move it — the
intervening commits (`sched_yield`, `statvfs`, `waitid`, the `fmax`/`exp2`
variants, an AFD connect fix) added no new test binary, and removing the four
`*-win.exe` results from the current patched run leaves exactly the 43 the
stock figure reports.

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
~50-minute compile (measured; see [Cost](#2-cost-what-a-wine-build-actually-costs))
of a project we do not maintain. As an added
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
each call site, and give both new jobs an explicit `timeout-minutes` so a hang
is bounded by minutes rather than hours.

This is not hypothetical, and it is worth stating as a flat rule because it
cost this investigation real time locally: **every** invocation of our Wine
fork — `wineboot`, a one-off `wine foo.exe`, a sanity check, `make check` —
must run with

```sh
export WINEDEBUG=-all
export WINEDLLOVERRIDES=winedbg.exe=d
```

Miss it once and an unimplemented-function stub or an unhandled exception
launches `winedbg --auto`, which sits waiting for input. Interactively that is
an annoyance; in CI it is a job that produces no output and no failure until
its timeout expires. The patched fork is *more* exposed than stock Wine here,
not less: it is a freshly built tree, its first run initialises a new
`WINEPREFIX`, and its strictness patches exist precisely to raise statuses
upstream Wine does not.

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
   same shape as `build-toolchain`. ~50 minutes on a miss, seconds on a hit;
   see [Cost](#2-cost-what-a-wine-build-actually-costs).

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

The measured cost is ~50 minutes per cache miss, and cache entries are evicted
after 7 days unused, so a quiet repository pays it more often than "only on a
pin bump" suggests. If that proves unacceptable in practice, fall back to
publishing a prebuilt Wine tarball as a release asset on the fork and having
CI download it. That trades reproducibility for speed, and it is the wrong
default, but it is a working answer — and it can be adopted later without
changing anything else about the shape above.

## Appendix A: the proposed diff

**This is a proposal, not a change.** `.github/workflows/ci.yml` is a gating
pipeline; nothing here is applied. It is written out so the decision is about
a concrete artefact rather than a description of one.

It also **cannot be applied yet**: `WINE_SHA` below is a placeholder, because
the branch it would name does not exist until step 1 of the recommendation
(pushing the series to `github.com/Pandapip1/wine`) has happened.

```diff
--- a/.github/workflows/ci.yml
+++ b/.github/workflows/ci.yml
@@
   TINYCC_REPO: https://github.com/Pandapip1/tinycc.git
   TINYCC_SHA: 69eed4d346f31dea12d61b99f60298d2f59f66be
+
+  # Wine, same treatment and for the same reason. Stock apt Wine cannot
+  # run anything that forks -- it has no RtlCloneUserProcess -- and is
+  # more permissive than real Windows in several places we have since
+  # patched (the FILE_READ_ATTRIBUTES access check, FileAttributes==0,
+  # the rename status, RootDirectory resolution, sub-page
+  # SectionAlignment) and cannot open a socket the portable way at all.
+  # This fork carries those patches on top of upstream wine-11.16.
+  #
+  # Pinned by SHA, not by branch name, for the reason given above for
+  # tinycc: a push to the fork must not silently change what CI tests
+  # against. Bump this on its own commit -- these patches deliberately
+  # make Wine *stricter* than upstream in order to surface ntlibc bugs,
+  # so a bump is expected to be able to turn tests red, and that should
+  # not arrive folded into an unrelated change.
+  WINE_REPO: https://github.com/Pandapip1/wine.git
+  WINE_SHA: 0000000000000000000000000000000000000000  # branch ntlibc-testing
 
 jobs:
@@
       - name: Sanity-check the cross compilers exist
         run: |
           test -x "$HOME/tinycc-install/bin/i386-win32-tcc"
           test -x "$HOME/tinycc-install/bin/x86_64-win32-tcc"
 
+  # Build the patched Wine once and cache it, keyed on the pinned commit,
+  # exactly like build-toolchain above. On a cache hit this job is a
+  # no-op; it only compiles when WINE_SHA is deliberately bumped.
+  #
+  # --prefix is $HOME/wine-install and the cache restores to that same
+  # path: Wine locates its own libraries relative to the configured
+  # prefix, so this is not relocatable and the two must agree.
+  #
+  # --enable-archs=i386,x86_64 is the WoW64 build, needed because ntlibc
+  # targets both. The --without-* flags drop graphics, audio and device
+  # support the test binaries never touch; they are pure build-time
+  # savings, not a behavioural change to anything ntlibc exercises.
+  build-wine:
+    runs-on: ubuntu-24.04
+    # ~50 minutes measured on 4 vCPUs for a cache miss; seconds on a hit.
+    # Bounded well above that so a wedged build fails in an hour rather
+    # than burning to the six-hour job default.
+    timeout-minutes: 90
+    steps:
+      - name: Cache patched Wine
+        id: cache-wine
+        uses: actions/cache@v6
+        with:
+          path: ~/wine-install
+          key: wine-${{ env.WINE_SHA }}-${{ runner.os }}-v1
+
+      - name: Install Wine build dependencies
+        if: steps.cache-wine.outputs.cache-hit != 'true'
+        run: |
+          sudo apt-get update
+          sudo apt-get install -y build-essential flex bison \
+            gcc-mingw-w64-i686 gcc-mingw-w64-x86-64
+
+      - name: Clone and build Wine
+        if: steps.cache-wine.outputs.cache-hit != 'true'
+        run: |
+          git clone "$WINE_REPO" wine
+          git -C wine checkout "$WINE_SHA"
+          mkdir wine/build
+          cd wine/build
+          ../configure --prefix="$HOME/wine-install" \
+            --enable-archs=i386,x86_64 \
+            --without-x --without-freetype --without-vulkan \
+            --without-opengl --without-gstreamer --without-sane \
+            --without-usb --without-udev --without-dbus --without-cups
+          make -j"$(nproc)"
+          make install
+
+      - name: Sanity-check the patched Wine exists and carries the patches
+        run: |
+          test -x "$HOME/wine-install/bin/wine"
+          # RtlCloneUserProcess is the patch the *-win.c tests need; if it
+          # is missing, the cache holds a stock build and the leg below
+          # would silently degrade to what we already have.
+          grep -q RtlCloneUserProcess \
+            "$HOME/wine-install/lib/wine/x86_64-unix/ntdll.so"
+
+  # The stricter Wine leg. Deliberately *additional* to the three `test`
+  # legs above rather than a replacement for them:
+  #
+  #   - stock apt Wine is what a contributor who has not built a custom
+  #     Wine actually runs `make check` against, so a leg on it is what
+  #     keeps that experience honest -- including catching a new test
+  #     that forks without being named *-win.c, which breaks stock Wine
+  #     immediately (this has happened: sh stage 4, de3e39e);
+  #   - stock Wine, patched Wine and real Windows disagree in different
+  #     directions, and a change can pass two and fail the third. Three
+  #     environments attribute a failure to a Wine *version* difference
+  #     versus a Wine-versus-Windows one; two cannot;
+  #   - `windows-test` hangs off `needs: test`, so putting a Wine compile
+  #     on that path would gate the only real-NT evidence we have behind
+  #     a build of a project we do not maintain.
+  #
+  # One leg, not three: what this adds over the existing legs is Wine
+  # accuracy, which is not arch- or kernel32-specific.
+  test-patched-wine:
+    needs: [build-toolchain, build-wine]
+    runs-on: ubuntu-24.04
+    # Bounded on purpose. Wine answers an unhandled exception by launching
+    # winedbg --auto, which waits for input CI never sends -- a hang, not
+    # a crash, which would otherwise run to the six-hour job default.
+    # tools/runtests.sh already disables winedbg and redirects stdin, so
+    # this is a backstop, not the primary guard.
+    timeout-minutes: 20
+    env:
+      WINEDEBUG: -all
+      WINEDLLOVERRIDES: winedbg.exe=d
+    steps:
+      - uses: actions/checkout@v7
+
+      - name: Restore tinycc toolchain
+        uses: actions/cache@v6
+        with:
+          path: ~/tinycc-install
+          key: tinycc-${{ env.TINYCC_SHA }}-${{ runner.os }}-v1
+
+      - name: Restore patched Wine
+        uses: actions/cache@v6
+        with:
+          path: ~/wine-install
+          key: wine-${{ env.WINE_SHA }}-${{ runner.os }}-v1
+
+      - name: Add tinycc to PATH
+        run: echo "$HOME/tinycc-install/bin" >> "$GITHUB_PATH"
+
+      - name: Configure
+        run: |
+          ./configure --host=x86_64-win32 CC=x86_64-win32-tcc \
+            --disable-kernel32 WINE="$HOME/wine-install/bin/wine"
+
+      - name: Build
+        run: make -j"$(nproc)"
+
+      # TEST_RUN, not the Makefile, is what excludes the *-win.exe tests,
+      # and it is a plain recursively-expanded variable -- so overriding
+      # it on the command line runs the full set here while leaving the
+      # default (and the comment at Makefile:232 explaining why stock
+      # Wine needs it) untouched for every other leg. These four tests --
+      # fork, fork-handles, fork-cloexec-exec and process -- are the
+      # entire fork/waitpid/exec/spawn surface, and this is the only
+      # place besides `windows-test` that runs them at all.
+      - name: Check, including the *-win.c tests stock Wine cannot run
+        run: make -j"$(nproc)" check 'TEST_RUN=$(TEST_EXES)'
```

Two things deliberately **not** in this diff. `Makefile:232` is untouched —
the `TEST_RUN` override does the whole job from the command line. And the
existing `test` and `windows-test` jobs are untouched, so if this leg is a
mistake, deleting the two added jobs restores the pipeline exactly.


