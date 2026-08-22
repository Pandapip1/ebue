<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Memory allocation, process termination, and environment coverage fragment

Clause-by-clause POSIX.1-2017 audit of `src/malloc/`, `src/exit/` and
`src/env/`. New audit: `test/posix-alloc.c` (48 `CHECK()` assertions).
Pre-existing coverage checked against the spec rather than duplicated:
`test/malloc.c` (owned by another agent, extensive alignment/aliasing/
ASan-ENOMEM coverage of malloc/calloc/realloc/free/posix_memalign/
aligned_alloc/memalign/valloc/reallocarray/malloc_usable_size) and
`test/misc.c` (owned by another agent, covers getenv/setenv/unsetenv/
putenv basics, `system()`, and one atexit-LIFO + abort/assert/exit
child-spawn sequence). This fragment does not edit
`test/POSIX-COVERAGE.md`; it is written standalone for the coordinator
to merge.

The atexit/abort/exit-vs-_Exit clauses need a child process observed
from the parent (fork() needs `RtlCloneUserProcess`, absent under stock
Wine); `test/posix-alloc.c` re-execs itself via `__spawn()`, same
pattern as `test/misc.c`'s `test_abort_child()`.

| function | clause checked | status | test |
|---|---|---|---|
| malloc | RETURN VALUE: size==0 -> NULL or a usable/freeable pointer (implementation-defined, either permitted) | covered | test/malloc.c (basic), test/posix-alloc.c (two simultaneously-live malloc(0) results must not alias if both non-null) |
| malloc | ERRORS: ENOMEM on failure | covered | test/malloc.c |
| calloc | DESCRIPTION: "the space shall be initialized to all bits 0" | covered | test/malloc.c (small), test/posix-alloc.c (4 MiB allocation, deliberately dirtied via a same-size malloc+memset+free beforehand to rule out "happened to start zero") |
| calloc | nelem\*elsize overflow must be detected (NULL+ENOMEM), not silently wrapped into an under-sized allocation -- classic exploitable calloc bug | covered | test/malloc.c (SIZE_MAX/2\*4, SIZE_MAX\*SIZE_MAX), test/posix-alloc.c (exact wraparound boundaries: product wraps to 0, product wraps to 1, product==SIZE_MAX exactly via both m=SIZE_MAX,n=1 and m=1,n=SIZE_MAX, n==0 never treated as overflow, and a genuinely satisfiable 1000\*1000 must NOT be rejected) |
| realloc | RETURN VALUE: size==0 -> NULL or a usable/freeable pointer (implementation-defined, either permitted); this implementation's actual choice recorded as information, not asserted as a preference | covered | test/malloc.c, test/posix-alloc.c |
| realloc | DESCRIPTION: "contents ... shall remain unchanged up to the lesser of the new and old sizes", both growing and shrinking | covered | test/malloc.c (basic), test/posix-alloc.c (32->4096 grow preserves all 32 original bytes; 4096->10 shrink preserves the surviving 10-byte prefix) |
| realloc | RETURN VALUE: "the memory referenced by ptr shall not be changed" if realloc() fails with ENOMEM | covered (best-effort) | test/posix-alloc.c (near-SIZE_MAX request; original content re-verified when the call does report failure; when the huge address space actually satisfies it, the successful path's content is checked instead -- ENOMEM is not reliably forceable under Wine, same caveat as `test/string.c`'s `strdup` note) |
| realloc(NULL, n) | DESCRIPTION: "equivalent to malloc(size)" | covered elsewhere | test/malloc.c |
| free | DESCRIPTION / no return value | covered elsewhere | test/malloc.c |
| posix_memalign | ERRORS: EINVAL for a non-power-of-two-multiple-of-sizeof(void\*) alignment | covered elsewhere | test/malloc.c |
| posix_memalign | boundary case: alignment == sizeof(void\*) itself (the smallest legal value) succeeds | covered | test/posix-alloc.c |
| aligned_alloc / memalign / valloc / reallocarray / malloc_usable_size | out of scope beyond what's cited above; already exercised in `test/malloc.c` | covered elsewhere | test/malloc.c |
| exit | DESCRIPTION: calls every atexit()-registered function "in the reverse order of their registration" | covered | test/misc.c (h1/h2/h3, LIFO order of 3, in-process), test/posix-alloc.c (LIFO order of 40 handlers, also in-process: no spawn needed, the last-to-run handler validates the whole sequence and calls `_Exit()` itself, since control never returns to `main()` after `exit()`) |
| exit | atexit handlers run "at normal program termination" via exit() | covered | test/misc.c (exit(23) propagates through waitpid), test/posix-alloc.c (exit-code-only child check: the registered handler force-terminates with a distinctive code (99) if it runs at all, so the parent's wait status alone says whether it ran -- no marker file, which would need the spawned child and the parent to agree on a working directory/filesystem view that the native asan build's simulated NT filesystem does not guarantee) |
| _Exit / _exit | "_exit() and _Exit() ... do not call functions registered with atexit()" | covered | test/posix-alloc.c (same exit-code-only check, contrasted directly against the exit() case in the same test) |
| atexit | DESCRIPTION: "at least 32 functions can be registered" | covered | test/posix-alloc.c (40 registrations in a child, every one required to return 0, all 40 confirmed to fire in exact reverse order via the log file) |
| atexit | RETURN VALUE: 0 on success / non-zero on failure | covered (success path only; ATEXIT_MAX==128 in `src/exit/exit.c`, so failing the call would need 128 registrations -- not attempted, out of scope beyond the required-minimum-32 clause) | test/posix-alloc.c |
| abort | DESCRIPTION: "abnormal process termination", SIGABRT via raise() | covered | test/misc.c (child dies, exit code nonzero or signalled), test/posix-alloc.c (`check_died_abnormally()`: `WIFSIGNALED(status)` required, and `WTERMSIG(status)==SIGABRT` additionally required whenever the status did come back signalled -- decoded via `__wait_encode_status`/`sig_status` in src/process/wait.c under Wine, which is where this holds unconditionally; the native asan build's real host wait4() truncates the 0xE0DE00xx encoding to 8 bits before ntlibc's own decoder ever sees it, same reason test/waitpid-overflow.c and test/posix-signal.c are excluded there wholesale, so the signal-number check is skipped rather than falsely failed in that one environment) |
| abort | DESCRIPTION: "shall override blocking or ignoring the SIGABRT signal" | covered | test/posix-alloc.c (child installs SIG_IGN for SIGABRT, still dies abnormally; separate child blocks SIGABRT via sigprocmask, still dies abnormally -- both via `check_died_abnormally()`) |
| abort | not reachable through atexit -- an atexit-registered handler must not run when abort() (rather than exit()) ends the process | covered | test/posix-alloc.c (the shared force-exit(99) handler's code absent from the wait status of both the SIG_IGN and the blocked-signal abort children) |
| assert | DESCRIPTION: false expression -> diagnostic to stderr, then abort() | covered | test/misc.c (child does not report success; noise expected on stderr), test/posix-alloc.c (`check_died_abnormally()` on the assert-triggering child, same contract as abort() itself) |
| getenv | RETURN VALUE: pointer to value, or NULL if not found | covered elsewhere | test/misc.c, test/posix-stdlib.c |
| setenv | ERRORS: EINVAL for empty name or a name containing '=' | covered elsewhere | test/misc.c, test/posix-stdlib.c |
| setenv | DESCRIPTION: overwrite==0 leaves an *existing* value unchanged, but must still create a variable that does not exist yet (the two cases are distinct; only "existing" was isolated elsewhere) | covered | test/posix-alloc.c |
| setenv | DESCRIPTION: "shall...copy" name and value (contrasted directly against putenv's aliasing, in the same test, on the same buffer) | covered | test/posix-alloc.c |
| unsetenv | RETURN VALUE 0 on success incl. a no-op removal of a missing name; ERRORS EINVAL for empty/'=' name | covered elsewhere | test/misc.c, test/posix-stdlib.c |
| putenv | DESCRIPTION: "the string ... shall become part of the environment, so altering the string shall change the environment" (aliased, not copied) | covered elsewhere (basic case) | test/misc.c; test/posix-alloc.c adds the direct copy-vs-alias contrast on one shared buffer |
| environ | reflects setenv() additions and unsetenv() removals when walked directly (not just through getenv()) | covered | test/posix-alloc.c |
| environ inheritance across a spawn | not a POSIX.1-2017 clause by itself (environment inheritance across exec is `exec.html`'s domain, `envp` argument), but a real NT integration point: the environment block `__spawn` hands the child is UTF-16 and rebuilt at spawn time (src/process/spawn.c) | covered (information, not a spec clause) | test/posix-alloc.c -- **observed: a setenv()-modified environment IS inherited by a spawned child** (child exits 44, meaning it saw the parent's `setenv()`ed variable) |

## Bugs found this session

None. Every clause checked against `malloc.html`, `calloc.html`,
`realloc.html`, `posix_memalign.html`, `exit.html`, `atexit.html`,
`abort.html`, `assert.html`, `getenv.html`, `setenv.html`,
`unsetenv.html`, and `putenv.html` matched ntlibc's `src/malloc/`,
`src/exit/`, and `src/env/` implementation, including the
security-sensitive `calloc` overflow check
(`if (n && m > (size_t)-1 / n)` in `src/malloc/malloc.c`), which is
correct at every wraparound boundary tested.

## Observed behaviour where POSIX permits latitude

- `malloc(0)`: returns a unique, non-NULL, freeable pointer (backed by
  `RtlAllocateHeap`, which is documented to do this for a 0-byte
  request). Two live `malloc(0)` results are distinct pointers.
- `realloc(p, 0)`: returns a non-NULL pointer (`RtlReAllocateHeap` with
  size 0 behaves the same way malloc(0) does on this heap), freeable.
- Neither is asserted as *the* correct answer -- both are within the
  POSIX-permitted set, and `test/posix-alloc.c` prints which one this
  build does (`note: malloc(0) returns ...` / `note: realloc(p, 0)
  returns ...`) so a successor auditing a different build does not have
  to re-derive it.
- A `setenv()`-modified environment **is** inherited by a spawned
  child (`test_setenv_inherited_by_child`, prints
  `note: setenv()-modified environment inherited by child: yes`).

## `src/` changes

None needed. Every clause of interest was already reachable through
the public API; no internal decision function needed extracting
`__errno_from_status`-style (the `calloc` overflow check is already a
small, directly-testable expression at the call site -- pulling it into
a separate named function would not have made anything more testable
than calling `calloc()` at the wraparound boundaries directly does).

## Build / environment notes

`make check` 28/28 on **both** `i386-win32` and `x86_64-win32`
(configured with `CC=i386-win32-tcc` / `CC=x86_64-win32-tcc`, bare
names per the pre-commit hook's kaem regeneration). `tools/asan-build.sh`
native build: `test/posix-alloc.c` links and runs there too --
`__spawn()` is backed by `fuzz/ntstubs.c`'s process-spawn stub (the
same one `test/misc.c` already relies on there) -- so it did not need
adding to that script's `not_native()` list, but it did need one small
change to that script: the `calloc()` overflow-boundary tests
deliberately request a genuinely unsatisfiable (non-overflowing, just
huge) size, which needs the same `ASAN_OPTIONS=...,
allocator_may_return_null=1` treatment the script already carves out
for `test/malloc.c` (see the block comment there and in
`tools/asan-build.sh`) so ASan hands back NULL instead of hard-aborting
on "exceeds maximum supported size". Extended that one `case` in
`tools/asan-build.sh` to cover `posix-alloc` too, rather than watering
down the test. `make lint` stays clean (0 findings) and `tools/asan-build.sh`
now reports 23/23 native passes (was 22/23 before this session).

## What was not reached

- `atexit()`'s own failure return (registering past `ATEXIT_MAX`==128
  in `src/exit/exit.c`) -- not attempted; the POSIX-mandated clause is
  only the *minimum* of 32 successful registrations, which is covered.
- `_SC_ATEXIT_MAX` via `sysconf()`: `src/unistd/sysconf.c` (out of this
  agent's `src/` scope: `src/unistd/`, not `src/malloc|exit|env/`) does
  not implement `_SC_ATEXIT_MAX` and falls through to its `default:
  errno=EINVAL; return -1` case. POSIX does not *require* every
  `_SC_*` name to be implemented, so this is not logged as a bug, only
  as a gap a successor touching `src/unistd/` could close.
- `realloc()`'s ENOMEM-leaves-ptr-untouched clause could not be forced
  down the failure path reliably under Wine (same caveat noted for
  `strdup`'s ENOMEM path in `test/posix-string.c`'s predecessor
  session) -- the test handles both outcomes rather than asserting
  failure is guaranteed.
- `quick_exit` / `at_quick_exit`: implemented in `src/exit/exit.c` but
  not exercised by any test in the tree (noted as a gap by the
  `stdlib.h` fragment already; not re-duplicated here since it is the
  same functions, same gap).
- Signal-catching form of `abort()`'s "unless SIGABRT is being caught
  and the handler does not return" clause (a real handler that
  `longjmp()`s out of `abort()`) -- not exercised; only the
  ignore-and-block override paths were, per the task brief's emphasis
  on "abort() must not be catchable by an atexit handler" rather than
  by a raw signal handler.
