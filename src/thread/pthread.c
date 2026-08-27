/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX thread lifecycle over native NT threads.  A pthread_t names a small
 * process-local control block; the NT thread handle is retained until join
 * or detached termination and supplies the kernel-signalled completion
 * object.  Control blocks remain as tombstones after resource reclamation,
 * which makes stale IDs diagnosable without dereferencing freed storage. */
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pthread_impl.h"

#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 1u
#define DEFAULT_STACK_SIZE (1024u * 1024u)
#define DEFAULT_GUARD_SIZE 4096u

static int concurrency;

__thread struct __pthread *__pthread_self_control;

static struct __pthread_attr_data *attr_data(pthread_attr_t *attr)
{
	return (struct __pthread_attr_data *)(void *)attr;
}

static const struct __pthread_attr_data *const_attr_data(const pthread_attr_t *attr)
{
	return (const struct __pthread_attr_data *)(const void *)attr;
}

static int valid_attr(const pthread_attr_t *attr)
{
	return attr && const_attr_data(attr)->magic == PTHREAD_ATTR_MAGIC;
}

struct __pthread *__pthread_current(void)
{
	struct __pthread *self = __pthread_self_control;
	if (self) return self;
	self = calloc(1, sizeof *self);
	if (!self) return 0;
	self->magic = PTHREAD_MAGIC;
	self->handle = NtCurrentThread();
	self->cancel_state = PTHREAD_CANCEL_ENABLE;
	self->cancel_type = PTHREAD_CANCEL_DEFERRED;
	self->sched_policy = SCHED_OTHER;
	__pthread_self_control = self;
	return self;
}

pthread_t pthread_self(void)
{
	return __pthread_current();
}

int pthread_equal(pthread_t left, pthread_t right)
{
	return left == right;
}

void __pthread_cleanup_push(struct __pthread_cleanup *cleanup)
{
	struct __pthread *self = __pthread_current();
	if (!self) return;
	cleanup->__previous = self->cleanup;
	self->cleanup = cleanup;
}

void __pthread_cleanup_pop(struct __pthread_cleanup *cleanup, int execute)
{
	struct __pthread *self = __pthread_current();
	if (!self || self->cleanup != cleanup) return;
	self->cleanup = cleanup->__previous;
	if (execute) cleanup->__routine(cleanup->__argument);
}

static void finish(struct __pthread *self, void *result)
{
	int detached;
	while (self->cleanup) {
		struct __pthread_cleanup *cleanup = self->cleanup;
		self->cleanup = cleanup->__previous;
		cleanup->__routine(cleanup->__argument);
	}
	__pthread_run_specific_destructors(self);
	RtlAcquirePebLock();
	self->result = result;
	self->exited = 1;
	detached = self->detached;
	RtlReleasePebLock();
	if (detached && self->handle && self->handle != NtCurrentThread()) {
		NtClose(self->handle);
		self->handle = 0;
	}
}

static ULONG NTAPI thread_entry(PVOID argument)
{
	struct __pthread *self = argument;
	void *result;
	__pthread_self_control = self;
	result = self->start(self->argument);
	finish(self, result);
	return 0;
}

