/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_thread.h -- see that header for
 * the contract each function makes.  Everything here was, until this file
 * existed, inline inside src/thread/{pthread,pthread_cancel,pthread_cond,
 * pthread_mutex,pthread_rwlock,pthread_signal,pthread_sync,pthread_tsd,
 * semaphore,mqueue,aio}.c; nothing changed in substance, only location and
 * the addition of a POSIX-shaped return (0/-1 with errno set, or the small
 * __PLAT_WAIT_* enum for a wait) in place of a raw NTSTATUS.
 */
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_thread.h"

/* Shared by every \BaseNamedObjects-rooted object this file creates or
 * opens: `ascii` is already the fully-qualified path, ASCII by
 * construction (a hash or a pid/sequence pair formatted by the front
 * door).  `openif` additionally clears OBJ_INHERIT and sets OBJ_OPENIF,
 * matching the one caller (the named-mutant lock) that needs create-or-
 * open object-manager semantics instead of plain inheritable-by-fork
 * creation. 128 WCHARs comfortably covers every name this subsystem
 * builds (the longest, a message queue's per-generation semaphore name,
 * is capped at 112 *bytes* -- see mqueue.c's struct mq_header -- well
 * under half this budget), so nothing that fit before can be truncated by
 * sharing one buffer size across every named-object call here. */
static void build_object_attributes(const char *ascii, OBJECT_ATTRIBUTES *oa,
	UNICODE_STRING *us, WCHAR *wide, size_t cap, int openif)
{
	size_t i, n = strlen(ascii);
	if (n >= cap) n = cap - 1;
	for (i = 0; i < n; i++) wide[i] = (unsigned char)ascii[i];
	wide[n] = 0;
	if (n > __US_MAX_WCHARS) n = __US_MAX_WCHARS;
	us->Length = (USHORT)(n * sizeof(WCHAR));
	us->MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
	us->Buffer = wide;
	InitializeObjectAttributes(oa, us, OBJ_CASE_INSENSITIVE | OBJ_INHERIT, 0, 0);
	if (openif) oa->Attributes = (oa->Attributes & ~OBJ_INHERIT) | OBJ_OPENIF;
}

/* ---- waiting -------------------------------------------------------- */

int __plat_wait_one(__plat_handle_t h, int alertable, int has_timeout,
	long long relative_ticks)
{
	LARGE_INTEGER timeout, *tp = 0;
	NTSTATUS st;
	if (has_timeout) { timeout = relative_ticks; tp = &timeout; }
	st = NtWaitForSingleObject(h, (BOOLEAN)alertable, tp);
	if (st == STATUS_USER_APC || st == STATUS_ALERTED) return __PLAT_WAIT_INTR;
	if (st == STATUS_TIMEOUT) return __PLAT_WAIT_TIMEOUT;
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return __PLAT_WAIT_ERROR; }
	return __PLAT_WAIT_OK;
}

int __plat_wait_any(__plat_handle_t *handles, unsigned count, int alertable,
	int has_timeout, long long relative_ticks)
{
	LARGE_INTEGER timeout, *tp = 0;
	NTSTATUS st;
	if (has_timeout) { timeout = relative_ticks; tp = &timeout; }
	st = NtWaitForMultipleObjects((ULONG)count, (HANDLE *)handles,
	                              1 /* WaitAny */, (BOOLEAN)alertable, tp);
	if (st == STATUS_USER_APC || st == STATUS_ALERTED) return __PLAT_WAIT_INTR;
	if (st == STATUS_TIMEOUT) return __PLAT_WAIT_TIMEOUT;
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return __PLAT_WAIT_ERROR; }
	return __PLAT_WAIT_OK;
}

/* ---- events ----------------------------------------------------------- */

int __plat_event_create(__plat_handle_t *out)
{
	OBJECT_ATTRIBUTES attributes;
	NTSTATUS status;
	InitializeObjectAttributes(&attributes, 0, 0, 0, 0);
	status = NtCreateEvent(out, EVENT_ALL_ACCESS, &attributes,
	                       SynchronizationEvent, FALSE);
	if (status == STATUS_NOT_IMPLEMENTED) return -2;
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	return 0;
}

int __plat_event_set(__plat_handle_t h)
{
	LONG previous;
	NTSTATUS status = NtSetEvent(h, &previous);
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	return 0;
}

/* ---- unnamed semaphores ------------------------------------------------ */

