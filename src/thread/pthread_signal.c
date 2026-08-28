/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include "pthread_impl.h"

static void NTAPI signal_apc(PVOID argument, PVOID signal_value, PVOID unused)
{
	struct __pthread *thread = argument;
	(void)unused;
	/* Native user APCs queued immediately after creation may run before
	 * thread_entry().  Establish the control block and inherited mask before
	 * invoking user code so pthread_self() already names the target thread. */
	if (!__pthread_self_control) {
		__pthread_self_control = thread;
		__sig_current_mask_install(&thread->sigmask);
	}
	__sig_lock();
	__raise_thread_internal((int)(ULONG_PTR)signal_value);
	__sig_unlock();
}

int pthread_kill(pthread_t thread, int sig)
{
	NTSTATUS status;
	if (!thread || thread->magic != PTHREAD_MAGIC) return ESRCH;
	if (sig < 0 || sig >= _NSIG) return EINVAL;
	RtlAcquirePebLock();
	if (thread->joined || (!thread->handle && thread->exited)) {
		RtlReleasePebLock();
		return ESRCH;
	}
	RtlReleasePebLock();
	if (!sig) return 0;
	if (thread == __pthread_self_control) {
		signal_apc(thread, (PVOID)(ULONG_PTR)sig, 0);
		return 0;
	}
	status = NtQueueApcThread(thread->handle, signal_apc, thread,
		(PVOID)(ULONG_PTR)sig, 0);
	return NT_SUCCESS(status) ? 0 : ESRCH;
}
