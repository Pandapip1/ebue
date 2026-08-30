/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_signal.h -- see that header
 * for the contract each function makes and for why several of them
 * necessarily take a raw UNICODE_STRING*.  Everything here was, until
 * this file existed, inline inside src/signal/signal.c and
 * sigdelivery.c; nothing changed in substance, only location.
 */
#include <errno.h>
#include <signal.h>
#include <string.h>
#include "libc.h"
#include "plat_signal.h"

__plat_handle_t __plat_sigevent_create(int initially_signalled)
{
	OBJECT_ATTRIBUTES oa;
	HANDLE ev;

	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	if (!NT_SUCCESS(NtCreateEvent(&ev, EVENT_ALL_ACCESS, &oa, SynchronizationEvent,
	                              initially_signalled ? TRUE : FALSE)))
		return __PLAT_HANDLE_NULL;
	return ev;
}

/* __plat_event_set() is declared above (this file's own header comment)
 * but defined in src/thread/nt/plat_thread.c, not here: both this file
 * and the thread subsystem's own generic-event path independently
 * arrived at an identical NtSetEvent()-wrapping implementation during
 * the platform-abstraction migration, and only one definition may
 * exist per the ODR. Kept where the other generic sync primitives
 * (__plat_event_create() et al.) live; this file just uses it. */

void __plat_signal_wait(__plat_handle_t wake_event, int has_timeout, long long ticks)
{
	LARGE_INTEGER t;

	if (wake_event) {
		if (has_timeout) { t = ticks; NtWaitForSingleObject(wake_event, TRUE, &t); }
		else NtWaitForSingleObject(wake_event, TRUE, 0);
		return;
	}
	if (has_timeout) { t = ticks; NtDelayExecution(TRUE, &t); return; }
	{
		LARGE_INTEGER fallback = -1000000; /* 100 ms */
		NtDelayExecution(TRUE, &fallback);
	}
}

__plat_handle_t __plat_signal_pipe_create(const UNICODE_STRING *name)
{
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	LARGE_INTEGER timeout = -1200000000LL;
	HANDLE pipe;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, (UNICODE_STRING *)name, OBJ_CASE_INSENSITIVE, 0, 0);
	st = NtCreateNamedPipeFile(&pipe,
		GENERIC_READ | GENERIC_WRITE | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
		&oa, &io, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_NONALERT, FILE_PIPE_MESSAGE_TYPE,
		FILE_PIPE_MESSAGE_MODE, FILE_PIPE_QUEUE_OPERATION, 2, 4096, 4096,
		&timeout);
	return NT_SUCCESS(st) ? pipe : __PLAT_HANDLE_NULL;
}

int __plat_signal_pipe_listen(__plat_handle_t pipe)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st = NtFsControlFile(pipe, 0, 0, 0, &io, FSCTL_PIPE_LISTEN, 0, 0, 0, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(pipe, 0, 0); st = io.Status; }
	/* A serialized sender can connect to the replacement instance after
	 * it is created but before this thread reaches LISTEN. The instance
	 * is already connected in that case, which is the desired state. */
	if (st == STATUS_PIPE_CONNECTED) st = STATUS_SUCCESS;
	return NT_SUCCESS(st);
}

int __plat_signal_pipe_read(__plat_handle_t h, void *buf, size_t len, size_t *received)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st = NtReadFile(h, 0, 0, 0, &io, buf, (ULONG)len, 0, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(h, 0, 0); st = io.Status; }
	if (!NT_SUCCESS(st)) return -1;
	*received = (size_t)io.Information;
	return 0;
}

int __plat_signal_pipe_write(__plat_handle_t h, const void *buf, size_t len, size_t *sent)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st = NtWriteFile(h, 0, 0, 0, &io, buf, (ULONG)len, 0, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(h, 0, 0); st = io.Status; }
	if (!NT_SUCCESS(st)) return -1;
	*sent = (size_t)io.Information;
	return 0;
}

void __plat_signal_backoff(void)
{
	LARGE_INTEGER d = -1000000; /* 100ms, src/unistd/sleep.c's own idiom */
	NtDelayExecution(0, &d);
}

__plat_handle_t __plat_signal_mutant_create(const UNICODE_STRING *name)
{
	OBJECT_ATTRIBUTES oa;
	HANDLE mutant;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, (UNICODE_STRING *)name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
	st = NtCreateMutant(&mutant, MUTANT_ALL_ACCESS, &oa, FALSE);
	return NT_SUCCESS(st) ? mutant : __PLAT_HANDLE_NULL;
}