int __plat_semaphore_create(long initial, long maximum, int inheritable,
	__plat_handle_t *out)
{
	OBJECT_ATTRIBUTES oa;
	NTSTATUS st;
	InitializeObjectAttributes(&oa, 0, inheritable ? OBJ_INHERIT : 0, 0, 0);
	st = NtCreateSemaphore(out, SEMAPHORE_ALL_ACCESS, &oa, (LONG)initial, (LONG)maximum);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_semaphore_post(__plat_handle_t h)
{
	NTSTATUS st = NtReleaseSemaphore(h, 1, NULL);
	/* [EOVERFLOW] decided here, not reconstructed from a generic errno
	 * afterward: see plat_thread.h's banner and src/unistd/nt/plat_fd.c's
	 * SIGPIPE comment for why this class of decision belongs where the
	 * real status is still in hand. */
	if (st == STATUS_SEMAPHORE_LIMIT_EXCEEDED) { errno = EOVERFLOW; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_semaphore_getvalue(__plat_handle_t h, int *value)
{
	SEMAPHORE_BASIC_INFORMATION info;
	NTSTATUS st = NtQuerySemaphore(h, SemaphoreBasicInformation,
	                               &info, sizeof info, NULL);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*value = info.CurrentCount;
	return 0;
}

/* ---- named objects ------------------------------------------------------ */

int __plat_named_semaphore_create(const char *name, long initial,
	long maximum, __plat_handle_t *out)
{
	WCHAR wide[128];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	NTSTATUS st;
	build_object_attributes(name, &oa, &us, wide, sizeof wide / sizeof wide[0], 0);
	st = NtCreateSemaphore(out, SEMAPHORE_ALL_ACCESS, &oa, (LONG)initial, (LONG)maximum);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_named_semaphore_open(const char *name, __plat_handle_t *out)
{
	WCHAR wide[128];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	NTSTATUS st;
	build_object_attributes(name, &oa, &us, wide, sizeof wide / sizeof wide[0], 0);
	st = NtOpenSemaphore(out, SEMAPHORE_ALL_ACCESS, &oa);
	if (st == STATUS_OBJECT_NAME_NOT_FOUND) return -2;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_named_semaphore_open_or_create(const char *name, long initial,
	long maximum, __plat_handle_t *out)
{
	WCHAR wide[128];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	NTSTATUS st;
	build_object_attributes(name, &oa, &us, wide, sizeof wide / sizeof wide[0], 0);
	st = NtCreateSemaphore(out, SEMAPHORE_ALL_ACCESS, &oa, (LONG)initial, (LONG)maximum);
	/* Wine reports an existing named semaphore as the error status
	 * STATUS_OBJECT_NAME_COLLISION rather than NT's informational
	 * STATUS_OBJECT_NAME_EXISTS.  Opening it is the same create-or-open
	 * contract every caller of this function needs. */
	if (st == STATUS_OBJECT_NAME_COLLISION)
		st = NtOpenSemaphore(out, SEMAPHORE_ALL_ACCESS, &oa);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_named_mutant_acquire(const char *name, __plat_handle_t *out)
{
	WCHAR wide[128];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	NTSTATUS st;
	build_object_attributes(name, &oa, &us, wide, sizeof wide / sizeof wide[0], 1);
	st = NtCreateMutant(out, MUTANT_ALL_ACCESS, &oa, FALSE);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	st = NtWaitForSingleObject(*out, FALSE, NULL);
	if (!NT_SUCCESS(st)) {
		NtClose(*out);
		*out = 0;
		return __set_errno_status(st);
	}
	return 0;
}

void __plat_named_mutant_release(__plat_handle_t lock)
{
	NtReleaseMutant(lock, NULL);
	NtClose(lock);
}

/* ---- thread lifecycle ---------------------------------------------------- */

#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 1u

int __plat_thread_spawn(__plat_thread_entry_t entry, void *arg,
	size_t stack_size, int create_suspended, __plat_handle_t *out)
{
	NTSTATUS status = NtCreateThreadEx(out, THREAD_ALL_ACCESS, 0,
		NtCurrentProcess(), (PVOID)entry, arg,
		create_suspended ? THREAD_CREATE_FLAGS_CREATE_SUSPENDED : 0,
		0, stack_size, stack_size, 0);
	if (status == STATUS_NOT_IMPLEMENTED) return -2;
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	return 0;
}

/* __plat_thread_resume() is declared above (this file's own header
 * comment) but defined in src/process/nt/plat_process.c, not here: both
 * this file (pthread_create()/pthread_cancel()'s deferred-cancellation
 * resume) and the process subsystem's own post-fork/post-spawn resume
 * independently arrived at an NtResumeThread()-wrapping implementation
 * during the platform-abstraction migration, and only one definition
 * may exist per the ODR. Kept where process/thread lifecycle otherwise
 * lives; this file just uses it. */

int __plat_thread_suspend(__plat_handle_t h)
{
	ULONG previous;
	NTSTATUS status = NtSuspendThread(h, &previous);
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	return 0;
}

int __plat_thread_queue_apc(__plat_handle_t h, __plat_apc_fn fn, void *arg1,
	void *arg2)
{
	NTSTATUS status = NtQueueApcThread(h, (PKNORMAL_ROUTINE)fn, arg1, arg2, 0);
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	return 0;
}

int __plat_thread_redirect_ip(__plat_handle_t h, void *target)
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
	ULONG_PTR ip = (ULONG_PTR)target;
	NTSTATUS status;

	memset(context, 0, sizeof storage - 15);
	memcpy(context + flags_offset, &flags, sizeof flags);
	status = NtGetContextThread(h, context);
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	memcpy(context + ip_offset, &ip, sizeof ip);
	status = NtSetContextThread(h, context);
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	return 0;
}

int __plat_thread_stack_extent(__plat_handle_t h, void **base, size_t *size)
{
	THREAD_BASIC_INFORMATION information;
	PTEB teb;
	NTSTATUS status = NtQueryInformationThread(h, ThreadBasicInformation,
		&information, sizeof information, 0);
	if (!NT_SUCCESS(status)) return __set_errno_status(status);
	teb = information.TebBaseAddress;
	*base = teb->NtTib.StackLimit;
	*size = (size_t)((char *)teb->NtTib.StackBase - (char *)teb->NtTib.StackLimit);
	return 0;
}

__plat_handle_t __plat_thread_duplicate_self(void)
{
	HANDLE handle;
	if (NT_SUCCESS(NtDuplicateObject(NtCurrentProcess(), NtCurrentThread(),
		NtCurrentProcess(), &handle, 0, 0, DUPLICATE_SAME_ACCESS)))
		return handle;
	return NtCurrentThread();
}

__plat_handle_t __plat_thread_current_pseudo(void)
{
	return NtCurrentThread();
}

_Noreturn void __plat_thread_terminate_self(void)
{
	NtTerminateThread(NtCurrentThread(), 0);
	for (;;) NtTerminateThread(NtCurrentThread(), 0);
}

/* The write deliberately bypasses stdio and the fd table: the suspended
 * target may own either one's locks.  Termination likewise goes straight
 * to NT instead of abort()/__nt_exit(), whose signal and child bookkeeping
 * are not async-cancel-safe themselves. */
_Noreturn void __plat_cancel_unsafe_abort(const char *region)
{
	static const char prefix[] =
		"ntlibc: undefined behavior: asynchronous cancellation during ";
	static const char suffix[] = "\r\n";
	IO_STATUS_BLOCK io;
	HANDLE error = 0;
	size_t length = 0;

	if (__peb && __peb->ProcessParameters)
		error = __peb->ProcessParameters->StandardError;
	if (!region) region = "an async-cancel-unsafe operation";
	while (region[length]) length++;
	if (error) {
		NtWriteFile(error, 0, 0, 0, &io, prefix,
			sizeof prefix - 1, 0, 0);
		NtWriteFile(error, 0, 0, 0, &io, region, (ULONG)length, 0, 0);
		NtWriteFile(error, 0, 0, 0, &io, suffix,
			sizeof suffix - 1, 0, 0);
	}
	NtTerminateProcess(NtCurrentProcess(), __NT_SIGNAL_EXIT(SIGABRT));
	for (;;) NtTerminateProcess(NtCurrentProcess(),
		__NT_SIGNAL_EXIT(SIGABRT));
}

void __plat_thread_alertable_yield(void)
{
	LARGE_INTEGER delay = 0;
	NtDelayExecution(TRUE, &delay);
}

long long __plat_query_system_time(void)
{
	LARGE_INTEGER t;
	NtQuerySystemTime(&t);
	return t;
}

/* ---- mqueue.c's queue-file I/O -------------------------------------------- */

ssize_t __plat_thread_file_io(__plat_handle_t h, void *buf, size_t count,
	off_t off, int write_op)
{
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos = off;
	ULONG part = count > 0x7fffffff ? 0x7fffffff : (ULONG)count;
	NTSTATUS st;
	io.Information = 0;
	if (write_op)
		st = NtWriteFile(h, 0, 0, 0, &io, buf, part, &pos, 0);
	else
		st = NtReadFile(h, 0, 0, 0, &io, buf, part, &pos, 0);
	if (st == STATUS_PENDING) {
		NtWaitForSingleObject(h, FALSE, NULL);
		st = io.Status;
	}
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}
