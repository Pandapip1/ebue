/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include "pthread_impl.h"
#include "plat_thread.h"

_Noreturn void __pthread_cancel_trampoline(void);

/* x86 branch: unchanged, still the literal `lock; cmpxchgl`/`lock;
 * xaddl` this project's pinned tcc (the only compiler the NT target
 * ever builds with) needs -- tcc has no __atomic_* builtins at all
 * (`'__atomic_compare_exchange_n' implicit declaration`, confirmed
 * against the actual pinned x86_64-win32-tcc), so those builtins are
 * only safe to reach on a target tcc never compiles for. On any other
 * arch (aarch64 today, always a real gcc/clang -- tcc is Windows/PE-
 * only) these unrecognized x86 instruction mnemonics would not even
 * assemble, so the builtin compiles to the correct native primitive
 * there instead (`cmpxchg`/`xadd` on x86, an LL/SC loop or `cas`/
 * `ldadd` on aarch64), same full-barrier semantics
 * (__ATOMIC_SEQ_CST). */
static int compare_exchange(volatile int *address, int old_value,
	int new_value)
{
#if defined(__i386__) || defined(__x86_64__)
	int previous;
	__asm__ __volatile__("lock; cmpxchgl %2, %1"
		: "=a"(previous), "+m"(*address)
		: "r"(new_value), "0"(old_value) : "memory");
	return previous;
#else
	__atomic_compare_exchange_n(address, &old_value, new_value, 0,
	                            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return old_value;
#endif
}

static int exchange_add(volatile int *address, int value)
{
#if defined(__i386__) || defined(__x86_64__)
	__asm__ __volatile__("lock; xaddl %0, %1"
		: "+r"(value), "+m"(*address) : : "memory");
	return value;
#else
	return __atomic_fetch_add(address, value, __ATOMIC_SEQ_CST);
#endif
}

static int atomic_load(volatile int *address)
{
	return compare_exchange(address, 0, 0);
}

/* POSIX only requires pthread_cancel(), pthread_setcancelstate(), and
 * pthread_setcanceltype() to be async-cancel-safe.  Redirecting a thread
 * out of any other function is therefore allowed to abandon an internal
 * lock half-acquired.  Mark the small set of ntlibc regions where that is
 * known to be destructive, and diagnose cancellation at delivery time.
 *
 * The write deliberately bypasses stdio and the fd table: the suspended
 * target may own either one's locks.  Termination likewise goes straight
 * to NT instead of abort()/__nt_exit(), whose signal and child bookkeeping
 * are not async-cancel-safe themselves. */
static _Noreturn void cancel_unsafe_abort(const char *region)
{
	__plat_cancel_unsafe_abort(region);
}

void __pthread_cancel_unsafe_enter(const char *region)
{
	struct __pthread *self = __pthread_self_control;
	if (!self) return;
	if (atomic_load(&self->cancel_unsafe_depth) == 0)
		self->cancel_unsafe_region = region;
	exchange_add(&self->cancel_unsafe_depth, 1);
}

void __pthread_cancel_unsafe_leave(void)
{
	struct __pthread *self = __pthread_self_control;
	int previous;
	if (!self || atomic_load(&self->cancel_unsafe_depth) <= 0) return;
	previous = exchange_add(&self->cancel_unsafe_depth, -1);
	if (previous == 1) self->cancel_unsafe_region = 0;
}

/* Internal observation point used by the death tests to wait until a
 * wrapper has entered its real unsafe interval before cancelling it. */
int __pthread_cancel_unsafe_active(pthread_t thread)
{
	if (!thread || thread->magic != PTHREAD_MAGIC) return 0;
	return atomic_load(&thread->cancel_unsafe_depth) > 0;
}

/* Internal locks are not cancellation boundaries.  If an asynchronous
 * request arrives while one is owned, remember it and act immediately after
 * the outermost protected transaction commits.  This is separate from the
 * unsafe-region diagnostic: the three POSIX async-cancel-safe operations use
 * this mechanism around their entire transaction, so cancellation cannot
 * expose their intermediate state. */
void __pthread_cancel_defer_enter(void)
{
	struct __pthread *self = __pthread_self_control;
	if (self) exchange_add(&self->cancel_defer_depth, 1);
}

void __pthread_cancel_defer_leave(void)
{
	struct __pthread *self = __pthread_self_control;
	int previous;
	if (!self || atomic_load(&self->cancel_defer_depth) <= 0) return;
	previous = exchange_add(&self->cancel_defer_depth, -1);
	if (previous == 1 && self->cancel_state == PTHREAD_CANCEL_ENABLE &&
	    self->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS &&
	    self->cancel_pending)
		__pthread_cancel_current();
}

/* A normal user APC is only delivered when the target enters an alertable
 * wait.  Asynchronous cancellation also has to stop a thread which never
 * waits (the conformance test intentionally uses a tight loop), so redirect
 * the suspended target to an arch trampoline which cannot return. */
static int redirect_async_cancel(struct __pthread *thread)
{
	int handled = 0;
	int unsafe = 0;
	const char *unsafe_region = 0;

	if (__plat_thread_suspend(thread->handle) < 0) return 0;
	/* The target can consume the request at a cancellation point after
	 * pthread_cancel() drops the PEB lock but before it is suspended.  Do
	 * not redirect it a second time while its cleanup handlers are already
	 * running: that abandons the first handler's stack frame. */
	if (!thread->cancel_pending || thread->cancel_running || thread->exited) {
		handled = 1;
	} else if (thread->cancel_state == PTHREAD_CANCEL_ENABLE &&
	    thread->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS) {
		if (atomic_load(&thread->cancel_defer_depth) > 0) {
			/* The outermost leave observes cancel_pending and delivers it.
			 * Report failure so pthread_cancel() also queues the APC needed
			 * if the target is already in an alertable wait. */
			handled = 0;
		} else if (atomic_load(&thread->cancel_unsafe_depth) > 0) {
			unsafe_region = thread->cancel_unsafe_region;
			unsafe = 1;
			handled = 1;
		} else {
			/* Claim the cancellation before changing the instruction pointer.
			 * The target makes the same atomic claim on its cancellation-point
			 * path, closing the interval in which both paths used to observe the
			 * plain volatile marker as zero and enter cleanup twice. */
			if (compare_exchange(&thread->cancel_running, 0, 1) != 0) {
				handled = 1;
			} else {
				handled = __plat_thread_redirect_ip(thread->handle,
					(void *)__pthread_cancel_trampoline) == 0;
				if (!handled)
					compare_exchange(&thread->cancel_running, 1, 0);
			}
		}
	}
	__plat_thread_resume(thread->handle);
	if (unsafe) cancel_unsafe_abort(unsafe_region);
	return handled;
}

static _Noreturn void cancel_current_claimed(void)
{
	struct __pthread *self = __pthread_self_control;
	if (self) {
		RtlAcquirePebLock();
		self->cancel_pending = 0;
		self->cancel_queued = 0;
		self->cancel_state = PTHREAD_CANCEL_DISABLE;
		RtlReleasePebLock();
	}
	pthread_exit(PTHREAD_CANCELED);
}

void __pthread_cancel_current(void)
{
	struct __pthread *self = __pthread_self_control;
	if (self && atomic_load(&self->cancel_defer_depth) > 0) return;
	if (self && self->cancel_state == PTHREAD_CANCEL_ENABLE &&
	    self->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS &&
	    atomic_load(&self->cancel_unsafe_depth) > 0)
		cancel_unsafe_abort(self->cancel_unsafe_region);
	if (self && compare_exchange(&self->cancel_running, 0, 1) != 0) return;
	cancel_current_claimed();
}

/* The suspend/context path claimed cancel_running before publishing this
 * instruction pointer, so its trampoline must not try to claim it again. */
_Noreturn void __pthread_cancel_redirected(void)
{
	cancel_current_claimed();
}

void __pthread_testcancel(void)
{
	struct __pthread *self = __pthread_self_control;
	int cancel = 0;
	if (!self) return;
	RtlAcquirePebLock();
	if (self->cancel_state == PTHREAD_CANCEL_ENABLE && self->cancel_pending)
		cancel = 1;
	RtlReleasePebLock();
	if (cancel) __pthread_cancel_current();
}

static void __PLAT_APC_CALL cancel_apc(void *argument, void *unused1, void *unused2)
{
	struct __pthread *self = argument;
	int cancel = 0;
	(void)unused1;
	(void)unused2;
	RtlAcquirePebLock();
	self->cancel_queued = 0;
	if (self->cancel_state == PTHREAD_CANCEL_ENABLE && self->cancel_pending &&
	    self->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS)
		cancel = 1;
	RtlReleasePebLock();
	if (cancel) __pthread_cancel_current();
}

int pthread_cancel(pthread_t thread)
{
	int queue = 0;
	int redirect = 0;
	int cancel_self = 0;
	if (!thread || thread->magic != PTHREAD_MAGIC) return ESRCH;
	__pthread_cancel_defer_enter();
	RtlAcquirePebLock();
	if (thread->joined || (!thread->handle && thread->exited)) {
		RtlReleasePebLock();
		__pthread_cancel_defer_leave();
		return ESRCH;
	}
	thread->cancel_pending = 1;
	if (thread->cancel_state == PTHREAD_CANCEL_ENABLE &&
	    !thread->cancel_queued && thread->handle) {
		thread->cancel_queued = 1;
		redirect = thread != __pthread_self_control &&
			thread->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS;
		queue = !redirect;
	}
	cancel_self = thread == __pthread_self_control &&
		thread->cancel_state == PTHREAD_CANCEL_ENABLE &&
		thread->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS;
	RtlReleasePebLock();
	if (cancel_self) __pthread_cancel_current();
	if (redirect && !redirect_async_cancel(thread)) queue = 1;
	if (queue && __plat_thread_queue_apc(thread->handle, cancel_apc,
		thread, 0) < 0) {
		RtlAcquirePebLock();
		thread->cancel_queued = 0;
		RtlReleasePebLock();
	}
	__pthread_cancel_defer_leave();
	return 0;
}

int pthread_setcancelstate(int state, int *old_state)
{
	struct __pthread *self;
	int cancel;
	if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
		return EINVAL;
	self = __pthread_current();
	if (!self) return ENOMEM;
	__pthread_cancel_defer_enter();
	RtlAcquirePebLock();
	if (old_state) *old_state = self->cancel_state;
	self->cancel_state = state;
	cancel = state == PTHREAD_CANCEL_ENABLE && self->cancel_pending &&
	         self->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS;
	RtlReleasePebLock();
	if (cancel) __pthread_cancel_current();
	__pthread_cancel_defer_leave();
	return 0;
}

int pthread_setcanceltype(int type, int *old_type)
{
	struct __pthread *self;
	int cancel;
	if (type != PTHREAD_CANCEL_DEFERRED &&
	    type != PTHREAD_CANCEL_ASYNCHRONOUS) return EINVAL;
	self = __pthread_current();
	if (!self) return ENOMEM;
	__pthread_cancel_defer_enter();
	RtlAcquirePebLock();
	if (old_type) *old_type = self->cancel_type;
	self->cancel_type = type;
	cancel = type == PTHREAD_CANCEL_ASYNCHRONOUS && self->cancel_pending &&
	         self->cancel_state == PTHREAD_CANCEL_ENABLE;
	RtlReleasePebLock();
	if (cancel) __pthread_cancel_current();
	__pthread_cancel_defer_leave();
	return 0;
}

void pthread_testcancel(void)
{
	__pthread_testcancel();
}
