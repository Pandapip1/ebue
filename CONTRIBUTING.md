<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to ntlibc

## NTDLL first, kernel32 only as a last resort

ntlibc's whole reason to exist is to be a C library that talks to Windows NT
directly through ntdll's `Nt*`/`Rtl*` routines, rather than going through
kernel32 (or any other DLL layered on top of it). Two rules follow from that:

1. **Use ntdll wherever a pure-ntdll path exists.** Before reaching for a
   kernel32 API, check whether the same effect is reachable through `Nt*`,
   `Rtl*`, or an `NtDll`-exported helper. It almost always is — kernel32
   itself is usually a thin wrapper over one of these. kernel32 may only be
   used for the few things that are genuinely kernel32 (or csrss/conhost)
   territory with no ntdll equivalent — e.g. registering a console
   Ctrl-C/Ctrl-Break handler, which has no `Nt*` counterpart.

2. **Every kernel32 reference must be compiled out unless explicitly
   requested.** Wrap it in `#ifdef NTLIBC_USE_KERNEL32` (see
   `src/internal/nt.h`/wherever the kernel32 prototypes for that call live)
   and provide a reasonable fallback behaviour for the `#else` path — not a
   build failure. `NTLIBC_USE_KERNEL32` is defined by `configure
   --enable-kernel32`/undefined by the default `--disable-kernel32` (see
   `configure`'s `KERNEL32` var and the `Makefile`'s `CFLAGS_ALL` rule for
   how the define is threaded through). This keeps a default build of
   ntlibc free of any kernel32 dependency at all, and makes every place
   that *isn't* ntdll-only easy to find by grepping for the macro.

When you add code that touches Windows and you're not sure whether it
belongs behind `NTLIBC_USE_KERNEL32`: if it's an `Nt*`/`Rtl*`/other-ntdll
call, it doesn't need the guard. If it's anything else (kernel32, advapi32,
etc.), it does, and it needs a real ntdll-only fallback path too.

## Why a shell lives in a libc repo

The first section above says ntlibc's whole reason to exist is to be a C
*library*, not a collection of userland programs — so `src/sh/` (the
parser/executor) and the `sh/` binary built on top of it need to justify
being here rather than in a separate project.

