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

## Status and what remains before a usable `sh.exe`

As of stage 4 (`src/sh/exec.c`), the executor covers: simple commands
with real PATH lookup and `$?` (stage 2); assignments, `&&`/`||`/`!`,
and all nine redirection operators including here-documents, plus
pipelines of arbitrary length (stage 3); and subshells `( list )` and
brace groups `{ list; }`, including as pipeline stages, plus a minimal
`cd` builtin (stage 4). `src/sh/parse.c` already parses more than the
executor runs: `if`/`while`/`for`/`case`, functions and aliases parse
as ordinary reserved words today only insofar as the lexer treats them
as WORD tokens (sh.h's banner) — the grammar for them does not exist
yet either, parser or executor.

What is still missing, in the order it would need to be tackled (later
items depend on earlier ones being real, not on each other's specific
implementation):

1. **Command substitution** (`$(...)`/`` `...` ``) — the one gap that
   already runs through every layer as a named, tested "not yet"
   (`WRDE_CMDSUB` from `wordexp()`, this file's -1 convention). Needed
   before `wordexp()` can stop refusing it, which is one of this
   project's three stated motivations. Mechanically: wire
   `wordexp()`'s command-substitution call-out to `__sh_exec_list()`,
   capturing that list's stdout into a buffer instead of an fd a real
   process would inherit — the same "subshell environment" 2.12
   requires for `( list )` (this file's own text above), so
   `exec_group()`'s save/restore machinery is *reused*, not
   reinvented, plus a way to capture output without a real pipe (a
   temp file, per this file's existing here-document reasoning, or an
   in-memory sink if one gets built first).
2. **Control-flow reserved words** (`if`/`while`/`for`/`case`) —
   currently an explicit non-goal (sh.h's banner, this note's opening
   paragraph). A `sh -c` that cannot run a real script needs at least
   `if`/`for` to be worth calling a shell rather than a command
   splitter; this is a scope decision for whoever picks this up next,
   not a foregone conclusion — the design note's original scope
   ("simple commands, pipelines, `&&`/`||`/`;` lists, redirections...
   quoting, and the expansions") deliberately left it out.
3. **Functions, aliases, job control** — also explicit non-goals today.
   Job control in particular is called out as permanently out of scope
   ("the libc needs none of them"); functions and aliases are not
   ruled out forever, just not yet decided.
4. **A `main()` with `-c` and script-file handling** — there is
   currently no `sh` binary at all, only `test/sh-engine.c`'s test harness
   (which re-execs itself for child roles, not a general-purpose
   entry point). This needs (1) and a scope decision on (2) to be
   worth shipping as a real `sh -c "..."`/`sh script` a user would
   actually invoke, per "Placement and gates" above (its own source
   directory and binary, not blurred into `src/`).
5. **Wiring `system()`/`popen()`/`wordexp()` over to it** — the actual
   payoff (see "Why a libc project is growing a shell" above). Once
   (1) exists, `wordexp()`'s own `WRDE_CMDSUB` refusal can be replaced
   outright. `system()` and `popen()` currently hand their command
   string to `%ComSpec%`/`cmd.exe` (`src/stdlib/system.c`,
   `src/stdio/misc.c`); switching them to `__sh_parse()` +
   `__sh_exec_list()` needs (4) settled for the entry-point shape but
   not the binary itself, since both already call into libc-internal
   code rather than spawning a separate process today.

None of the above blocks anything already shipped: stages 2-4 are a
complete, correctly-scoped subset on their own, tested against the
XCU clauses they implement.
