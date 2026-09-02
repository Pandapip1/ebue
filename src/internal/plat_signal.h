/* C library internals and platform ABI fields intentionally use the
 * implementation-reserved namespace so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface src/signal/signal.c and sigdelivery.c's
 * POSIX-facing front doors call into instead of raw
 * Nt{CreateEvent,SetEvent,WaitForSingleObject,CreateMutant,
 * ReleaseMutant,CreateNamedPipeFile,OpenFile,FsControlFile,ReadFile,
 * WriteFile,CreateThreadEx,SuspendProcess,ResumeProcess,OpenProcess,
 * TerminateProcess,Close,DelayExecution,QueryVirtualMemory} calls. See
 * src/signal/nt/plat_signal.c for the implementation these declare.
 *
 * __sig_lock()/__sig_unlock()/__sig_unlock_for_handler()/
 * __sig_relock_after_handler() (sigdelivery.c) are NOT part of this
 * interface and are untouched by this migration: they are this
 * project's Clang-Thread-Safety-Analysis-annotated recursive lock, out
 * of scope by explicit instruction.  __sig_wait_delivery() (also
 * sigdelivery.c) keeps its own pre-existing NTSTATUS/LARGE_INTEGER*
 * signature unconverted too, for a narrower reason: src/unistd/sleep.c
 * -- outside this migration's scope -- calls it directly with a
 * LARGE_INTEGER it constructs itself, so that signature cannot change
 * without touching a file this migration does not own.  Its raw wait
 * calls are still relocated, via __plat_signal_wait() below; only the
 * thin NTSTATUS/LARGE_INTEGER* wrapper stays in sigdelivery.c, and its
 * return value is never once inspected by any of its callers, so
 * collapsing every NT wait outcome into a single reported status there
 * is not a behavior change.
 *
 * This subsystem's own wire protocol (sigdelivery.c's named pipes and
 * mutants) has no POSIX shape to begin with -- it is this library's own
 * invented cross-process RPC, built entirely out of NT object-manager
 * primitives -- so several functions below necessarily take an NT
 * UNICODE_STRING* for an object name, the same way src/internal/
 * plat_mem.h's __plat_mem_map_file() necessarily takes an off_t: there
 * is no POSIX-shaped alternative for what these operations are.  What
 * IS relocated, per the general contract, is every NTSTATUS-level
 * interpretation step: STATUS_PENDING waits, STATUS_PIPE_CONNECTED/
 * STATUS_OBJECT_NAME_EXISTS/STATUS_PROCESS_IS_TERMINATING special-
 * casing, and the [EPERM]-vs-[ESRCH] decision on a failed process open
 * that needs the real status in hand (see __plat_kill_open() below and
 * the matching note in plat_misc.h, whose sched.c call site makes
 * exactly the same decision for exactly the same reason).
 */
#ifndef _NTLIBC_PLAT_SIGNAL_H
#define _NTLIBC_PLAT_SIGNAL_H

#include <sys/types.h>
#include "plat_handle.h"

struct _UNICODE_STRING;

/* ---- sigdelivery.c -------------------------------------------------- */

/* Create a SynchronizationEvent, initially signalled iff
 * `initially_signalled`.  __PLAT_HANDLE_NULL on failure.  Named
 * __plat_sigevent_create(), not __plat_event_create(), to stay distinct
 * from plat_thread.h's __plat_event_create(__plat_handle_t *) -- same
 * underlying NT primitive, different calling convention (this one
 * returns the handle directly rather than through an out-param, and
 * takes an initial-signal-state flag thread's does not need). */
__plat_handle_t __plat_sigevent_create(int initially_signalled);

/* Signal `ev`.  0/-1(errno) via return. */
int __plat_event_set(__plat_handle_t ev);

/* Wait (alertably, so timer APCs stay deliverable) on `wake_event` if it
 * is not __PLAT_HANDLE_NULL, for up to `ticks` 100ns units (NT's own
 * relative/absolute LARGE_INTEGER encoding, passed through unchanged)
 * when `has_timeout`, or indefinitely when not; when `wake_event` IS
 * __PLAT_HANDLE_NULL (this process's delivery event was never created --
 * a degraded startup, see sigdelivery.c's own comment), sleep for
 * `ticks`/indefinitely the same way, or for a fixed 100ms fallback when
 * neither a timeout nor an event is available.  No return value: every
 * caller of __sig_wait_delivery() (signal.c, unistd/sleep.c) discards
 * what woke it and just rechecks its own state. */
void __plat_signal_wait(__plat_handle_t wake_event, int has_timeout, long long ticks);

/* Create (FILE_OPEN_IF) the named, message-mode signal-delivery pipe
 * object `name` -- sigdelivery.c's sig_create_pipe().
 * __PLAT_HANDLE_NULL on failure. */
