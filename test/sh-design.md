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
- **`wordexp()`** (`src/wordexp/wordexp.c`) refused command substitution
  outright with `WRDE_CMDSUB`, because `cmd.exe` cannot parse `$(...)`.
  **Fixed as of stage 5** — it calls `__sh_cmdsub()`
  (`src/internal/libc.h`, implemented in `src/sh/exec.c`) and runs the
  substitution for real. `WRDE_NOCMD` is now the only thing that
  produces `WRDE_CMDSUB`, which is what the standard says it is for.

That is one missing component, not three problems. Building it fixes all
three at the source rather than accumulating three accommodations. One
of the three is fixed; `system()` and `popen()` are item 5 below.

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

As of stage 5 (`src/sh/exec.c`), the executor covers: simple commands
with real PATH lookup and `$?` (stage 2); assignments, `&&`/`||`/`!`,
and all nine redirection operators including here-documents, plus
pipelines of arbitrary length (stage 3); subshells `( list )` and
brace groups `{ list; }`, including as pipeline stages, plus a minimal
`cd` builtin (stage 4); and command substitution in both the `$(...)`
and the `` `...` `` form (stage 5). `src/sh/parse.c` already parses more
than the executor runs: `if`/`while`/`for`/`case`, functions and aliases
parse as ordinary reserved words today only insofar as the lexer treats
them as WORD tokens (sh.h's banner) — the grammar for them does not
exist yet either, parser or executor.

What is still missing, in the order it would need to be tackled (later
items depend on earlier ones being real, not on each other's specific
implementation):

1. ~~**Command substitution** (`$(...)`/`` `...` ``)~~ — **done, stage
   5.** It went exactly the way this item predicted: `wordexp()`'s
   command-substitution call-out is wired to `__sh_exec_list()` via
   `__sh_cmdsub()` (`src/sh/exec.c`), and `exec_group()`'s
   environ/cwd save-and-restore is *reused* for the "subshell
   environment" 2.6.3/2.12 require rather than reinvented.

   The one thing this item left open was how to capture output
   without a real pipe. **A temporary file**, per this file's own
   here-document reasoning, and for the identical reason: an
   in-memory sink is not available to a *spawned* child (the
   substituted commands are separate NT processes that inherit an fd,
   not a buffer), and a pipe would deadlock, because
   `__sh_exec_list()` runs to completion in this process before
   anything reads — so a substitution larger than one 64KiB pipe
   buffer would wedge the shell against itself, silently. Concurrency
   would fix that and is exactly what `exec.c` deliberately does not
   have (no fork: stock Wine has no `RtlCloneUserProcess`; no
   threads). `tmpfile()` is seekable, so writers finish first and the
   read happens after.

   Trade-off accepted: a substitution's output round-trips through
   `%TEMP%`, so it needs a writable temp directory (everything else
   in `exec.c` needs only a writable cwd) — the same dependency
   here-documents already have — and the result is not available
   incrementally, which costs nothing because 2.6.3's result is the
   complete output by definition.

   Both forms are first class. The backquoted one is not a footnote
   to `$(...)`: an autoconf-generated `configure` uses it roughly 14×
   as often, so 2.6.3's "first unquoted non-escaped backquote" search,
   its backslash removal (`\$`, `` \` ``, `\\`), and nesting
   *through* that removal have their own scanner and their own tests.
2. **Control-flow reserved words** (`if`/`while`/`for`/`case`) —
   currently an explicit non-goal (sh.h's banner, this note's opening
   paragraph). A `sh -c` that cannot run a real script needs at least
   `if`/`for` to be worth calling a shell rather than a command
   splitter; this is a scope decision for whoever picks this up next,
   not a foregone conclusion — the design note's original scope
   ("simple commands, pipelines, `&&`/`||`/`;` lists, redirections...
   quoting, and the expansions") deliberately left it out.
3. **Functions, aliases, job control** — functions are *done* (stage
   7b): XCU 2.9.5's `fname ( ) compound-command`, with new positional
   parameters per call, `return`, and 2.9.1's command search order
   placing them between the special and the regular built-ins. They
   stopped being "not yet decided" the moment `configure` became the
   target workload, since an autoconf script defines dozens of them.
   Aliases remain undecided. Job control in particular is called out as
   permanently out of scope ("the libc needs none of them").
4. **A `main()` with `-c` and script-file handling** — *done*:
   `sh/main.c`, built as `obj/sh/sh.exe` by `make sh` (and by `make
   all`), with its own black-box tests in `test/sh-main.c`. Its own
   source directory and binary, per "Placement and gates" above.
   Supports `sh -c command_string [command_name [arg...]]`, `sh [-s]
   [command_file [arg...]]`, a script on standard input, `--`/`-`,
   and XCU 2.8.2's exit status (that of the last command executed).

   It deliberately shipped *ahead* of (1) and without (2), which this
   entry had assumed it would wait for. What made that defensible is
   that it refuses, up front and by name, every construct the engine
   would otherwise misread rather than diagnose: control-flow reserved
   words and unimplemented built-ins (all of which lex as ordinary
   WORDs today, so `if`/`export` would run as external commands and
   fail with a *true* "command not found" about a fiction — or, for
   `export`, fail while silently not exporting), positional and
   special parameters (`wordexp()` expands only `$NAME`/`${NAME}`, so
   `"$1"` reaches the command as two literal characters), and `&`
   (which `__sh_exec_list()` currently runs synchronously). A script
   that uses one of those stops before anything runs, with a message
   saying which. Positional parameters are not plumbed through at all
   and are not faked: there is nowhere to put them, since the only
   variable store any expansion sees is the real `environ`.

   That leaves an honest, narrow shell rather than a broad and subtly
   wrong one — the property "Placement and gates" says matters most
   ("a shell that is subtly wrong ... is worse than no shell, because
   callers cannot tell"). Each of (1), (2) and (3) landing shortens
   the refusal list rather than changing the binary's shape.
5. **Wiring `system()`/`popen()` over to it** — the actual payoff (see
   "Why a libc project is growing a shell" above). `wordexp()`'s half
   of this is done: its `WRDE_CMDSUB` refusal is gone (stage 5).
   `system()` and `popen()` still hand their command string to
   `%ComSpec%`/`cmd.exe` (`src/stdlib/system.c`, `src/stdio/misc.c`).
   Stage 5 investigated the switch rather than making it. **Do not
   switch yet**, and in particular do not switch on the strength of a
   parse-failure fallback: measured against this executor, that
   fallback does not fire where it would need to.

   What was measured (a probe running each string through both
   `__sh_parse()`+`__sh_exec_list()` and today's `system()`, under
   Wine):

   - **Today, cmd.exe already fails on most POSIX-shaped strings.**
     `prog 'single quoted arg'`, `( a; b )`, a here-document, `$(...)`,
     `` `...` ``, `if`, `for` — all rejected by cmd's own lexer or
     mangled. So the *upside* of switching is real and large.
   - **cmd.exe's internal commands are the regression surface, and
     they are bigger than control flow.** `dir`, `echo`, `copy`,
     `del`, `set`, `type`, `mkdir`, `rmdir` are cmd built-ins with no
     `.exe` anywhere; every one of them works through `system()`
     today and returns **127** through this executor, which has
     exactly one builtin (`cd`). Windows-targeted code calling
     `system("del file")` or `system("echo x > y")` is not exotic;
     it is the normal way that code is written.
   - **A fallback keyed on *parse failure* does not catch control
     flow.** This lexer treats reserved words as ordinary WORD tokens
     (sh.h's banner), so `for f in a b; do echo $f; done`,
     `if true; then echo y; fi` and `while ...; do ...; done` all
     **parse successfully** and then run a program called `for`/`if`/
     `while` — exit 127, no parse error, nothing for a fallback to
     trigger on. Only `case ...` and a function definition are
     actually rejected by the parser. A fallback keyed on parse
     failure would therefore protect precisely the two constructs
     nobody writes and miss the three everybody does. (`sh/main.c`'s
     own refusal check catches them; the parser does not, and
     `system()` would be calling the parser.)

   Half the missing detector already exists, in (4): `sh/main.c`
   refuses, up front and by name, exactly the constructs the engine
   would otherwise misread — control-flow reserved words, unimplemented
   built-ins, positional/special parameters, `&`. That refusal, lifted
   out of `main()` into something `system()` could call, is what would
   make "our shell cannot handle this string" a *detectable* condition
   instead of a silent 127, and is the precondition a fallback needs.

   So the sequence is: (2) first — or, at minimum, factor
   `sh/main.c`'s refusal check into a reusable "can this engine run
   this program?" predicate — plus a decision about cmd built-ins
   (implement the handful that matter as real builtins, or accept the
   regression, or keep the fallback permanently). Only then is a
   fallback switch defensible, and even then its cost is that the
   *language* `system()` speaks becomes "POSIX sh, or cmd.exe batch,
   depending on which one this build happens to accept" —
   undiagnosable from the outside, and silently different for a string
   valid in both (`echo a > b` is not the same program in the two
   languages). Treat it as a transition strategy with a removal
   condition, not a design.

None of the above blocks anything already shipped: stages 2-5 are a
complete, correctly-scoped subset on their own, tested against the
XCU clauses they implement, and (4) above now exposes exactly that
subset as a real program.
