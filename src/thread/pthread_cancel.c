/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include "pthread_impl.h"

_Noreturn void __pthread_cancel_trampoline(void);

/* A normal user APC is only delivered when the target enters an alertable
 * wait.  Asynchronous cancellation also has to stop a thread which never
 * waits (the conformance test intentionally uses a tight loop), so redirect
 * the suspended target to an arch trampoline which cannot return. */
static int redirect_async_cancel(struct __pthread *thread)
{
#if defined(__x86_64__)
	unsigned char storage[0x4d0 + 15];
	const ULONG flags = 0x100001; /* CONTEXT_AMD64 | CONTEXT_CONTROL */
	const size_t flags_offset = 0x30;
	const size_t ip_offset = 0xf8;
#elif defined(__i386__)
	unsigned char storage[0x2cc + 15];
	const ULONG flags = 0x10001; /* CONTEXT_i386 | CONTEXT_CONTROL */
	const size_t flags_offset = 0;
	const size_t ip_offset = 0xb8;
#else
# error unsupported architecture
#endif
	unsigned char *context = (unsigned char *)
		(((ULONG_PTR)storage + 15) & ~(ULONG_PTR)15);
	ULONG_PTR ip = (ULONG_PTR)__pthread_cancel_trampoline;
	ULONG previous;
	NTSTATUS status;
	int handled = 0;

	memset(context, 0, sizeof storage - 15);
	memcpy(context + flags_offset, &flags, sizeof flags);
	status = NtSuspendThread(thread->handle, &previous);
	if (!NT_SUCCESS(status)) return 0;
	/* The target can consume the request at a cancellation point after
	 * pthread_cancel() drops the PEB lock but before it is suspended.  Do
	 * not redirect it a second time while its cleanup handlers are already
	 * running: that abandons the first handler's stack frame. */
	if (!thread->cancel_pending || thread->cancel_running || thread->exited) {
		handled = 1;
	} else if (thread->cancel_state == PTHREAD_CANCEL_ENABLE &&
	    thread->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS) {
		status = NtGetContextThread(thread->handle, context);
		if (NT_SUCCESS(status)) {
			memcpy(context + ip_offset, &ip, sizeof ip);
			status = NtSetContextThread(thread->handle, context);
		}
		handled = NT_SUCCESS(status);
	}
	NtResumeThread(thread->handle, &previous);
	return handled;
}

_Noreturn void __pthread_cancel_current(void)
{
	struct __pthread *self = __pthread_self_control;
	if (self) {
		/* Publish this before taking the shared lock.  A concurrent
		 * redirector suspends this thread before consulting the marker, so
		 * it cannot race past a completed store. */
		self->cancel_running = 1;
		RtlAcquirePebLock();
		self->cancel_pending = 0;
		self->cancel_queued = 0;
		self->cancel_state = PTHREAD_CANCEL_DISABLE;
		RtlReleasePebLock();
	}
	pthread_exit(PTHREAD_CANCELED);
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

static void NTAPI cancel_apc(PVOID argument, PVOID unused1, PVOID unused2)
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
	RtlAcquirePebLock();
	if (thread->joined || (!thread->handle && thread->exited)) {
		RtlReleasePebLock();
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
	if (queue && !NT_SUCCESS(NtQueueApcThread(thread->handle, cancel_apc,
		thread, 0, 0))) {
		RtlAcquirePebLock();
		thread->cancel_queued = 0;
		RtlReleasePebLock();
	}
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
	RtlAcquirePebLock();
	if (old_state) *old_state = self->cancel_state;
	self->cancel_state = state;
	cancel = state == PTHREAD_CANCEL_ENABLE && self->cancel_pending &&
	         self->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS;
	RtlReleasePebLock();
	if (cancel) __pthread_cancel_current();
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
	RtlAcquirePebLock();
	if (old_type) *old_type = self->cancel_type;
	self->cancel_type = type;
	cancel = type == PTHREAD_CANCEL_ASYNCHRONOUS && self->cancel_pending &&
	         self->cancel_state == PTHREAD_CANCEL_ENABLE;
	RtlReleasePebLock();
	if (cancel) __pthread_cancel_current();
	return 0;
}

void pthread_testcancel(void)
{
	__pthread_testcancel();
}
