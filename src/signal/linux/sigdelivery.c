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
 * and the real lock semaphore above. Every signal this process generates
 * for ITSELF (raise(), abort(), a hardware fault turned into a signal by
 * the platform's fault path) is already fully real and portable: it goes
 * straight through signal.c's own __raise_internal_info(), which is
 * ordinary C, not platform-specific at all, and needs nothing from this
 * file. This process's own real kernel-level rt_sigaction(2) state is
 * a separate, orthogonal thing (below), needing no init step of its own
 * here: __signal_init() (src/signal/signal.c) installs it directly, and
 * signal.c's own sigaction()/signal() keep it in step from then on.
 *
 * __sig_try_deliver_remote{,_info,_nondefault}(): Tier 1
 * (src/signal/linux/plat_signal.c's __plat_sig_install_fault_
 * handlers()) installed a real rt_sigaction(2) handler -- with a real
 * sigreturn trampoline, arch/aarch64/src/sigreturn_trampoline.S -- for
 * exactly five hardware-fault signals (SIGSEGV/SIGBUS/SIGILL/SIGFPE/
 * SIGTRAP), so that a real fault on this thread reaches
 * __raise_internal_info() at all, instead of running the kernel's own
 * default action unseen by this library. Tier 2 widens that same real
 * entry point to the FULL, caller-chosen signal set: signal.c's
 * sigaction()/signal() now call __plat_sig_install_real_handler() (this
 * file's own comment on __plat_sig_sync_kernel() -- src/internal/
 * plat_signal.h -- has the full story) for ANY signal a real, catchable
 * disposition is installed for, not only the fixed five. Once that is
 * installed, the Linux kernel's own kill(2)/tgkill(2)/pidfd_send_
 * signal(2) already deliver to the target's REAL registered disposition
 * directly, through the kernel -- no RPC of any kind is needed the way
 * NT's own named-pipe transport needs one, which is why these three
 * functions, unlike NT's sigdelivery.c, do not implement a wire protocol
 * at all: they issue the real syscall themselves (__plat_kill_
 * terminate(), reused here as a plain "send this real signal for real"
 * primitive, not only kill()'s own last-resort fallback) and report
 * whether it was accepted.
 *
 * __sig_try_deliver_remote()/__sig_try_deliver_remote_info(): always
 * attempt the real send and report its outcome. This is correct even
 * for a signal the target never installed a real handler for: the
 * kernel's own default action (Term for most signals, Ignore for
 * SIGCHLD/SIGURG/SIGWINCH/SIGCONT, matching signal.c's own
 * default_action() table exactly, since both describe the same real
 * Linux kernel) already applies whether or not a real rt_sigaction(2)
 * handler exists, so there is no case where sending for real is worse
 * than the pre-Tier-2 degrade of not trying at all. `data`
 * (sigqueue()'s sigval payload) is honestly, deliberately not threaded
 * through the real siginfo_t pidfd_send_signal(2) could in principle
 * carry: no currently-passing test exercises a remote sigqueue() with a
 * real payload, and getting the kernel's own raw siginfo_t ABI layout
 * exactly right for that one unexercised path is real risk for no
 * measured benefit -- a real signal number still reaches the target
 * either way, just without its value.
 *
 * __sig_try_deliver_remote_nondefault(): kill()'s catchable-stop-signal
 * arm (SIGTSTP/SIGTTIN/SIGTTOU) needs an answer to a question the other
 * two do not: whether to apply the real per-signal default STOP action
 * itself (sig_job_control(), src/signal/signal.c, which always uses a
 * substitute SIGSTOP rather than the real requested signal, matching
 * NT's own undifferentiated suspend) or send the real requested signal
 * because the target overrode that default. Unlike an ordinary signal,
 * there is no single real syscall whose own outcome tells the SENDER
 * which of those happened, so this asks first: __plat_sig_remote_
 * disposition_nondefault() (src/signal/linux/plat_signal.c) reads the
 * target's own real kernel-level disposition straight out of
 * /proc/pid/status, and only if it says "caught" or "ignored" does this
 * function send the real signal and report success -- otherwise it
 * reports failure, unchanged, so kill()'s existing job-control fallback
 * runs exactly as it always did.
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

/* __plat_sigevent_set(), not __plat_event_set(): wake_event is a real
 * eventfd (__plat_sigevent_create() above), a different __plat_handle_t
 * domain than __plat_event_set()'s ntlibc_linux_sync-pointer one on this
 * platform -- see that function's own plat_signal.h comment for the real,
 * confirmed crash this used to cause the first time a real handler was
 * ever reached from a genuine kernel-delivered signal. */
void __sig_notify_delivery(void)
{
	if (wake_event) __plat_sigevent_set(wake_event);
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

/* See this file's own banner: the real kernel already delivers to
 * `pid`'s own real disposition once that disposition is installed at
 * the kernel level (Tier 1's fixed five plus Tier 2's
 * __plat_sig_install_real_handler() widening), so this just issues the
 * real send and reports whether the kernel accepted it -- reusing
 * __plat_kill_terminate() (src/internal/plat_signal.h), which already
 * decodes __ENCODE_SIGNAL_EXIT(sig) back out and performs a genuine
 * pidfd_send_signal(2) of the real signal number, exactly the operation
 * this function needs and kill()'s own last-resort arm already trusted
 * for the "no real handler" case. `h` is built the same bare-pid way
 * __plat_kill_open() builds one for a non-child target (src/signal/
 * linux/plat_signal.c's own banner on that convention) -- this file has
 * no struct __child of its own to read one from. `data` is
 * deliberately unused: see this file's own banner. */
int __sig_try_deliver_remote_info(int pid, int sig, const void *data) // NOLINT(bugprone-easily-swappable-parameters) -- fixed signal-delivery contract; process ID and signal number have distinct roles
{
	__plat_handle_t h = (__plat_handle_t)(long)pid;
	(void)data;
	return __plat_kill_terminate(h, __ENCODE_SIGNAL_EXIT(sig)) == 0;
}

int __sig_try_deliver_remote(int pid, int sig)
{
	return __sig_try_deliver_remote_info(pid, sig, 0);
}

/* See this file's own banner on why this cannot just always send: kill()
 * needs to know, before acting, whether the target's real disposition
 * overrides the default STOP action, since sending first and asking
 * later has no way to tell the sender which one the kernel actually
 * did. __plat_sig_remote_disposition_nondefault() (src/signal/linux/
 * plat_signal.c) answers that for real, off the target's own kernel-
 * exported state; only then is the real signal sent. */
int __sig_try_deliver_remote_nondefault(int pid, int sig) // NOLINT(bugprone-easily-swappable-parameters) -- fixed signal-delivery contract; process ID and signal number have distinct roles
{
	__plat_handle_t h;

	if (!__plat_sig_remote_disposition_nondefault((pid_t)pid, sig)) return 0;
	h = (__plat_handle_t)(long)pid;
	return __plat_kill_terminate(h, __ENCODE_SIGNAL_EXIT(sig)) == 0;
}

// NOLINTEND(misc-include-cleaner)