The justification is that `sh` isn't really a new feature: it's the fix
for three POSIX interfaces this library already ships and already gets
wrong. `system()` (`src/stdlib/system.c`), `popen()` (`src/stdio/misc.c`)
and `wordexp()` (`src/wordexp/wordexp.c`) are all specified in terms of
the Shell Command Language, and all three are degraded on this platform
for the same reason — there is no POSIX shell here, so the first two
hand the command to `cmd.exe` (a different, incompatible grammar) and the
third refuses command substitution outright. See `test/sh-design.md` for
the full design note, but the load-bearing decision is: the shell is
internal functions compiled into `libc.a`, `wordexp`/`system`/`popen`
call those functions directly instead of spawning an external
interpreter (which would mean trusting whatever `sh.exe` happens to be
first on a caller's `PATH`), and the `sh` binary is a thin `main()` over
the same code every other caller uses. It ships as its own source
directory and its own binary specifically so it stays visibly separate
from the ntdll-facing library code the rest of this file is about —
`src/sh/` is a self-contained command-language layer, not something
blurred into `src/stdlib/` or `src/wordexp/`.

Concretely: `src/sh/` is the engine (parser, executor) and is part of
`libc.a` like any other module; `sh/main.c` is the entry point and builds
to `obj/sh/sh.exe` (`make sh`, or `make all`, installed into
`$prefix/bin`). `test/sh-engine.c` tests the engine in-process;
`test/sh-main.c` tests the binary as a process. The binary refuses, with
a diagnostic naming what is unsupported, anything the engine would
misread rather than report — see `sh/main.c`'s header for the list and
`test/sh-design.md` for what is still to come.

## Other conventions

- SPDX header on every new file (this project is
  [REUSE](https://reuse.software/)-compliant; `reuse lint` checks it in CI):
  ```c
  /* SPDX-FileCopyrightText: (C) 2026 Gavin John
   * SPDX-License-Identifier: GPL-3.0-or-later */
  ```
  Use `#`/`;`/`<!-- -->` in place of `/* */` for files whose syntax doesn't
  support C comments. Vendored code (see `tools/install.sh`) keeps its
  original copyright holder and license instead.
- Comments explain non-obvious *why* (an NT quirk, a measured behavior
  difference, a rejected alternative and why it didn't work) — not what the
  code already says by being well-named. See `src/process/spawn.c` and
  `src/process/fork.c` for the tone this codebase uses.
- Build/test loop:
  ```
  ./configure --host=x86_64-win32 CC=x86_64-win32-tcc
  make -j4
  make -j4 check   # builds test/*.c and runs them under wine
  ```
  If you've touched anything behind `#ifdef NTLIBC_USE_KERNEL32`, also run
  `make check-kernel32` — it reconfigures the already-configured tree with
  `--enable-kernel32`, rebuilds from clean (required: `KERNEL32` feeds
  `CFLAGS_ALL`, and object files don't depend on `config.mak`'s contents,
  so a same-tree reconfigure without a clean silently keeps linking the
  old objects), runs `make check`, and then puts the tree back the way it
  found it.
- CI (`.github/workflows/ci.yml`) runs this loop under Wine on Linux
  runners for `i386-win32` and `x86_64-win32`, both built the default
  `--disable-kernel32` way, plus a third leg that builds `x86_64-win32`
  with `--enable-kernel32` so the guarded code actually gets compiled,
  run under Wine, and (via `windows-test`) re-run on real Windows — not
  a full arch × kernel32 matrix, since kernel32 support isn't itself
  arch-specific. It also cross-builds the same `test/*.c` binaries on
  Linux and then runs them on a real `windows-latest` runner (including
  the `*-win.c` tests that Wine can't run) — that job is the one that
  actually proves fork()/WOW64 behavior and kernel32 code paths against
  real Windows rather than Wine's emulation of it.
- `make asan`/`make fuzz` (native, Linux/ELF, see `tools/asan-build.sh`)
  never define `NTLIBC_USE_KERNEL32`: crt1.c calls `__signal_init()`
  unconditionally, so turning it on there would require `fuzz/ntstubs.c`
  to answer `LdrLoadDll`/`LdrGetProcedureAddress` for every test and fuzz
  binary, and even then nothing native can synthesize a real console
  control event to drive the handler it would install — that coverage
  comes from the Wine/Windows legs above instead.

## Dependency updates (Renovate)

`renovate.json5` configures [Renovate](https://docs.renovatebot.com/) to open
PRs when a pinned dependency moves. It manages two things:

- **GitHub Actions versions** in `.github/workflows/*.yml`. Non-major bumps
  are grouped into one weekly PR; majors get a PR each. Majors are the ones
  that bite: `actions/*` v4 moved to the deprecated Node 20 runtime, and a
  `cache/restore@v4` cannot read an entry written by `cache@v6` — it just
  misses silently and the job fails later, somewhere unrelated. When bumping
  a pair (`upload-artifact`/`download-artifact`, `cache`/`cache/restore`),
  bump every leg in lockstep.
- **The pinned tinycc commit**, `TINYCC_SHA`, tracked against the `mob`
  branch of TinyCC's repo via a `customManagers` regex rule (Renovate has no
  built-in manager for a bare SHA in an `env:` block). Checked monthly.

Nothing auto-merges, deliberately — every bump here can change build
behaviour. Before merging a **tinycc** bump specifically:

- `make -j4 check` is 15/15 for *both* `i386-win32` and `x86_64-win32`;
- `make generated` leaves no drift;
- the real-Windows CI leg passes, not just the Wine one;
- `TINYCC_SHA` ends up identical in `ci.yml` and `fuzz.yml` — fuzz.yml reads
  the toolchain cache entry ci.yml writes, keyed on that value, so a mismatch
  silently rebuilds tinycc from scratch every night;
- tinycc's own log between the old and new commit has no codegen or
  PE-output change that ntlibc would need to adapt to.

Renovate only runs once the GitHub App is enabled on the repository; the
config file alone does nothing.

## Linting (`make lint`)

The library is built with tcc, which is far more permissive than gcc or
clang — the two compilers people actually build ntlibc *against*. `make
lint` (i.e. `tools/lint.sh`) runs the checks tcc cannot:

```
make lint                        # everything that is installed
tools/lint.sh warn               # just the strict gcc/clang warning build
tools/lint.sh analyze            # just the clang static analyzer
LINT_CONVERSION=1 tools/lint.sh warn   # add -Wconversion -Wsign-conversion
```

It is strictly opt-in: nothing in the build depends on it, it is not part
of `make check`, and it is not a reason to put compiler-specific pragmas or
casts into `src/`. Findings get judged, and either fixed properly or left
alone with a note — not blanket-silenced. Any tool that isn't installed is
skipped with a message rather than failing.

`tools/lint.sh` documents the warning set and why particular checks are
excluded; `.clang-tidy` does the same for the clang-tidy check list, and
`tools/cppcheck-suppressions.txt` for cppcheck. A cross toolchain is *not*
required: clang is invoked with `--target=i686-w64-mingw32` /
`--target=x86_64-w64-mingw32`, which works with `-nostdinc` because the
build never touches the target's own headers. gcc falls back to a native
`-m32`/`-m64` pass (and says so) unless mingw-w64 gcc is installed.

Deliberately absent: any formatting enforcement. The house style here is
musl's, applied by hand; a reformat would be enormous churn for no
correctness gain and would wreck `git blame`.

## Sanitizers and fuzzing (`make asan`, `make fuzz`)

Wine runs the real library but sees only what the program itself notices.
A second, **native** build of the same `src/*.c` — Linux/ELF, clang,
AddressSanitizer + UBSan, and libFuzzer — sees the rest. It is not a
substitute for `make check`; it is a different instrument, and it needs no
cross toolchain.

```
make               # once, for obj/include/bits/alltypes.h
make asan          # native ASan+UBSan build, runs the applicable test/*.c
make fuzz          # builds fuzz/*, runs each harness for 60s
tools/fuzz.sh 300 strtod printf     # longer, selected harnesses
```

Three rules keep the results worth anything.

**The harnesses compile the real `src/*.c`.** Never copy library code into
a harness. A harness that tests a transcription of `printf.c` stops
testing `printf.c` the moment somebody edits `printf.c` — and a
transcription that models a bigger scratch buffer than the real one cannot
reproduce the real overflow.

**The file list is derived, not written down.** `tools/asan-build.sh`
compiles every `src/**/*.c` and keeps the ones clang accepts (202 of 205
today; the three are other-architecture files and the TEB accessor, listed
with reasons in `obj/asan/skipped.txt`). Add a source file and it is
covered without touching this tooling.

**The ntdll side is a stub, and the stubs are graded.** `fuzz/ntstubs.c`
stands in for ntdll, which is the one thing a native build cannot have.
Some of it is real (the heap, on ASan's allocator, so ntlibc's heap use is
redzone-checked; read/write; the clocks), some is plausible, and the rest
answers `STATUS_NOT_IMPLEMENTED`. Nothing in `src/` is modified or
conditionally compiled for it. Tests that need what the stubs do not
provide are skipped **by name with a reason** in `tools/asan-build.sh`, as
are the two that assert the LLP64 target ABI (`long` is 4 bytes there and 8
here). Everything else must pass: a genuine ASan or UBSan report fails the
run.

A sanitizer only sees corruption, so the harnesses that can also check for
*wrong answers* do. `fuzz/fuzz_strtod.c` parses every input a second time
with glibc's `strtod` and compares bit patterns —
`strtod("1e442")` returning NaN instead of infinity is a real defect that
no sanitizer would ever flag. `fuzz/host_oracle.c` is the only file built
against the host headers; it reaches glibc through `dlsym` (ntlibc's own
definitions are linked hidden, so they are not in the dynamic symbol table
and cannot be found that way) and does its own reporting, because
formatting a printf bug with the printf under test proves nothing.

**Leaks are checked, and the fuzzers are where that pays.** `make asan`
and `make fuzz` run with `detect_leaks=1`. Nothing needs suppressing:
ntlibc's `malloc` is `RtlAllocateHeap`, which `fuzz/ntstubs.c` answers with
ASan's own allocator, so LeakSanitizer sees every ntlibc block with a full
ntlibc stack. Set `NTLIBC_LEAKS=0` (make variable `LEAKS=0` for
`make -C fuzz run`) only to isolate some other failure.

This was off until it was measured, and the measurement is worth recording.
`sscanf` leaked a `BUFSIZ` block per call from the initial commit until
64ea74e, through a green `make check` the whole time; it was noticed only
because a fuzzer's peak RSS reached 28 GB. Rebuilt with the pre-fix
`src/stdio/scanf.c` and `detect_leaks=1`, LSan reports it on the first
`sscanf` call, naming `rd` → `vsscanf_impl` → `sscanf`. Switching it on
immediately found the same defect in the sibling function: `vxprintf_mem`
never freed the one-byte staging buffer `__ensure_buf` hands the throwaway
memory `FILE`, so every `sprintf`/`snprintf` leaked a byte.

Note *which* instrument found it. `make asan`'s native tests never
reported that leak: LSan runs from the at-exit hook, and the one native
test that uses `sprintf` heavily (`test/stdio`) aborts on a UBSan report
before it gets there. The fuzzers found it in seconds, because libFuzzer
checks for leaks after every input rather than once at exit. Prefer a
harness over a test when the question is "does this leak".

Valgrind was considered here and deliberately not added. It cannot see the
PE/Wine side either, so it would be a second native approximation; it does
not coexist with ASan, so it would need a third build of `src/*.c`; and the
one thing it offers that this tooling does not — uninitialised-read
tracking — is exactly the check that a `-nostdinc` libc linked against a
sanitizer runtime makes noisiest. If that check is wanted, MSan on this
same native build is the cheaper route, and it is still unbuilt work.

Two consequences of linking a libc against a libc, worth knowing before
you debug them:

- The library objects are built `-fvisibility=hidden` and the sanitizer
  runtime is the **shared** one. Otherwise ntlibc's `malloc` and `sysconf`
  end up in the executable's dynamic symbol table, and ld.so and ASan's own
  start-up call into an NT libc that has not been initialised yet.
- libFuzzer's corpus and crash-artefact files go through ntlibc's
  `open`/`stat`/`readdir`, which are stubs here, so the fuzzers run without
  an on-disk corpus. Each harness prints the input that failed, and
  libFuzzer prints the crashing unit as Base64. A reproducer worth keeping
  belongs in `test/` as a case in the matching `test/*.c`, not in a corpus
  directory.

## The kaem bootstrap build path (`boot/kaem/`)

`boot/kaem/build-x86_64.kaem` (and `build-i386.kaem`) is a second, alternate
way to build `lib/libc.a` and `lib/crt1.o` that does **not** use `make`,
does **not** use any real shell, and does **not** use a standalone binutils
`ar`. It exists for exactly one reason: ntlibc is meant to work as the libc
stage of a from-scratch ("full source bootstrap") toolchain bootstrap, in
the style of [live-bootstrap](https://github.com/fosslinux/live-bootstrap).
At the specific point in that chain right after Mes's `mescc` has produced
a working `tcc` (see live-bootstrap's `parts.rst`, the tinycc 0.9.26/0.9.27
sections), neither `make` nor `bash` exist yet -- they show up noticeably
later in the chain (around `make 3.82`/`bash 2.05b`). The only command
driver available at that point is
[`kaem`](https://github.com/oriansj/mescc-tools) from mescc-tools: it reads
one line at a time, splits it into arguments, and execs -- no loops, no
globbing, no functions, no pipes, and (importantly, and unlike a real
shell) no `>`/`<` redirection and no way to get a literal `$` past its
`${VAR}`/`$@` variable expander. `boot/kaem/build-*.kaem` is a flat,
fully-unrolled kaem script that gets ntlibc built under exactly those
constraints, so it can slot into a kaem-driven bootstrap stage the same way
Guix's `gzip-mesboot0` (see `commencement.scm`) is a special-cased,
minimal-tool build of gzip used only at the equivalent early point in
Guix's bootstrap, before gzip's own normal `./configure && make` build
becomes usable later in the chain once more of the toolchain exists.

**The normal `./configure && make` path above remains the one to use** for
regular development, CI, and for building ntlibc at any later point in a
bootstrap chain once `make`/`bash` are available (or on any regular Windows
or cross-compilation host). Reach for `boot/kaem/` only when driving ntlibc
through kaem itself, with nothing else on PATH but a win32-cross `tcc`,
`mkdir`, `cp`, `catm`, and kaem's own builtins. Those last three all come
from
[mescc-tools-extra](https://github.com/oriansj/mescc-tools-extra), the
companion package to mescc-tools (where kaem itself lives), so they are
already on hand wherever kaem is; `catm OUT IN...` is its minimal
concatenator, standing in for a shell's `cat IN... > OUT`. Nothing outside
that set is assumed -- in particular there is no `sed` and no `awk` at this
point in the chain, which is why the `bits/*.h.gen` pre-expansion described
below exists.

**Do not hand-edit `boot/kaem/build-*.kaem`.** It is a generated file,
regenerated straight from this Makefile's own build recipe so it cannot
silently drift out of sync as source files are added, removed, or renamed.
Regenerate it with:
```
./configure --host=x86_64-win32 CC=x86_64-win32-tcc
make kaem
```
`make kaem` (backed by `tools/gen-kaem.sh`) works by asking the real
Makefile what it would do -- `make -n -B lib/libc.a lib/crt1.o` -- and
mechanically rewriting that dry-run output into kaem-legal syntax: it
expands every `mkdir -p DIR` into the full chain of bare, parent-before-
child `mkdir` commands (kaem's assumed toolset may not have a `-p`-capable
`mkdir`; see the comments at the top of the generated file for the full
reasoning, including what mescc-tools-extra's own `mkdir` actually
supports), and it rewrites the `cat A B > obj/include/bits/alltypes.h`
step into a single `catm obj/include/bits/alltypes.h A B`, since kaem has
no `>` redirection. Everything else -- every compile command and the final
`tcc -ar` archiving step -- is carried through close to verbatim. See
`tools/gen-kaem.sh`'s own comments for the details of each workaround.

### `bits/alltypes.h` and the `*.h.gen` files

`bits/alltypes.h` is the one header the build has to *generate* rather than
copy. Its two halves -- the per-arch `arch/$(ARCH)/bits/alltypes.h.in` and
the shared `include/alltypes.h.in` -- are written in a compact
`TYPEDEF`/`STRUCT`/`UNION` DSL that `tools/mkalltypes.sed` expands into
`__NEED_`/`__DEFINED_`-guarded blocks. That expansion is a capture-group
rewrite, and *nothing* in the bootstrap toolset can perform one: there is
no `sed` and no `awk`, and mescc-tools-extra's `replace` does literal
substring substitution only. Compiling a helper on the spot is no escape
either, since the `tcc` on PATH there is a cross compiler emitting win32
PE, so whatever it builds cannot run on the build host.

So the expansion happens once, at development time, in
`tools/gen-alltypes.sh` (`make alltypes`), which writes a committed
`*.h.gen` next to each `*.h.in`. The bootstrap then only has to
concatenate the two halves, which is exactly what `catm` does. The normal
`make` build reads the same `*.h.gen` files (`cat A B > $@`), so there is
one expansion and one source of truth rather than sed-for-make and
catm-for-kaem.

**`*.h.in` is the file you edit; `*.h.gen` is generated -- do not hand-edit
it either.** The split is safe because `mkalltypes.sed`'s rules are all
purely per-line (no hold space, no range addresses), so expanding the
halves separately and concatenating is byte-identical to concatenating and
then expanding.

### Keeping the generated files honest

`./configure` enables a tracked pre-commit hook (`.githooks/pre-commit`,
via `git config core.hooksPath .githooks`) that regenerates both kinds of
generated-and-committed file and blocks the commit if anything changes, so
neither can drift silently into a commit. `make generated` does the same
two regenerations by hand:
```
make generated   # == make alltypes + make kaem
```
The hook also keeps the two gap reports honest -- see "Keeping the gap
reports honest" below, which is a different problem: those go stale from
commits that never touch them.
CI runs the same pair and `git diff --exit-code`s the results (the
"Regenerate generated files and check for drift" step). If you're
committing without having run `./configure` in this checkout, enable the
hook by hand with the same `git config` line, or just run `make generated`
yourself before committing.

### Merging/rebasing/cherry-picking across a source-file add

`boot/kaem/build-i386.kaem` and `build-x86_64.kaem` are generated from the
Makefile's own list of source files (see above), so two branches that each
add a *different* source file conflict textually in these two files even
though there is nothing really in conflict -- the right result is neither
side, but a fresh `make kaem` against the merged tree. Resolving that by
hand, the same mechanical way every time (take either side, `make kaem`,
`git add boot/kaem/`), is exactly the kind of resolution most likely to go
wrong under pressure: conflict markers have twice ended up committed
straight into these files, which is particularly nasty since nothing
compiles them until a from-scratch kaem bootstrap actually runs (see
`.githooks/pre-commit`'s conflict-marker check, added for that reason).

`./configure` also registers a `git` merge driver
(`tools/merge-kaem.sh`, wired up via `.gitattributes`) that resolves a
conflict on either `.kaem` file instead of leaving a text-merge conflict
-- during `git merge`, `git rebase`, *and* `git cherry-pick` alike, since
this project's own workflow uses cherry-pick the most.

It does **not** work by calling `tools/gen-kaem.sh` against the live
worktree, even though that was the first thing tried: git's default merge
backend (`ort`) computes an entire `merge`/`rebase`/`cherry-pick` in
memory and does not write *any* path -- not even one that merged with no
conflict at all -- to the real index or working tree until every path,
including every custom merge driver's own invocation, has finished. A
sibling source-file addition from the other side of the same operation is
therefore never actually on disk yet when this driver runs, so a live
regeneration silently drops it: no conflict markers, exit 0, and a wrong
`boot/kaem/*.kaem` -- exactly the "plausible but wrong" failure this
driver exists to prevent, and not a rare timing fluke, since it reproduces
on every conflict that also touches a sibling path. It also is not caught
downstream: git does not run `.githooks/pre-commit` for a commit that
`cherry-pick`/`merge` makes on its own without a human needing to resolve
anything by hand, so the hook's own `make kaem` drift check -- which has
exactly the same live-tree blind spot anyway -- never even gets a chance
to run.

Instead, `tools/merge-kaem.sh` reruns `git merge-file` on the three
versions of the conflicting file git already hands it (the only inputs a
merge driver is ever guaranteed to have, regardless of what stage the
rest of the operation is at) and resolves each resulting hunk using what
is known about the file's own structure: an independent single-line
insertion from each side (a new compile command or `mkdir` line, at the
same point in an already-sorted list) is reordered and kept; the one
`${CC} -ar rcs lib/libc.a ...` line -- which is rewritten differently by
each side on *every* source-file add, since it lists every object on one
line -- is resolved with a token-level three-way union instead of a text
merge. Anything else is left as a normal conflict for a human; this
driver never guesses at a hunk shape it does not recognize. See
`tools/merge-kaem.sh`'s own header for the full story (including how this
was confirmed empirically) and the exact two shapes it handles.

This needs no source tree, no compiler, and no `config.mak` -- it is a
pure function of the three blobs git already passed it. If you're merging
without having run `./configure` in this checkout, enable the driver by
hand with the same `git config` line `./configure` uses:
```
git config merge.ntlibc-kaem.driver 'tools/merge-kaem.sh %O %A %B %P'
```

### The two gap maps: one engine, two backends

`test/POSIX-GAP-MAP.generated.md` and `test/LIBC-TEST-MAP.generated.md`
are generated, committed, and each has a gate stage checking it is not
stale. They are produced by two backends over one engine:

| | |
|---|---|
| `tools/suitemap-engine.sh` | the engine. Sourced, never run. Everything that decides what a number *means*. |
| `tools/posix-gapmap.sh` | backend: LTP's Open POSIX Test Suite, 1610 tests |
| `tools/libc-test-map.sh` | backend: musl's `libc-test`, 146 tests |

They were once two near-identical ~1300-line scripts, and that was a
correctness problem rather than a tidiness one: every property either
tool established had to be established *again* in the other, and the
second time is where it quietly was not. Three real instances are named
in the engine's header — a locale bug that shipped twice and was found
once, a refuse-to-measure guard that was reasoned in one and a bare
one-line `die` in the other, and two *different* greedy algorithms under
the same section heading.

So: anything that decides what a number means lives in the engine, once.
A backend describes its suite — how to enumerate it, how to classify one
test, what its report says — and nothing else.

**A backend may not compile anything itself.** Every compile goes through
`sm_cc`, and `sm_cc` refuses to run until `sm_require_built` has passed.
That guard is the most important thing in either tool: without
`lib/libc.a` every test fails to link, every test classifies as blocked,
and the report reads as a *total gap* — a build error wearing the costume
of a measurement. Making it unskippable rather than merely present is the
point. Where a backend compiles in parallel the worker is a separate
process, so it carries the same assertion inline via `SM_WORKER_GUARD`.

### Keeping the gap reports honest

They go stale for a reason `boot/kaem/` and the `*.h.gen` headers do not:
not because someone edited them, but because a header or a symbol landed
*somewhere else in the tree*. `spawn.h` landing put both stages red from
two commits that touched neither file.

**The reports carry their own data.** Each ends with an HTML comment
holding the rows it was rendered from. Everything above it — the greedy
closure, the ledger, the reconciliation, every count in the prose — is a
pure function of those rows. So there are two operations, costing very
differently (measured on a 24-core box at `GAPMAP_JOBS=2`):

| | what it does | needs | posix-gapmap | libc-test-map |
|---|---|---|---|---|
| `make posix-gapmap` / `make libc-test-map` | **measures** the tree | the submodule, `config.mak`, a build | 8.9s cold, 2.7s warm | 1.2s cold, 0.2s warm |
| `tools/…  --render` | **re-renders** from the rows it already carries | nothing at all | 2.8s | 0.2s |
| `make …-check` | measures, and diffs against the checked-in file | as the first row | as the first row | as the first row |

The invariants — census, partition, both floors, the canaries — live on
the *rendering* side, so they fire on `--render` too. That is deliberate:
if they only ran during a measurement, the cheap path would be a way to
launder a broken or truncated set of rows past every one of them.

**The pre-commit hook** re-renders both reports on every commit and
re-measures both whenever anything staged could possibly have moved a
number — which is everything except `*.md`, `LICENSES/`, `.github/` and
`.githooks/`. It states that as an *exemption* list on purpose: being
wrong in the direction of spending the seconds is cheap, being wrong the
other way is a stale report. Costs 3.2s on a commit that cannot move a
number, 8.6–15.2s on one that can.

If a submodule is not checked out the hook **does not skip quietly**: it
re-renders, and says in as many words that the report was *not* measured.
"I could not check" and "it checks out" are different claims.

### The cache

Measuring is not cheap and the hook has to pay for it, so the analysis
half is cached under `.suitemap-cache/` (gitignored, per-machine, safe to
delete — the next run just pays full price). `SUITEMAP_CACHE=0` disables
it; `SUITEMAP_CACHE_DIR` relocates it.

What is cached is the whole **analysis**, not just compiled objects.
Profiling posix-gapmap's 8.5s said compiling the 1610 tests is only 35%
of it; absent-header resolution and the class B/ledger work are the other
57%. Caching "the compiled artefacts" would have left two thirds on the
table.

**The key is the entire design.** A cache keyed on something that does
not fully determine its value serves a stale answer silently, and a
silently stale gap measurement is strictly worse than no cache — it is
the "good news shaped like a broken instrument" failure these tools exist
to prevent, with a performance optimisation as its cause. Six components,
hashed separately so a miss can be *explained* rather than guessed at:

| component | covers | why it must be in the key |
|---|---|---|
| `cc` | `$CC` **and its version banner** | a compiler rebuilt in place is invisible to a name-only key |
| `flags` | `CFLAGS_C99FSE`, `CFLAGS_AUTO`, the `-I` set | changes what compiles |
| `headers` | `include/`, `arch/`, `obj/include/` | decides class A |
| `libc` | `lib/libc.a` by content | decides B from C |
| `suite` | the suite's *shared* material only | so a submodule bump recompiles what changed |
| `tool` | the engine and the backend | the classification rules live there |

A miss prints which components moved. If you are ever suspicious of it,
`SUITEMAP_CACHE=0` and compare: cached and uncached output are required
to be byte-identical, and that equivalence is how the cache was
validated in the first place — each of the six inputs was *changed in
turn* and the answer checked, rather than reading the key back and
concluding that the code which computes it ran.

`lib/libc.a` is byte-reproducible across an identical rebuild, which is
what makes this cache useful rather than theoretical: a no-op `make`
still hits.

### Keeping the gap reports honest

`test/POSIX-GAP-MAP.generated.md` (`tools/posix-gapmap.sh`) and
`test/LIBC-TEST-MAP.generated.md` (`tools/libc-test-map.sh`) are
generated and committed too, and the gate has a stage for each one's
staleness. They go stale for a reason `boot/kaem/` and the `*.h.gen`
headers do not: not because someone edited them, but because a header or
a symbol landed *somewhere else in the tree*. `spawn.h` landing put both
stages red from two commits that touched neither file, and the authors
had no way to know until CI said so.

**The reports carry their own data.** Each one ends with an HTML comment
holding the rows it was rendered from -- one per conformance test, one
per interface directory, one per class B name, plus a few scalars.
Everything above it (the greedy closure, the ledger, the reconciliation,
every count in the prose) is a pure function of those rows. So there are
two different operations on these files, and they cost very differently:

| | what it does | needs | cost (24 cores) |
|---|---|---|---|
| `make posix-gapmap` / `make libc-test-map` | **measures** the tree: compiles 1610 OPTS tests / builds musl's corpus, and writes new rows | the submodule, `config.mak`, a build | ~6.0s / ~1.3s |
| `tools/posix-gapmap.sh --render` / `tools/libc-test-map.sh --render` | **re-renders** the report from the rows it already carries | nothing at all | ~2.8s / ~0.3s |
| `make posix-gapmap-check` / `make libc-test-map-check` | measures, and diffs against the checked-in file | as the first row | as the first row |

The invariants -- census, partition, both floors, the canaries -- live on
the *rendering* side, so they fire on `--render` too. That is deliberate:
if they only ran during a measurement, the cheap path would be a way to
launder a broken or truncated set of rows past every one of them.

**The pre-commit hook** re-renders both reports on every commit (cheap,
and it catches a hand-edit or a renderer change), and re-measures both
whenever anything staged could possibly have moved a number -- which is
everything except `*.md`, `LICENSES/`, `.github/` and `.githooks/`. It
states that exemption as an exemption on purpose: being wrong in the
direction of spending the seconds is cheap, and being wrong the other way
is a stale report.

If a submodule is not checked out the hook **does not skip quietly**: it
re-renders, and says in as many words that the report was *not* measured
and that CI will catch it if this commit moved a number. "I could not
check" and "it checks out" are different claims.

### Merging a generated report

`test/*.generated.md` is routed through a second merge driver,
`tools/merge-gendata.sh` (`.gitattributes`, `merge=ntlibc-gendata`),
registered by `./configure` alongside the `boot/kaem/` one. By hand:
```
git config merge.ntlibc-gendata.driver 'tools/merge-gendata.sh %O %A %B %P'
```

The reason is the same as for `boot/kaem/` and sharper: these files are
almost entirely derived, so two branches that each land a header change
*every number in both files*, and a three-way text merge of that is not
merely conflict-prone but meaningless -- the correct result is neither
side's text.

It reaches a different answer from `tools/merge-kaem.sh`, for a reason
worth stating. `merge-kaem.sh` had to reconcile generated *text*, hunk
shape by hunk shape, because `boot/kaem/*.kaem` has no separable data
layer -- the text is all there is. These reports do have one, so this
driver merges the **rows**, key by key, with the textbook three-way rule
(one row per test, per directory, per name, per scalar, and the key is
unique within a version), and then re-renders the report from the merged
rows using the generator's own `--render FILE` mode. That is a real
resolution rather than a text reconciliation.

What it does **not** do is regenerate from the worktree, for exactly the
reason `merge-kaem.sh`'s header documents at length: git's `ort` backend
writes nothing to the index or working tree -- not even a sibling path
that merged cleanly -- until every path's merge has finished, so a live
regeneration silently measures a tree missing whatever the other side
added. Here it would be worse, since the generators also need their
submodule and a build, neither of which a mid-rebase clone is guaranteed
to have. Both the row merge and `--render` are pure functions of the
three blobs git hands the driver, which is the only input a merge driver
can rely on.

And it does not claim more than it can check. Interacting *header*
additions turn out to be the easy case: land `semaphore.h` on one branch
and `sys/mman.h` on another, and the six tests blocked by both have their
absent-header set reduced differently on each side, so those rows
diverge, the driver marks the path conflicted and asks for a
regeneration. (Measured: the row merge said `A=730` where measuring the
merged tree said `A=728`, and it did not publish that quietly.)

What it cannot see is an interaction one level *down*, in the library
rather than the headers: a class B test needing two symbols, one added on
each branch. All three versions of that row read `B`, nothing diverges,
and the merged report is individually right and jointly stale. So the
driver stamps the recorded ntlibc SHA `unknown`. Both tools' `--check`
verifies that SHA is an ancestor of `HEAD`, so `unknown` fails loudly by
an existing, self-tested path; the merge itself completes with no
conflict markers anywhere; and the pre-commit hook treats `unknown` as a
reason to re-measure, so the ordinary case clears the beacon on the very
next commit without anyone needing to know it was lit.

Where rows were changed *differently on both sides*, the driver takes
this side's value -- so the file stays valid and marker-free -- and exits
non-zero so git records the path as conflicted. Regenerate and `git add`
it. It never guesses at a divergence, the same contract
`merge-kaem.sh` uses for a hunk shape it does not recognize.
