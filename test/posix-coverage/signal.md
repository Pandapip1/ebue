<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# signal.h / sys/wait.h coverage fragment

Companion to `test/POSIX-COVERAGE.md` (priority item 7, `signal.h`, plus
`sys/wait.h` from the "also not yet reached" list). Per the task split
this session, this file is owned by the `signal.h` agent and is not
merged into `POSIX-COVERAGE.md` itself. New clause-cited coverage lives
in `test/posix-signal.c`; read `src/signal/signal.c`'s header comment
first -- there is no asynchronous signal delivery from another thread or
process on this platform, which shapes almost every N/A below.

Existing ad-hoc coverage this session did not duplicate: `test/misc.c`
(`test_signal`, `test_abort_child`: SIG_DFL/SIG_IGN/handler round trip,
SIGKILL EINVAL, blocked-signal-becomes-pending-and-delivers-on-unblock,
abort() overriding SIG_IGN, an assert()ing child, an exit(23) child) and
`test/waitpid-overflow.c` (child-table growth past its static seed,
every exit code 0..255 round-tripping through `WIFEXITED`/`WEXITSTATUS`,
`kill(SIGTERM)`/`abort()` round-tripping through `WIFSIGNALED`/
`WTERMSIG`/`WCOREDUMP`, `ECHILD` for an already-reaped or nonexistent
pid). `test/posix-signal.c`'s block comment cross-references both.

## signal.h