int pthread_create(pthread_t *__restrict output,
	const pthread_attr_t *__restrict attr, void *(*start)(void *),
	void *__restrict argument)
{
	const struct __pthread_attr_data *data = 0;
	struct __pthread *creator = 0;
	struct __pthread *thread;
	HANDLE handle;
	NTSTATUS status;
	ULONG previous;

	if (!output || !start) return EINVAL;
	if (attr) {
		if (!valid_attr(attr)) return EINVAL;
		data = const_attr_data(attr);
	}
	if (!data || data->inherit_sched == PTHREAD_INHERIT_SCHED) {
		creator = __pthread_current();
		if (!creator) return EAGAIN;
	}
	thread = calloc(1, sizeof *thread);
	if (!thread) return EAGAIN;
	thread->magic = PTHREAD_MAGIC;
	thread->start = start;
	thread->argument = argument;
	thread->detached = data && data->detach_state == PTHREAD_CREATE_DETACHED;
	thread->cancel_state = PTHREAD_CANCEL_ENABLE;
	thread->cancel_type = PTHREAD_CANCEL_DEFERRED;
	if (creator) {
		RtlAcquirePebLock();
		thread->sched_policy = creator->sched_policy;
		thread->sched_priority = creator->sched_priority;
		RtlReleasePebLock();
	} else {
		thread->sched_policy = data->sched_policy;
		thread->sched_priority = data->sched_priority;
	}
	status = NtCreateThreadEx(&handle, THREAD_ALL_ACCESS, 0, NtCurrentProcess(),
		(PVOID)thread_entry, thread, THREAD_CREATE_FLAGS_CREATE_SUSPENDED,
		0, data ? data->stack_size : DEFAULT_STACK_SIZE,
		data ? data->stack_size : DEFAULT_STACK_SIZE, 0);
	if (!NT_SUCCESS(status)) {
		free(thread);
		return status == STATUS_NO_MEMORY ? EAGAIN : EAGAIN;
	}
	thread->handle = handle;
	*output = thread;
	status = NtResumeThread(handle, &previous);
	if (!NT_SUCCESS(status)) {
		NtClose(handle);
		thread->handle = 0;
		thread->joined = 1;
		return EAGAIN;
	}
	return 0;
}

int pthread_join(pthread_t thread, void **result)
{
	NTSTATUS status;
	if (!thread || thread->magic != PTHREAD_MAGIC) return ESRCH;
	if (thread == __pthread_current()) return EDEADLK;
	RtlAcquirePebLock();
	if (thread->detached) {
		RtlReleasePebLock();
		return EINVAL;
	}
	if (thread->joined || thread->joining || !thread->handle) {
		RtlReleasePebLock();
		return ESRCH;
	}
	thread->joining = 1;
	RtlReleasePebLock();
	status = NtWaitForSingleObject(thread->handle, TRUE, 0);
	if (!NT_SUCCESS(status)) {
		RtlAcquirePebLock();
		thread->joining = 0;
		RtlReleasePebLock();
		return status == STATUS_USER_APC || status == STATUS_ALERTED ? EINTR : EINVAL;
	}
	if (result) *result = thread->result;
	NtClose(thread->handle);
	thread->handle = 0;
	thread->joining = 0;
	thread->joined = 1;
	return 0;
}

int pthread_detach(pthread_t thread)
{
	int close_handle;
	if (!thread || thread->magic != PTHREAD_MAGIC) return ESRCH;
	RtlAcquirePebLock();
	if (thread->joined || (!thread->handle && thread->exited)) {
		RtlReleasePebLock();
		return ESRCH;
	}
	if (thread->detached || thread->joining) {
		RtlReleasePebLock();
		return EINVAL;
	}
	thread->detached = 1;
	close_handle = thread->exited && thread->handle != 0;
	RtlReleasePebLock();
	if (close_handle) {
		NtClose(thread->handle);
		thread->handle = 0;
	}
	return 0;
}

_Noreturn void pthread_exit(void *result)
{
	struct __pthread *self = __pthread_current();
	if (self) finish(self, result);
	NtTerminateThread(NtCurrentThread(), 0);
	for (;;) NtTerminateThread(NtCurrentThread(), 0);
}

int pthread_attr_init(pthread_attr_t *attr)
{
	struct __pthread_attr_data *data;
	if (!attr) return EINVAL;
	memset(attr, 0, sizeof *attr);
	data = attr_data(attr);
	data->magic = PTHREAD_ATTR_MAGIC;
	data->stack_size = DEFAULT_STACK_SIZE;
	data->guard_size = DEFAULT_GUARD_SIZE;
	data->detach_state = PTHREAD_CREATE_JOINABLE;
	data->scope = PTHREAD_SCOPE_SYSTEM;
	data->inherit_sched = PTHREAD_INHERIT_SCHED;
	data->sched_policy = SCHED_OTHER;
	return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
	if (!valid_attr(attr)) return EINVAL;
	memset(attr, 0, sizeof *attr);
	return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *__restrict attr,
	int *__restrict value)
{
	if (!valid_attr(attr) || !value) return EINVAL;
	*value = const_attr_data(attr)->detach_state;
	return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t *__restrict attr,
	size_t *__restrict value)
{
	if (!valid_attr(attr) || !value) return EINVAL;
	*value = const_attr_data(attr)->guard_size;
	return 0;
}

