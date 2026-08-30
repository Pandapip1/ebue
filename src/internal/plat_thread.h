/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform-thread interface src/thread/{pthread,pthread_cancel,
 * pthread_cond,pthread_mutex,pthread_rwlock,pthread_signal,pthread_sync,
 * pthread_tsd,semaphore,mqueue,aio}.c's POSIX-facing front doors call into
 * instead of raw Nt{CreateThreadEx,SuspendThread,ResumeThread,
 * GetContextThread,SetContextThread,QueueApcThread,WaitForSingleObject,
 * WaitForMultipleObjects,CreateEvent,SetEvent,CreateSemaphore,
 * OpenSemaphore,ReleaseSemaphore,QuerySemaphore,CreateMutant,
 * ReleaseMutant,DuplicateObject,QueryInformationThread,TerminateThread,
 * TerminateProcess,DelayExecution,QuerySystemTime,ReadFile,WriteFile}
 * calls.  See src/thread/nt/plat_thread.c for the implementation these
 * declare.
 *
 * Every function here takes POSIX/opaque-shaped arguments and returns a
 * POSIX-shaped result -- errno already set on failure, never a raw NTSTATUS
 * for the front door to interpret.  A handful of exceptions are
 * deliberate, not oversights, and each is called out at its own
 * declaration below:
 *
 *   - __plat_wait_one()/__plat_wait_any() return a small fixed enum
 *     (__PLAT_WAIT_*) rather than 0/-1.  Every wait-with-retry loop across
 *     this subsystem's mutex/cond/rwlock/barrier/once/semaphore/aio code
 *     branches on strictly the same four outcomes -- object acquired,
 *     timed out, woken by an APC/alert with nothing to show for it yet, or
 *     a real error -- so this is the POSIX-shaped vocabulary those loops
 *     already speak, not raw NT status leaking through.  The loop
 *     structure itself (what to DO with each outcome: retry, translate to
 *     ETIMEDOUT, run __pthread_testcancel(), ...) stays in the front door
 *     exactly as before; only the individual NtWaitFor{Single,Multiple}
 *     Object() call at the bottom of each loop iteration moves here.
 *
 *   - __plat_event_create() and __plat_thread_spawn() additionally return
 *     -2 (distinct from the usual -1/errno) for NT's STATUS_NOT_IMPLEMENTED
 *     -- observed on some sandboxed hosts that have no event/thread
 *     objects at all.  aio.c's start_worker() has a real degraded-but-
 *     correct fallback (run every request synchronously) that must trigger
 *     on exactly that condition and no other failure; reconstructing "was
 *     it specifically STATUS_NOT_IMPLEMENTED" from a generic errno
 *     afterward is exactly the trap this interface exists to avoid, so the
 *     decision is made here, once, while the real status is still in hand.
 *
 *   - __plat_thread_entry_t/__plat_apc_fn are NT's own native-thread-start
 *     and asynchronous-procedure-call callback shapes.  There is no POSIX
 *     vocabulary for "a function the kernel invokes directly in a target
 *     thread it does not otherwise control" -- pthread_create()'s native
 *     thread entry and pthread_cancel()'s/pthread_kill()'s APC-delivered
 *     signal trampoline all need exactly this shape, and inventing a
 *     POSIX-looking wrapper around it would hide, not remove, the
 *     platform dependency.  The callback BODIES (thread_entry(),
 *     notice_thread(), aio_worker(), signal_apc(), cancel_apc()) all stay
 *     in their front doors, unmoved: every one of them operates on
 *     private front-door state (struct __pthread, the aio request table,
 *     ...) that has no business being visible from src/thread/nt/.
 */
#ifndef _NTLIBC_PLAT_THREAD_H
#define _NTLIBC_PLAT_THREAD_H

#include <stddef.h>
#include <sys/types.h>
#include "plat_handle.h"

/* NT's __stdcall on i386, the only calling convention x86_64 has -- the
 * exact condition src/internal/nt.h's own NTAPI macro gates on, reproduced
 * here rather than pulled in via nt.h so this header stays free of every
 * other NT declaration a hypothetical future backend would have no use
 * for. */
#if defined(__i386__)
#define __PLAT_APC_CALL __attribute__((stdcall))
#else
#define __PLAT_APC_CALL
#endif

/* pthread_create()'s native thread entry point shape (NtCreateThreadEx's
 * StartRoutine) and NT's asynchronous-procedure-call callback shape
 * (NtQueueApcThread's ApcRoutine) -- see this file's banner for why both
 * are exposed as-is rather than behind a POSIX-looking wrapper. */
typedef unsigned (__PLAT_APC_CALL *__plat_thread_entry_t)(void *);
typedef void (__PLAT_APC_CALL *__plat_apc_fn)(void *, void *, void *);

