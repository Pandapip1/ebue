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
- CI (`.github/workflows/ci.yml`) runs this loop under Wine on Linux
  runners for both `i386-win32` and `x86_64-win32`, for fast feedback.
  It also cross-builds the same `test/*.c` binaries on Linux and then
  runs them on a real `windows-latest` runner (including the `*-win.c`
  tests that Wine can't run) — that job is the one that actually proves
  fork()/WOW64 behavior and kernel32 code paths against real Windows
  rather than Wine's emulation of it.

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
CI runs the same pair and `git diff --exit-code`s the results (the
"Regenerate generated files and check for drift" step). If you're
committing without having run `./configure` in this checkout, enable the
hook by hand with the same `git config` line, or just run `make generated`
yourself before committing.