int pthread_attr_getinheritsched(const pthread_attr_t *__restrict attr,
	int *__restrict value)
{
	if (!valid_attr(attr) || !value) return EINVAL;
	*value = const_attr_data(attr)->inherit_sched;
	return 0;
}

int pthread_attr_getschedpolicy(const pthread_attr_t *__restrict attr,
	int *__restrict value)
{
	if (!valid_attr(attr) || !value) return EINVAL;
	*value = const_attr_data(attr)->sched_policy;
	return 0;
}

int pthread_attr_getscope(const pthread_attr_t *__restrict attr,
	int *__restrict value)
{
	if (!valid_attr(attr) || !value) return EINVAL;
	*value = const_attr_data(attr)->scope;
	return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *__restrict attr,
	size_t *__restrict value)
{
	if (!valid_attr(attr) || !value) return EINVAL;
	*value = const_attr_data(attr)->stack_size;
	return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int value)
{
	if (!valid_attr(attr) || (value != PTHREAD_CREATE_JOINABLE &&
	    value != PTHREAD_CREATE_DETACHED)) return EINVAL;
	attr_data(attr)->detach_state = value;
	return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t value)
{
	if (!valid_attr(attr)) return EINVAL;
	attr_data(attr)->guard_size = value;
	return 0;
}

int pthread_attr_setinheritsched(pthread_attr_t *attr, int value)
{
	if (!valid_attr(attr) || (value != PTHREAD_INHERIT_SCHED &&
	    value != PTHREAD_EXPLICIT_SCHED)) return EINVAL;
	attr_data(attr)->inherit_sched = value;
	return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int value)
{
	if (!valid_attr(attr) || value < SCHED_OTHER || value > SCHED_SPORADIC)
		return EINVAL;
	attr_data(attr)->sched_policy = value;
	return 0;
}

int pthread_attr_setscope(pthread_attr_t *attr, int value)
{
	if (!valid_attr(attr)) return EINVAL;
	if (value == PTHREAD_SCOPE_PROCESS) return ENOTSUP;
	if (value != PTHREAD_SCOPE_SYSTEM) return EINVAL;
	attr_data(attr)->scope = value;
	return 0;
}

int pthread_attr_getschedparam(const pthread_attr_t *__restrict attr,
	struct sched_param *__restrict parameter)
{
	if (!valid_attr(attr) || !parameter) return EINVAL;
	parameter->sched_priority = const_attr_data(attr)->sched_priority;
	return 0;
}

int pthread_attr_setschedparam(pthread_attr_t *__restrict attr,
	const struct sched_param *__restrict parameter)
{
	const struct __pthread_attr_data *data;
	int minimum, maximum;
	if (!valid_attr(attr) || !parameter) return EINVAL;
	data = const_attr_data(attr);
	minimum = sched_get_priority_min(data->sched_policy);
	maximum = sched_get_priority_max(data->sched_policy);
	if (minimum < 0 || maximum < 0 || parameter->sched_priority < minimum ||
	    parameter->sched_priority > maximum) return EINVAL;
	attr_data(attr)->sched_priority = parameter->sched_priority;
	return 0;
}

int pthread_attr_getstack(const pthread_attr_t *__restrict attr,
	void **__restrict address, size_t *__restrict size)
{
	if (!valid_attr(attr) || !address || !size) return EINVAL;
	*address = const_attr_data(attr)->stack_address;
	*size = const_attr_data(attr)->stack_size;
	return 0;
}

int pthread_attr_setstack(pthread_attr_t *attr, void *address, size_t size)
{
	if (!valid_attr(attr) || !address || size < PTHREAD_STACK_MIN) return EINVAL;
	attr_data(attr)->stack_address = address;
	attr_data(attr)->stack_size = size;
	return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size)
{
	if (!valid_attr(attr) || size < PTHREAD_STACK_MIN) return EINVAL;
	attr_data(attr)->stack_size = size;
	return 0;
}

