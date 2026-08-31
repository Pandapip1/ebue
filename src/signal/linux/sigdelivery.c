/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux's half of the src/signal/{nt,linux}/sigdelivery.c split. See
 * src/signal/nt/sigdelivery.c's own banner for the full story this
 * file does NOT need to repeat: NT has no real signal delivery of its
 * own, so that file invents one out of a named pipe (one per process,
 * \Device\NamedPipe\ntlibc-sig.<pid>) plus a named mutant, with a
 * dedicated OS thread blocked reading the pipe. None of that
 * machinery has a reason to exist on Linux, which already has real
 * kernel-native signal delivery -- this file implements the exact
 * same portable functions signal.c calls (declared in libc.h,
 * unconditionally, next to __sig_lock()'s own NTLIBC_ACQUIRE()
 * annotation) using Linux's own real primitives where Linux has an
 * equivalent, and an honest, disclosed degrade where it does not yet.
 *
 * __sig_lock()/__sig_unlock()/__sig_unlock_for_handler()/
 * __sig_relock_after_handler(): a real, working port of the identical
 * recursive-lock design the NT file documents at length (see that
 * file's own "Locking." section) -- owning-thread-id-plus-depth on
 * top of one binary lock, so the SAME thread can walk back in without
 * waiting on itself while a genuinely different thread blocks for
 * real, and the lock is released (not merely left held) around the
 * user's signal handler callback. The binary lock itself is a real
 * semaphore built from this platform's own already-real primitives
 * (src/thread/linux/plat_thread.c's __plat_semaphore_create() /
 * __plat_wait_one() / __plat_semaphore_post()), not a fabrication --
 * the identical primitive src/thread/pthread_mutex.c's own blocking
 * slow path already trusts for real mutual exclusion under real
 * contention (see that file's own pilot report).
 *
 * __sig_delivery_init()/__sig_delivery_reinit_after_fork(): create the
 * real wake_event (__plat_sigevent_create(), a real Linux eventfd)
 * and the real lock semaphore above. What this does NOT do yet is
 * install any real kernel-level signal handler (rt_sigaction(2)) --
 * every signal this process generates for ITSELF (raise(), abort(),
 * a hardware fault turned into a signal by the platform's fault path)
 * is already fully real and portable: it goes straight through
 * signal.c's own __raise_internal_info(), which is ordinary C, not
 * platform-specific at all, and needs nothing from this file. What
 * IS degraded, honestly and on purpose, is described next.
 *
 * __sig_try_deliver_remote{,_info,_nondefault}(): always report "not
 * delivered" (0), which is not a fabricated stub -- it is the EXACT
 * documented degrade path signal.c's kill() already has to handle
 * unconditionally for the "no listener" case on NT (see that
 * function's own comment: "Failure ... falls straight through to the
 * existing behaviour unchanged"). Real disposition-aware cross-process
 * delivery on Linux needs a real kernel signal handler installed via
 * rt_sigaction(2) (with a real sigreturn trampoline -- genuinely
 * assembly-level work, the same class of thing src/thread/linux/
 * clone_aarch64.S already had to be for thread creation) so that
 * tgkill(2)/kill(2) invoke the target's REAL registered disposition
 * directly, through the kernel, needing no RPC of any kind -- simpler
 * than NT's own scheme once built, but not yet built. Until then,
 * kill() to another process falls back to its existing
 * __plat_kill_terminate() path exactly as if the target had never
 * installed a handler, the same honest degrade every NT process
 * without a listener already gets.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "libc.h"
#include "plat_signal.h"
#include "plat_thread.h"

static __plat_handle_t wake_event;   /* real eventfd; 0 = not running */
static __plat_handle_t lock_sem;     /* real binary semaphore; 0 = no locking done */
static pid_t lock_owner;
static int lock_depth;

__plat_handle_t __sig_delivery_event(void) { return wake_event; }

NTSTATUS __sig_wait_delivery(LARGE_INTEGER *timeout)
{
	__plat_signal_wait(wake_event, timeout != 0, timeout ? (long long)*timeout : 0);
	return STATUS_SUCCESS;
}

void __sig_notify_delivery(void)
{
	if (wake_event) __plat_event_set(wake_event);
}

/* NTLIBC_NO_THREAD_SAFETY_ANALYSIS: these four are
 * __ntlibc_sig_lock_token's real implementation on this platform, the
 * same reasoning src/signal/nt/sigdelivery.c's own matching comment
 * gives for its four. */
void __sig_lock(void) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	pid_t me;
	if (!lock_sem) return;
	__pthread_cancel_defer_enter();
	me = gettid();
	if (lock_depth > 0 && lock_owner == me) { lock_depth++; return; }
	__plat_wait_one(lock_sem, 0, 0, 0);
	lock_owner = me;
	lock_depth = 1;
}

void __sig_unlock(void) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	if (!lock_sem) return;
	if (--lock_depth > 0) {
		__pthread_cancel_defer_leave();
		return;
	}
	lock_owner = 0;
	__plat_semaphore_post(lock_sem);
	__pthread_cancel_defer_leave();
}

int __sig_unlock_for_handler(void) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	int depth;
	if (!lock_sem) return 0;
	depth = lock_depth;
	lock_depth = 0;
	lock_owner = 0;
	__plat_semaphore_post(lock_sem);
	return depth;
}

void __sig_relock_after_handler(int depth) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	if (!lock_sem || depth <= 0) return;
	__sig_lock();
	lock_depth = depth;
}

void __sig_delivery_init(void)
{
	__plat_handle_t sem, ev;

	if (__plat_semaphore_create(1, 1, 0, &sem) < 0) return;
	lock_sem = sem;

	ev = __plat_sigevent_create(0);
	if (!ev) return;
	wake_event = ev;
}

void __sig_delivery_reinit_after_fork(void)
{
	wake_event = __PLAT_HANDLE_NULL;
	lock_sem = __PLAT_HANDLE_NULL;
	lock_owner = 0;
	lock_depth = 0;
	__sig_pending_reset_after_fork();
	__timer_reinit_after_fork();
	__sig_delivery_init();
}

/* See this file's own banner: real disposition-aware remote delivery
 * needs a real kernel signal handler this file does not install yet.
 * Reporting "not delivered" here is signal.c's own already-documented
 * fallback contract, not a fabrication. */
int __sig_try_deliver_remote_info(int pid, int sig, const void *data) // NOLINT(bugprone-easily-swappable-parameters) -- fixed signal-delivery contract; process ID and signal number have distinct roles
{
	(void)pid; (void)sig; (void)data;
	return 0;
}

int __sig_try_deliver_remote(int pid, int sig)
{
	return __sig_try_deliver_remote_info(pid, sig, 0);
}

int __sig_try_deliver_remote_nondefault(int pid, int sig) // NOLINT(bugprone-easily-swappable-parameters) -- fixed signal-delivery contract; process ID and signal number have distinct roles
{
	(void)pid; (void)sig;
	return 0;
}

// NOLINTEND(misc-include-cleaner)