/* ---- waiting ------------------------------------------------------------
 * See this file's banner for why these four outcomes, not a raw NTSTATUS,
 * are the interface. */
#define __PLAT_WAIT_OK      0   /* the object was acquired/signalled */
#define __PLAT_WAIT_TIMEOUT 1   /* the requested interval elapsed first */
#define __PLAT_WAIT_INTR    2   /* alertable wait woken by an APC/alert;
                                 * nothing acquired -- go around again */
#define __PLAT_WAIT_ERROR   3   /* a genuine failure; errno is set */

/* Wait on one object.  `alertable` matches NtWaitForSingleObject's own
 * Alertable argument.  `has_timeout` zero means block indefinitely
 * (a NULL LARGE_INTEGER timeout pointer); nonzero means `relative_ticks`
 * (100ns units, already negative/relative or exactly zero for an
 * immediate poll -- the caller's own convention, unchanged) is passed
 * through verbatim. */
int __plat_wait_one(__plat_handle_t h, int alertable, int has_timeout,
                    long long relative_ticks);
/* Same, for NtWaitForMultipleObjects' WaitAny mode -- the only mode any
 * caller in this subsystem uses.  Which of `handles[0..count)` woke the
 * wait is never reported: no existing caller needs it (each re-derives
 * what happened from its own state under its own lock after the wait
 * returns), matching NtWaitForMultipleObjects' STATUS_WAIT_0+n encoding
 * folding uniformly into __PLAT_WAIT_OK here. */
int __plat_wait_any(__plat_handle_t *handles, unsigned count, int alertable,
                    int has_timeout, long long relative_ticks);

/* ---- events (SynchronizationEvent, initially unset) ---------------------
 * __plat_event_create()'s -2 is explained in this file's banner. */
int __plat_event_create(__plat_handle_t *out);
int __plat_event_set(__plat_handle_t h);

/* ---- unnamed semaphores --------------------------------------------------
 * `inheritable` requests OBJ_INHERIT, needed only by sem_init()'s
 * process-shared/fork-surviving semaphores -- every internal wait object
 * this subsystem builds for its own bookkeeping (a mutex/cond/rwlock
 * waiter's private wake object) passes 0. */
int __plat_semaphore_create(long initial, long maximum, int inheritable,
                            __plat_handle_t *out);
/* Release by exactly 1 -- the only count any caller in this subsystem
 * ever releases by.  NT's STATUS_SEMAPHORE_LIMIT_EXCEEDED becomes
 * [EOVERFLOW] here, not reconstructed afterward: see write.c's SIGPIPE
 * comment (src/unistd/nt/plat_fd.c) for why this class of decision belongs
 * inside the backend function that still has the real status in hand. */
int __plat_semaphore_post(__plat_handle_t h);
int __plat_semaphore_getvalue(__plat_handle_t h, int *value);

/* ---- named objects under \BaseNamedObjects -------------------------------
 * `name` is the already-fully-qualified NT object-manager path, ASCII
 * (every name this subsystem builds is ASCII by construction -- a hash or
 * a pid/sequence pair formatted with snprintf), built by the front door;
 * everything UNICODE_STRING/OBJECT_ATTRIBUTES-shaped about turning it into
 * an NT object lives entirely inside the backend. */

/* A fresh, believed-unique name (the front door has already embedded a
 * pid/sequence counter in it) -- collision is a plain error, not a
 * create-or-open contract. */
int __plat_named_semaphore_create(const char *name, long initial,
                                  long maximum, __plat_handle_t *out);
/* -2 (distinct from the usual -1/errno), rather than a generic ENOENT,
 * reports NT's STATUS_OBJECT_NAME_NOT_FOUND specifically: sem_open()'s
 * O_CREAT-without-O_EXCL recovery path (a creator can die after
 * publishing the filesystem record but before filling it in with a real
 * object name) must fire on exactly that condition and no other -- see
 * this file's banner on why that decision is made here, not
 * reconstructed from errno afterward. */
int __plat_named_semaphore_open(const char *name, __plat_handle_t *out);
/* Create it, or open the existing one if the name is already taken.
 * Wine reports an existing named semaphore as the ERROR status
 * STATUS_OBJECT_NAME_COLLISION rather than NT's own informational
 * STATUS_OBJECT_NAME_EXISTS; falling back to an open on exactly that
 * status -- decided here, while it is still in hand -- is what makes this
 * a create-or-open primitive at all rather than one that merely fails
 * under Wine every time the name is reused. */
int __plat_named_semaphore_open_or_create(const char *name, long initial,
                                          long maximum, __plat_handle_t *out);

/* A named binary mutant used as a cross-process advisory lock: create-or-
 * open it (NT's OBJ_OPENIF) and wait on it, infinitely and non-alertably,
 * as one call -- the two steps NT's own object manager makes atomic
 * against a second creator racing the same name, so splitting them across
 * two backend calls would reintroduce exactly the race this pattern
 * exists to avoid. */
