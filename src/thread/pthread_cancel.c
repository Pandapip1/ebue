/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include "pthread_impl.h"

_Noreturn void __pthread_cancel_current(void)
{
	struct __pthread *self = __pthread_self_control;
	if (self) {
		RtlAcquirePebLock();
		self->cancel_pending = 0;
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
		queue = 1;
	}
	RtlReleasePebLock();
	if (thread == __pthread_self_control &&
	    thread->cancel_state == PTHREAD_CANCEL_ENABLE &&
	    thread->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS)
		__pthread_cancel_current();
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