int __plat_wait_acquire(__plat_handle_t h)
{
	NTSTATUS st = NtWaitForSingleObject(h, 0, 0);
	return NT_SUCCESS(st);
}

void __plat_mutant_release(__plat_handle_t h)
{
	NtReleaseMutant(h, 0);
}

int __plat_event_peek(__plat_handle_t ev)
{
	LARGE_INTEGER zero = 0;
	return NtWaitForSingleObject(ev, 0, &zero) == STATUS_SUCCESS;
}

__plat_handle_t __plat_signal_pipe_open(const UNICODE_STRING *name)
{
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, (UNICODE_STRING *)name, OBJ_CASE_INSENSITIVE, 0, 0);
	st = NtOpenFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io,
	                FILE_SHARE_READ | FILE_SHARE_WRITE,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	return NT_SUCCESS(st) ? h : __PLAT_HANDLE_NULL;
}

int __plat_thread_start(void *entry, void *arg, __plat_handle_t *out)
{
	HANDLE thr;
	NTSTATUS st = NtCreateThreadEx(&thr, THREAD_ALL_ACCESS, 0, NtCurrentProcess(),
	                               entry, arg, 0, 0, 0, 0, 0);
	if (!NT_SUCCESS(st)) return -1;
	*out = thr;
	return 0;
}

__plat_handle_t __plat_stop_event_create(const UNICODE_STRING *name)
{
	OBJECT_ATTRIBUTES oa;
	HANDLE h;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, (UNICODE_STRING *)name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
	st = NtCreateEvent(&h, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, FALSE);
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return __PLAT_HANDLE_NULL; }
	return h;
}

int __plat_stop_event_probe(const UNICODE_STRING *name, __plat_handle_t *out, int *already_existed)
{
	OBJECT_ATTRIBUTES oa;
	HANDLE h;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, (UNICODE_STRING *)name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
	st = NtCreateEvent(&h, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, FALSE);
	if (!NT_SUCCESS(st)) return -1;
	*out = h;
	*already_existed = (st == (NTSTATUS)STATUS_OBJECT_NAME_EXISTS);
	return 0;
}

int __plat_process_suspend_self(void)
{
	NTSTATUS st = NtSuspendProcess(NtCurrentProcess());
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_process_suspend(__plat_handle_t h)
{
	NTSTATUS st = NtSuspendProcess(h);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* __plat_process_resume() is declared above (this file's own header
 * comment) but defined in src/process/nt/plat_process.c, not here: both
 * this file and the process subsystem's own resume-a-stopped-child path
 * (src/process/children.c) independently arrived at the identical
 * NtResumeProcess()-wrapping implementation during the platform-
 * abstraction migration, and only one definition may exist per the ODR.
 * Kept where process lifecycle otherwise lives; this file just uses it. */

int __plat_kill_open(pid_t pid, int want_suspend_resume, __plat_handle_t *out)
{
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;
	ACCESS_MASK want = PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION;
	HANDLE h;
	NTSTATUS st;

	if (want_suspend_resume) want |= PROCESS_SUSPEND_RESUME;
	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
	cid.UniqueThread = 0;
	st = NtOpenProcess(&h, want, &oa, &cid);
	if (!NT_SUCCESS(st)) {
		errno = st == (NTSTATUS)STATUS_ACCESS_DENIED ? EPERM : ESRCH;
		return -1;
	}
	*out = h;
	return 0;
}

int __plat_kill_terminate(__plat_handle_t h, int exitcode)
{
	NTSTATUS st = NtTerminateProcess(h, exitcode);
	if (!NT_SUCCESS(st) && st != (NTSTATUS)STATUS_PROCESS_IS_TERMINATING)
		return __set_errno_status(st);
	return 0;
}

int __plat_segv_code(void *addr)
{
	MEMORY_BASIC_INFORMATION mbi;
	SIZE_T ret = 0;
	NTSTATUS st;

	memset(&mbi, 0, sizeof mbi);
	st = NtQueryVirtualMemory(NtCurrentProcess(), addr, MemoryBasicInformation, &mbi, sizeof mbi, &ret);
	if (!NT_SUCCESS(st)) return SEGV_MAPERR;
	return mbi.State == MEM_COMMIT ? SEGV_ACCERR : SEGV_MAPERR;
}