int pthread_attr_getstackaddr(const pthread_attr_t *__restrict attr,
	void **__restrict address)
{
	if (!valid_attr(attr) || !address) return EINVAL;
	*address = const_attr_data(attr)->stack_address;
	return 0;
}

int pthread_attr_setstackaddr(pthread_attr_t *attr, void *address)
{
	if (!valid_attr(attr) || !address) return EINVAL;
	attr_data(attr)->stack_address = address;
	return 0;
}

int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr)
{
	THREAD_BASIC_INFORMATION information;
	PTEB teb;
	HANDLE handle;
	NTSTATUS status;
	if (!thread || thread->magic != PTHREAD_MAGIC || !attr) return ESRCH;
	if (pthread_attr_init(attr)) return EINVAL;
	handle = thread == __pthread_current() ? NtCurrentThread() : thread->handle;
	if (!handle) return ESRCH;
	status = NtQueryInformationThread(handle, ThreadBasicInformation,
		&information, sizeof information, 0);
	if (!NT_SUCCESS(status)) return ESRCH;
	teb = information.TebBaseAddress;
	attr_data(attr)->stack_address = teb->NtTib.StackLimit;
	attr_data(attr)->stack_size = (size_t)((char *)teb->NtTib.StackBase -
		(char *)teb->NtTib.StackLimit);
	attr_data(attr)->detach_state = thread->detached ?
		PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
	return 0;
}

static int valid_policy(int policy)
{
	return policy == SCHED_OTHER || policy == SCHED_FIFO ||
	       policy == SCHED_RR || policy == SCHED_SPORADIC;
}

static int valid_priority(int policy, int priority)
{
	int minimum = sched_get_priority_min(policy);
	int maximum = sched_get_priority_max(policy);
	return minimum >= 0 && maximum >= 0 && priority >= minimum &&
	       priority <= maximum;
}

int pthread_getschedparam(pthread_t thread, int *__restrict policy,
	struct sched_param *__restrict parameter)
{
	if (!thread || thread->magic != PTHREAD_MAGIC || thread->joined ||
	    (!thread->handle && thread->exited)) return ESRCH;
	if (!policy || !parameter) return EINVAL;
	RtlAcquirePebLock();
	*policy = thread->sched_policy;
	parameter->sched_priority = thread->sched_priority;
	RtlReleasePebLock();
	return 0;
}

int pthread_setschedparam(pthread_t thread, int policy,
	const struct sched_param *parameter)
{
	if (!thread || thread->magic != PTHREAD_MAGIC || thread->joined ||
	    (!thread->handle && thread->exited)) return ESRCH;
	if (!parameter || !valid_policy(policy) ||
	    !valid_priority(policy, parameter->sched_priority)) return EINVAL;
	RtlAcquirePebLock();
	thread->sched_policy = policy;
	thread->sched_priority = parameter->sched_priority;
	RtlReleasePebLock();
	return 0;
}

int pthread_setschedprio(pthread_t thread, int priority)
{
	int policy;
	if (!thread || thread->magic != PTHREAD_MAGIC || thread->joined ||
	    (!thread->handle && thread->exited)) return ESRCH;
	policy = thread->sched_policy;
	if (!valid_priority(policy, priority)) return EINVAL;
	RtlAcquirePebLock();
	thread->sched_priority = priority;
	RtlReleasePebLock();
	return 0;
}

int pthread_getcpuclockid(pthread_t thread, clockid_t *clock)
{
	if (!thread || thread->magic != PTHREAD_MAGIC || thread->joined ||
	    (!thread->handle && thread->exited)) return ESRCH;
	if (!clock) return EINVAL;
	/* clock_gettime() maps this ID to NT CPU-time accounting.  The clock
	 * implementation currently reports process aggregate time for it, but
	 * it retains the required monotonic CPU-time behavior and public ID. */
	*clock = CLOCK_THREAD_CPUTIME_ID;
	return 0;
}

int pthread_getconcurrency(void)
{
	return concurrency;
}

int pthread_setconcurrency(int level)
{
	if (level < 0) return EINVAL;
	concurrency = level;
	return 0;
}