__plat_handle_t __plat_signal_pipe_create(const struct _UNICODE_STRING *name);

/* Block until a client connects to `pipe` (FSCTL_PIPE_LISTEN, with the
 * STATUS_PIPE_CONNECTED-means-already-connected normalization
 * sigdelivery.c's banner explains). 1 connected, 0 on any failure. */
int __plat_signal_pipe_listen(__plat_handle_t pipe);

/* Read up to `len` bytes from `h` into `buf` (STATUS_PENDING handled
 * internally). 0 with *received set on success, -1 on failure.
 * received is required: written unconditionally on the success path
 * with no NULL check (the outputs being left untouched on FAILURE is
 * a fact about the value, not about whether the pointer itself may be
 * NULL -- d24fe86's own __plat_process_fork() precedent); both real
 * call sites (src/signal/nt/sigdelivery.c) pass &got, never NULL. */
int __plat_signal_pipe_read(__plat_handle_t h, void *buf, size_t len, size_t *received)
    __attribute__((nonnull(4)));

/* Write `len` bytes of `buf` to `h` (STATUS_PENDING handled
 * internally). 0 with *sent set on success, -1 on failure. sent
 * required, same reasoning and both real call sites (&sent) as
 * __plat_signal_pipe_read() above. */
int __plat_signal_pipe_write(__plat_handle_t h, const void *buf, size_t len, size_t *sent)
    __attribute__((nonnull(4)));

/* 100ms backoff delay -- sig_delivery_thread()'s retry pause after a
 * failed pipe (re)create. */
void __plat_signal_backoff(void);

/* Create-or-open (OBJ_OPENIF) the named mutant `name` -- used both by
 * __sig_delivery_init() (this process's own send-serializing mutant)
 * and kill()'s cross-process arm (the target's).  __PLAT_HANDLE_NULL on
 * failure. */
__plat_handle_t __plat_signal_mutant_create(const struct _UNICODE_STRING *name);

/* Acquire `h` (wait indefinitely, non-alertable) -- sig_try_deliver_
 * remote_info()'s mutant acquisition. 1 success, 0 any failure. */
int __plat_wait_acquire(__plat_handle_t h);

/* Release the mutant `h`. */
void __plat_mutant_release(__plat_handle_t h);

/* Zero-timeout peek at `ev`: 1 if it was signalled (and, being
 * auto-reset, is now consumed), 0 if not.  Used both to check a
 * candidate stop-event (__sig_consume_child_stop(), signal.c) and to
 * retract a stop notification that turned out not to be needed
 * (stop_self(), signal.c). */
int __plat_event_peek(__plat_handle_t ev);

/* Open the client end of pid's signal pipe (see sigdelivery.c's
 * sig_pipe_name()), for a kill() sender's request/reply exchange.
 * __PLAT_HANDLE_NULL if the pipe does not currently name a listener --
 * sig_try_deliver_remote_info()'s caller draws no distinction between
 * that and any other open failure (see sigdelivery.c's own banner). */
__plat_handle_t __plat_signal_pipe_open(const struct _UNICODE_STRING *name);

/* Start a new NT thread running `entry(arg)` (calling convention/
 * signature `ULONG NTAPI entry(void *arg)`, NtCreateThreadEx's own
 * requirement) -- runs detached, as every thread this library ever
 * creates does (NT tears down every thread of a process at exit).
 * 0/-1 via return, *out set to the new thread's still-open handle on
 * success. out required: written unconditionally on success, no NULL
 * check; its one real call site (src/signal/nt/sigdelivery.c) passes
 * &thr, never NULL. NT-only (this header's own banner on
 * src/signal/linux/plat_signal.c: the pipe/mutant transport this
 * thread serves is not yet ported, so there is no Linux definition to
 * cross-check against). */
int __plat_thread_start(void *entry, void *arg, __plat_handle_t *out)
    __attribute__((nonnull(3)));

/* ---- signal.c --------------------------------------------------------- */

/* Create-or-open (OBJ_OPENIF) the named, initially-unsignalled
 * SynchronizationEvent `name` -- stop_self()'s self-stop marker.
 * __PLAT_HANDLE_NULL with errno set (__set_errno_status()'s generic
 * mapping -- there is no per-status decision to make for this one) on
 * failure. */
__plat_handle_t __plat_stop_event_create(const struct _UNICODE_STRING *name);