| function | clause checked | status | test |
|---|---|---|---|
| signal | SIG_DFL/SIG_IGN/handler set & returns previous disposition | covered | test/misc.c |
| signal | EINVAL + SIG_ERR for invalid sig (0, _NSIG, -1) | covered | test/misc.c |
| signal | EINVAL + SIG_ERR for SIGKILL (cannot be caught/ignored) | covered | test/misc.c |
| signal | EINVAL + SIG_ERR for SIGSTOP (same restriction) | covered | test/posix-signal.c |
| signal.h | `sig_atomic_t` assignable in a handler, visible to the caller once the delivering call returns | covered (see note) | test/posix-signal.c |
| raise | returns 0 on success; runs the handler before returning (delivery is always synchronous here) | covered | test/misc.c |
| raise | EINVAL for invalid sig (0, -1, _NSIG) | covered | test/misc.c (0), test/posix-signal.c (-1, _NSIG) |
| kill | pid == caller: routes to raise(), sig delivered before kill() returns | covered | test/misc.c |
| kill | sig == 0: existence/permission check, "no signal is actually sent" | covered | test/posix-signal.c (against a real child, not just self) |
| kill | pid > 0, real other process: signal reaches exactly that process | covered | test/posix-signal.c |
| kill | EINVAL for invalid/unsupported sig | covered | test/posix-signal.c |
| kill | ESRCH for a pid nothing spawned | covered | test/posix-signal.c |
| kill | pid == 0 ("current process group"), pid < -1 (magnitude names a group) | N/A -- no process groups on this platform (see below) | -- |
| kill | EPERM (different real/effective uid) | N/A -- not reliably triggerable under Wine without a second user | -- |
| killpg | BSD extension, `killpg(pg,sig) == kill(pg,sig)` verbatim | N/A (not POSIX.1-2017 base) | -- |
| sigaction | act==NULL queries without changing; oact==NULL is accepted | covered | test/posix-signal.c |
| sigaction | EINVAL for SIGKILL/SIGSTOP | covered | test/posix-signal.c |
| sigaction | SA_RESETHAND: disposition reset to SIG_DFL on entry to the handler | **BUG (fenced)** | test/posix-signal.c: `test_sa_resethand` |
| sigaction | implicit self-mask on entry (signal blocked against itself during its own handler unless SA_NODEFER) | **BUG (fenced)** | test/posix-signal.c: `test_sigaction_implicit_mask` |
| sigaction | sa_mask (blocking a *different* signal for the handler's duration) | **BUG, not separately tested** -- same root cause as the SA_NODEFER finding (`sigaction()` never reads `act->sa_mask` at all) | see note under Bugs found |
| sigaction | SA_RESTART: an interruptible function restarts instead of failing EINTR | N/A -- no blocking call is ever interrupted by an asynchronously-delivered signal on this platform, so there is nothing to restart (see below) | -- |
| sigemptyset / sigfillset | always return 0, no errors defined | covered | test/posix-signal.c |
| sigaddset / sigdelset / sigismember | EINVAL for invalid signo (0, -1, _NSIG) | covered | test/posix-signal.c |
| sigismember | returns exactly 1 (member) or 0 (not), not merely truthy | covered | test/posix-signal.c |
| sigprocmask | SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK semantics (union/complement-intersect/replace) | covered | test/misc.c (block), test/posix-signal.c (SETMASK replace, UNBLOCK) |
| sigprocmask | EINVAL for an invalid `how` | covered | test/posix-signal.c |
| sigprocmask | set==NULL: mask unchanged, oset still filled | covered | test/posix-signal.c |
| sigprocmask | SIGKILL/SIGSTOP cannot be blocked, enforced without error | covered | test/posix-signal.c |
| sigprocmask | a blocked signal becomes pending, delivered on unblock | covered | test/misc.c, test/posix-signal.c (via sigpending too) |
| sigpending | empty when nothing is blocked+raised; reflects a blocked+raised signal; clears once delivered | covered | test/posix-signal.c |
| sigsuspend | return value: -1 / EINTR (the only return this stub can produce) | covered | test/posix-signal.c |
| sigsuspend | DESCRIPTION: replace the mask, actually suspend until a signal is delivered | N/A -- documented permanent stub, see below | -- |
| sigwait / sigtimedwait / sigqueue | require a per-process queued-signal-with-payload facility this platform has none of | N/A -- documented stubs, see `include/signal.h`'s comments on `sigwaitinfo()`/`sigqueue()` | -- |
| sigaltstack | accepts/reports SS_DISABLE only; no real alternate signal stack | N/A -- documented no-op (there is no signal-stack switch on a synchronous-exception-only implementation) | -- |
| abort | never returns | covered (implicit: every test that calls it is itself proof) | test/misc.c, test/posix-signal.c |
| abort | overrides SIG_IGN | covered | test/misc.c |
| abort | overrides SIG_BLOCK | covered | test/posix-signal.c |
| abort | a caught SIGABRT whose handler returns normally still terminates the process | covered | test/posix-signal.c |
| abort | "may" attempt fclose() on open streams | N/A -- MAY, not SHALL; not a testable requirement either way | -- |
| strsignal | maps signum to a non-NULL, non-empty implementation-defined string, for every valid signal number | covered | test/string.c (spot checks against `__sigmsgs[]`), test/posix-signal.c (full 1.._NSIG-1 sweep) |
| strsignal | unspecified (not required to be non-NULL) for an invalid signum; must not crash | covered | test/posix-signal.c |
| psignal / psiginfo | not implemented anywhere in `src/` or `include/` -- no symbol exists | N/A -- not implemented; nothing to test | -- |

**Correction to `test/POSIX-COVERAGE.md`'s string.h table**: that ledger
lists `strsignal` as "N/A (XSI extension, not base)". Fetching
`https://pubs.opengroup.org/onlinepubs/9699919799/functions/strsignal.html`
this session shows it *is* part of the POSIX.1-2017 base
(`IEEE Std 1003.1-2017`), not XSI/GNU-only. Not fixed here since that
table belongs to the string.h agent's file; flagging so it is not lost.

### Why kill()/waitpid() process-group targeting (pid==0, pid<-1) is N/A

`src/signal/signal.c`'s `kill()` treats `pid == 0` as an alias for the
caller's own pid (routes to `raise()`) and any `pid < 0` as `ESRCH`,
rather than "current process group" / "the group named by |pid|". NT has
no equivalent of a Unix process group, and this library does not
maintain one (`killpg()` is a straight pass-through to `kill()`, and
`do_waitpid()`'s `pid < 0` branch just negates and treats it as one
specific pid). Given no group concept exists to be right or wrong about,
this is recorded as an architectural N/A rather than a bug -- there is
no group of more than one process to observe a difference against.

### Why SA_RESTART is N/A

POSIX's SA_RESTART only has an effect on a call that was interrupted
mid-block by an asynchronously delivered signal. On this platform every
signal is delivered synchronously, inside the very call that generates
it (`raise()`, `kill()` on self, `abort()`) or inside the vectored
exception handler for a synchronous CPU fault -- nothing is ever
"interrupted" the way e.g. a blocking `read()` on a real Unix would be,
so there is no `EINTR` case for SA_RESTART to suppress and no test that
could ever observe the flag doing anything either way.

### Why sigsuspend()'s DESCRIPTION clause is N/A, not a fenced BUG

`sigsuspend()` (`src/signal/signal.c`) is `{ errno = EINTR; return -1; }`
verbatim -- a documented permanent stub, in the same family as
`sigwait()`/`sigtimedwait()` (see `include/signal.h`'s comment on
`sigwaitinfo()`, which explains the shared reason: no per-thread
suspend/wake primitive and no queued-signal facility exist to build a
real one on). It happens to satisfy the one narrow, unconditionally-true
RETURN VALUE clause ("if a return occurs, -1 shall be returned ... with
EINTR") by construction. The DESCRIPTION clause -- replace the mask,
then actually block until a signal arrives -- can never be made to pass
without a real per-thread wait primitive this platform does not have, so
per the task brief this is recorded as N/A with its reason rather than
given a test written to fail forever.

## sys/wait.h

| function/macro | clause checked | status | test |
|---|---|---|---|
| wait | any child, blocks until one changes state | covered | test/misc.c, test/waitpid-overflow.c |
| waitpid | pid==-1/0: any child (this platform has one implicit process group, so treating pid==0 the same as pid==-1 is the correct degenerate case, not a shortcut) | covered | test/waitpid-overflow.c |
| waitpid | pid>0: exactly that child | covered | test/waitpid-overflow.c |
| waitpid | WNOHANG: 0 immediately for a child that has not exited, without touching `*status` | covered | test/posix-signal.c |
| waitpid | ECHILD: no children, an already-reaped pid, a pid that is not our child | covered | test/waitpid-overflow.c |
| waitpid | EINVAL for an invalid `options` value | **BUG (fenced)** | test/posix-signal.c: `test_waitpid_einval_options` |
| waitpid | EINTR (a signal caught while waiting) | N/A -- no asynchronous delivery exists to interrupt a blocking wait | -- |
| wait3 / wait4 | not a POSIX.1-2017 base function -- `wait3.html` 404s on the standard site; BSD/historical | N/A (not POSIX.1-2017 base) | test/posix-signal.c: `test_wait4_sanity` (sanity pass only: reaps correctly, fills a `struct rusage` without crashing) |
| WIFEXITED / WEXITSTATUS | true and exit code recovered for every exit code 0..255, mutually exclusive with WIFSIGNALED | covered | test/waitpid-overflow.c (through real processes), test/posix-signal.c (`__wait_encode_status` unit sweep, all 256 codes) |
| WIFSIGNALED / WTERMSIG | true and signal recovered for a signal death, mutually exclusive with WIFEXITED | covered | test/waitpid-overflow.c, test/posix-signal.c |
| WCOREDUMP | set for the traditional core-dumping signals (SIGABRT etc.), clear otherwise | covered; not a POSIX.1-2017 base macro (`sys_wait.h.html`'s macro list has no WCOREDUMP entry -- BSD/glibc extension) | test/waitpid-overflow.c (SIGABRT/SIGTERM via real processes), test/posix-signal.c (SIGABRT/SIGTERM/SIGKILL via `__wait_encode_status`) |
| WIFSTOPPED | never true for any status this implementation can produce (no job control exists here) | covered | test/posix-signal.c (`__wait_encode_status` sweep) |
| WSTOPSIG | defined identically to WEXITSTATUS in `include/sys/wait.h`; nothing to exercise since a stopped child never exists | N/A -- no job control, so no stopped status this macro could meaningfully decode | -- |
| the 0xE0DE00xx encoding property | a signal death can never collide with a real 0..255 exit code (the bug fixed earlier this session, commit "waitpid: stop decoding exit codes 129-192 as signal deaths") | covered, specifically re-checked for the 129..192 range that regressed before | test/posix-signal.c: `test_wait_encode_status` |

## Extraction made

`src/process/wait.c`'s `encode_status()` (the exit-code -> wait-status
mapping) was `static`; renamed to `__wait_encode_status()`, made
non-static, and declared in `src/internal/libc.h` (not `include/` --
this is not public API), following the exact model of
`__errno_from_status()` in `src/internal/errno.c`. `test/posix-signal.c`
declares it locally (`test/` is not on the `-I src/internal` path,
same as `test/posix-errno.c`'s local NTSTATUS declarations) and drives
it directly for the 256-exit-code sweep and the signal-death cases,
instead of needing a real spawned process for every one of those
boundary cases.

## Bugs found this session

1. **`sigaction()`'s `sa_flags`/`sa_mask` are read nowhere.**
   `src/signal/signal.c`'s `sigaction()` is:
   ```c
   int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
   {
       ...
       if (act) handlers[sig] = act->sa_handler;
       return 0;
   }
   ```
   Only `sa_handler` is ever copied out of `*act`. Three separate
   POSIX.1-2017 base requirements from `sigaction.html`'s DESCRIPTION
   fall out of this, all with the same root cause:
   - `SA_RESETHAND` never resets the disposition to `SIG_DFL` after one
     delivery (`test_sa_resethand`, fenced).
   - Without `SA_NODEFER`, a signal is never blocked against re-entering
     its own handler (`test_sigaction_implicit_mask`, fenced).
   - `sa_mask` (blocking a *different* signal for the handler's
     duration) is equally unimplemented, though not given its own fenced
     test -- it is the same missing field access, and a test would only
     restate the same finding.

2. **`waitpid()` never validates `options`.** `wait.html` ERRORS says
   waitpid() "shall fail" with `EINVAL` if `options` is not valid;
   `do_waitpid()` (`src/process/wait.c`) only ever tests
   `options & WNOHANG` and silently ignores every other bit, including
   nonsense values with none of the defined flags set
   (`test_waitpid_einval_options`, fenced).

## Incidental fixes made while writing this pass

Exercising `WIFSTOPPED()` across the full 0..255 exit-code range and the
signal-death range under `make asan` (UBSan) surfaced a real, pre-existing
undefined-behavior bug in the macro itself, unrelated to any of the three
BUG findings above:
`#define WIFSTOPPED(s) ((short)((((s)&0xffff)*0x10001)>>8) > 0x7f00)`
computes `(s)&0xffff` as a plain (signed) `int`, and multiplying that by
`0x10001` overflows a 32-bit `int` for any `s` whose low 16 bits are
`>= 0x8000` -- signed integer overflow, UB, and it aborted the ASan test
run. Fixed in both places this macro is defined
(`include/sys/wait.h` and the duplicate copy in `include/stdlib.h`, which
`stdlib.h` also exposes for legacy System V compatibility) by casting to
`unsigned` before the multiply; the truncation to `short` afterward makes
the observable result identical, so this is a pure UB fix with no
behavior change. `tools/asan-build.sh` also gained a `posix-signal` entry
in its `not_native()` skip list, for the same reason `waitpid-overflow`
is already skipped there: a native host's `wait()` status only carries 8
bits of exit code, too few to round-trip this library's
`0xE0DE00xx`-encoded signal deaths through a real child process.

## What was not reached

- `psiginfo()` (the `siginfo_t`-taking sibling of `psignal()` on the same
  spec page) -- not implemented in ntlibc and out of the brief's explicit
  function list; noted for completeness, not tested.
- `SA_SIGINFO` / `sa_sigaction` / `siginfo_t` delivery -- `sigaction()`
  only ever copies `sa_handler`, never looks at the `sa_sigaction` union
  member or `SA_SIGINFO`, so a `SA_SIGINFO`-registered handler is
  silently never invoked as `(int, siginfo_t *, void *)`. This is the
  same root cause as the `sa_flags` bugs above (nothing but `sa_handler`
  is ever read) but was not separately fenced-tested; a successor
  wanting to close this out can extend `test_sigaction_implicit_mask`'s
  approach to a real `SA_SIGINFO` registration.
- Real hardware-fault signals (SIGSEGV/SIGFPE/SIGILL/SIGBUS via the
  vectored exception handler in `src/signal/signal.c`) were not
  exercised here -- deliberately provoking a real access violation or
  divide-by-zero from this test file felt like the wrong tool (it is
  inherently UB/platform-timing-sensitive to trigger on purpose, and
  `test/waitpid-overflow.c`'s "how the process actually died" coverage
  is via `kill()`/`abort()`, not a real fault). A successor with a
  reliable way to provoke one under Wine (a `--fault-child` role that
  dereferences NULL, say) could round out the `WIFSIGNALED`/`WTERMSIG`
  mapping for `EXCEPTION_ACCESS_VIOLATION` etc. specifically, rather
  than only through the `__NT_SIGNAL_EXIT` path this session covers.