int __plat_named_mutant_acquire(const char *name, __plat_handle_t *out);
void __plat_named_mutant_release(__plat_handle_t lock);

/* ---- thread lifecycle ----------------------------------------------------
 * __plat_thread_spawn()'s -2 is explained in this file's banner.
 * `stack_size` 0 requests the image's own default (NtCreateThreadEx's own
 * meaning for a zero reserve/commit size); nonzero is used for both the
 * reserve and commit size, matching every existing call site, which never
 * asked for the two to differ. */
int __plat_thread_spawn(__plat_thread_entry_t entry, void *arg,
                        size_t stack_size, int create_suspended,
                        __plat_handle_t *out);
int __plat_thread_resume(__plat_handle_t h);
int __plat_thread_suspend(__plat_handle_t h);
/* Queue `fn(arg1, arg2, 0)` to run the next time `h` becomes alertable
 * (or immediately, if it is already inside an alertable wait). */
int __plat_thread_queue_apc(__plat_handle_t h, __plat_apc_fn fn, void *arg1,
                            void *arg2);
/* Force `h` -- which the caller has already suspended -- to resume
 * execution at `target` (a _Noreturn trampoline) rather than wherever it
 * was interrupted.  The GetContextThread/patch-one-field/SetContextThread
 * sequence this takes is NT/arch-ABI-specific CONTEXT-record knowledge
 * with no POSIX-facing decision anywhere inside it, so it stays one call
 * rather than being fragmented across the suspend/resume bracketing it in
 * the front door (see pthread_cancel.c's redirect_async_cancel(), which
 * still owns every pthread-cancellation-policy decision around this
 * call). */
int __plat_thread_redirect_ip(__plat_handle_t h, void *target);
/* The stack [base, base+size) of a live thread, read via its TEB --
 * pthread_getattr_np()'s one platform-specific step. */
int __plat_thread_stack_extent(__plat_handle_t h, void **base, size_t *size);
/* A durable handle on the calling thread, safe to store past this call
 * (unlike NtCurrentThread()'s pseudo-handle, which is only ever valid for
 * an operation performed *by* that thread on itself).  Always succeeds
 * from the caller's perspective: a duplication failure falls back to the
 * pseudo-handle itself, exactly as before this call existed -- there is
 * no POSIX-facing decision inside this fallback, only NT plumbing. */
__plat_handle_t __plat_thread_duplicate_self(void);
/* NtCurrentThread()'s pseudo-handle -- valid only for an operation the
 * calling thread performs on itself within this same call (unlike the
 * durable handle above), used by pthread_getattr_np() to query its own
 * TEB without needing a duplicate. */
__plat_handle_t __plat_thread_current_pseudo(void);
/* Never returns -- retries forever defensively, matching the exact
 * pre-existing pthread_exit() idiom this replaces. */
_Noreturn void __plat_thread_terminate_self(void);
/* The bypass-everything emergency abort __pthread_cancel_unsafe_enter()'s
 * documented regions use: write a fixed diagnostic straight to the
 * process's standard error handle (bypassing stdio and the fd table,
 * which the suspended target may itself own the locks of) and terminate
 * the whole process immediately with this library's signal-exit encoding
 * for SIGABRT.  One call because there is no POSIX-facing decision
 * anywhere inside it to leave in a front door -- see
 * pthread_cancel.c's cancel_unsafe_abort() banner for why this exists at
 * all. */
_Noreturn void __plat_cancel_unsafe_abort(const char *region);
/* An alertable, zero-length delay -- "yield the processor, but let a
 * pending APC run first if there is one." */
void __plat_thread_alertable_yield(void);
/* The current NT system time, 100ns ticks since 1601 -- aio_suspend()'s
 * own clock for measuring its deadline against, kept as NT ticks (not
 * translated through clock_gettime()/struct timespec) because that is
 * what it already used and this is a relocation, not a redesign. */
long long __plat_query_system_time(void);

/* ---- mqueue.c's queue-file I/O --------------------------------------------
 * A positioned transfer at a fixed byte offset, non-alertable (mqueue's
 * own retry loops are driven by wait_count()'s separate alertable
 * semaphore waits, never by this transfer itself), with NO end-of-file
 * short-circuit: an incomplete transfer against the queue's own backing
 * file is always a real error condition for the caller's retry loop to
 * evaluate (see mqueue.c's raw_io()), never the "read() hit a legitimate
 * EOF, report 0" case unistd's own __plat_pread (src/internal/plat_fd.h)
 * exists to provide.  Returns bytes transferred (may be less than `count`
 * -- the caller loops) or -1/errno. */
ssize_t __plat_thread_file_io(__plat_handle_t h, void *buf, size_t count,
                              off_t off, int write_op);

#endif
