<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# A `sh` for ntlibc: why, and how it links

Design note, not an implementation. Records a decision so the work can be
picked up without re-deriving it.

## Why a libc project is growing a shell

Three POSIX interfaces are specified *in terms of a shell*, and all three
are currently degraded for the same single reason — there is no POSIX
shell on this platform:

- **`system()`** (`src/stdlib/system.c`) hands the command to `%ComSpec%`,
  i.e. `cmd.exe`, whose grammar is not the Shell Command Language.
- **`popen()`** (`src/stdio/misc.c`) does the same, and its header comment
  documents the substitution.
- **`wordexp()`** (`src/wordexp/wordexp.c`) refuses command substitution
  outright with `WRDE_CMDSUB`, because `cmd.exe` cannot parse `$(...)`.

That is one missing component, not three problems. Building it fixes all
three at the source rather than accumulating three accommodations.

## The reuse rule, which is the whole point

**The shell is a set of internal functions compiled into `libc.a`. The
`sh` binary is a thin `main()` over them. `wordexp`/`system`/`popen` call
those functions directly and never spawn an external interpreter.**

Concretely, that rules out the alternative that was on the table: having
`wordexp()` find `sh.exe` on `PATH` via `__find_program()` and spawn it.
Reasons the in-process route wins:

- **No `PATH` hijacking vector.** Discovering an interpreter on `PATH`
  means executing whatever `sh.exe` a caller's `PATH` happens to reach
  first, with whatever the caller passed. `include/ntlibc/rpath.h`
  documents the same class of problem for `$ORIGIN` DLL search and
  resolves it by never accepting an unqualified name; the shell should
  not reintroduce it.
- **No external dependency.** Command substitution either works
  everywhere or nowhere, rather than depending on whether msys2 or
  Git-for-Windows happens to be installed.
- **Costs nothing to programs that do not use it.** `libc.a` is an
  archive: a member is pulled in only to satisfy an undefined symbol, so
  a program that never calls `system`/`popen`/`wordexp` never links the
  shell at all. (This is the same archive-extraction property that forces
  `crt1.o` to be a separate object rather than an archive member — see
  the top-level `Makefile`.)

## The subtlety: "in-process" is about linkage, not isolation

Calling into the shell directly does **not** mean running the command in
the caller's process context. POSIX requires command substitution to run
in a subshell environment, and `system()` to behave as if `fork()`ed. So:

- *Parsing and expansion* happen in-process, in linked-in code.
- *Execution* still forks/spawns exactly where the standard requires it,
  using the existing `__spawn`/`fork`/`waitpid` machinery.

What is avoided is a second interpreter **image** on disk, not the
process boundaries the specification demands. Anything that would let a
substituted command clobber the caller's fds, cwd, signal dispositions or
`environ` is a bug.

## Scope: `sh -c`, not bash

Target the POSIX Shell Command Language (XCU chapter 2). In scope:
simple commands, pipelines, `&&`/`||`/`;` lists, redirections including
here-documents, quoting, and the expansions. Explicitly **out** of scope
for the first pass: interactive use, line editing, history, job control,
and aliases — the libc needs none of them, and they are most of what
makes real shells large.

## What already exists and must be reused, not rewritten

Most of the expansion engine landed before this note was written:

- `src/wordexp/` — tilde, parameter and pathname expansion, quote removal
- `src/glob/`, `fnmatch` — pathname generation
- `src/process/` — `__spawn`, `fork`, `waitpid`, `__find_program`
- `src/unistd/pipe.c`, `dup2`, `src/signal/` — plumbing and dispositions

The genuinely new layer is the *command language*: parsing pipelines,
lists, compound commands and redirections, and executing them. That is a
much smaller delta than "write a shell", and it is the part nothing in
the tree does today.

## Placement and gates

Ship it as a clearly separate deliverable in this repo — its own source
directory and binary — rather than blurring into `src/`. `CONTRIBUTING.md`
opens by saying ntlibc exists to be a C library talking to ntdll; a shell
is a userland program, and the reason it lives here should be stated
there rather than left implicit.

It needs its own clause-cited conformance tests. A shell that is subtly
wrong about quoting or field splitting is worse than no shell, because
callers cannot tell.

## Bootstrap note

The kaem bootstrap (`boot/kaem/`, `tools/gen-kaem.sh`) exists precisely
because there is no shell at that stage. A working `sh` eventually means
ordinary shell scripts could replace some of that machinery — but it is
downstream of the shell being trustworthy, and kaem must keep working
unchanged in the meantime.