/* __sig_consume_child_stop()'s per-candidate-signal probe: create-or-
 * open the named event; on ANY failure simply report "not this one",
 * touching no errno (unlike __plat_stop_event_create() above, which IS
 * meant to report a failure to its caller) so the loop's next candidate
 * is tried exactly as before. *already_existed is set only on success,
 * to 1 iff the name already lived (STATUS_OBJECT_NAME_EXISTS) --
 * exactly the distinguishing fact the loop needs the real status in
 * hand for: an event this call created fresh cannot possibly have a
 * stop already recorded in it. 0/-1 via return. out/already_existed
 * both required: both backends (NT and Linux) write both
 * unconditionally on the success path with no NULL check, and the
 * one real call site (src/signal/signal.c's __sig_consume_child_stop())
 * passes &h/&already_existed, never NULL. */
int __plat_stop_event_probe(const struct _UNICODE_STRING *name, __plat_handle_t *out, int *already_existed)
    __attribute__((nonnull(2, 3)));

/* Suspend/resume THIS process (NtSuspendProcess(NtCurrentProcess())) --
 * stop_self()'s own suspend; distinct from __plat_process_suspend()/
 * __plat_process_resume() below, which act on a handle to some OTHER
 * process (kill()'s job-control arm). 0/-1(errno) via return. */
int __plat_process_suspend_self(void);

/* Suspend/resume the process `h` refers to -- kill()'s SIGSTOP/SIGCONT
 * job-control arm (sig_job_control(), signal.c). 0/-1(errno) via
 * return. */
int __plat_process_suspend(__plat_handle_t h);
int __plat_process_resume(__plat_handle_t h);

/* Open pid's process object for kill()'s cross-process operations --
 * PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION always,
 * PROCESS_SUSPEND_RESUME too when `want_suspend_resume` (job-control
 * signals only). [EPERM] on failure iff the platform's own access
 * check specifically refused (STATUS_ACCESS_DENIED); [ESRCH] for every
 * other reason (no such process). The distinction is made here, with
 * the real status still in hand -- see this header's own banner. out
 * required: both backends write it unconditionally on the success
 * path with no NULL check, and the one real call site
 * (src/signal/signal.c's kill()) passes &h, never NULL. */
int __plat_kill_open(pid_t pid, int want_suspend_resume, __plat_handle_t *out)
    __attribute__((nonnull(3)));

/* NtTerminateProcess(h, exitcode), with kill()'s own tolerance: a
 * target already exiting reports STATUS_PROCESS_IS_TERMINATING, which
 * kill() treats as success rather than the [ESRCH] the generic status
 * table would give it (see __errno_from_status()) -- POSIX's kill() to
 * a process that no longer meaningfully exists as a live target is not
 * a failure the caller can act on. 0/-1(errno) via return. */
int __plat_kill_terminate(__plat_handle_t h, int exitcode);

/* SEGV_MAPERR vs SEGV_ACCERR for a fault at `addr` -- see signal.c's
 * own comment on segv_code() (moved here verbatim) for the full
 * MEM_COMMIT/Protect reasoning. */
int __plat_segv_code(void *addr);

/* Synchronize `sig`'s disposition with the real, kernel-level signal
 * disposition, for the two dispositions the kernel already implements
 * entirely on its own with no userspace callback at all: SIG_IGN
 * (`ignore` nonzero) and SIG_DFL (`ignore` zero). signal()/sigaction()
 * (src/signal/signal.c) call this on every disposition change, so that a
 * signal the KERNEL itself can raise synchronously out of a syscall --
 * SIGPIPE from write() to a reader-less pipe is the motivating case, see
 * src/unistd/linux/plat_fd.c's own banner -- is actually ignored/restored
 * to default at the point the kernel decides whether to act, rather than
 * always running the kernel's inherited-at-exec disposition regardless of
 * what this library's own handlers[] table says.
 *
 * Deliberately narrower than "make signal() work on Linux": a real
 * user-defined handler is NOT installed at the kernel level by this call
 * (or by anything else yet) -- doing that needs a real
 * rt_sigaction(2)-installed entry point with its own sigreturn trampoline,
 * disclosed and left for later exactly where src/signal/linux/
 * sigdelivery.c's own banner already discloses the identical gap on the
 * delivery side. A signal caught by a real ntlibc handler still only
 * fires for what this library synthesizes itself -- raise(), abort(), a
 * hardware fault turned into a signal, kill() to self (see signal.c's own
 * header comment) -- exactly as before this function existed; this call
 * only ever asks the kernel for SIG_IGN or SIG_DFL, never a function
 * pointer, so no trampoline is ever needed.
 *
 * No-op where there is no real kernel signal delivery to synchronize
 * with in the first place (the NT backend, which already gets this
 * right by construction: every signal reaching __raise_internal() on NT
 * was synthesized by this library, which always consults handlers[]
 * itself -- see src/signal/nt/plat_signal.c's own definition). */
void __plat_sig_sync_kernel(int sig, int ignore);

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
