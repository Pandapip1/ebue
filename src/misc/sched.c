/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* NT exposes priority classes and a scheduler quantum, but not distinct
 * POSIX FIFO and round-robin process policies.  Keep the observable
 * policy/priority tuple here so applications can set it and read it back,
 * while include/unistd.h deliberately does not claim the stronger
 * _POSIX_PRIORITY_SCHEDULING option or hard realtime behavior.
 *
 * Foreign-process state is local to the caller.  This is enough to make
 * the process APIs coherent for self and children, including the common
 * set-then-get use, without pretending that another ntlibc process can
 * see a policy distinction the NT kernel itself does not store.
 *
 * sched_yield.html: "The sched_yield() function shall force the running
 * thread to relinquish the processor until it again becomes the head of
 * its thread list."  RETURN VALUE -- "shall return 0 if it completes
 * successfully, or ... -1 and set errno".  ERRORS -- "No errors are
 * defined."
 *
 * NtYieldExecution() is the NTDLL primitive, and takes no arguments.
 * It returns STATUS_SUCCESS when it actually switched away, and the
 * informational (non-error, high bit clear) STATUS_NO_YIELD_PERFORMED
 * 0x40000024 when there was no other runnable thread to switch to --
 * this is what kernel32's SwitchToThread() turns into a FALSE return.
 *
 * That second case is *not* a POSIX failure: the spec's only
 * requirement is that the caller relinquish the processor, and having
 * relinquished it to a scheduler that immediately handed it back
 * satisfies that. POSIX defines no errors here at all, so there is no
 * errno value that could describe it either. Hence the unconditional
 * 0: the return value is checked for nothing because there is nothing
 * NtYieldExecution can report that POSIX would call a failure. */
#include <sched.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "libc.h"

#define SCHED_STATE_MAX 64
#define SCHED_PRIORITY_MAX 31

struct sched_state {
	pid_t pid;
	int policy;
	int priority;
};

static struct sched_state states[SCHED_STATE_MAX];
static int self_policy = SCHED_OTHER;
static int self_priority;

static int policy_valid(int policy)
{
	return policy == SCHED_OTHER || policy == SCHED_FIFO ||
	       policy == SCHED_RR || policy == SCHED_SPORADIC;
}

static int priority_min(int policy)
{
	return policy == SCHED_OTHER ? 0 : 1;
}

static int priority_valid(int policy, int priority)
{
	return priority >= priority_min(policy) && priority <= SCHED_PRIORITY_MAX;
}

static int process_exists(pid_t pid)
{
	struct __child *child;
	PROCESS_BASIC_INFORMATION pbi;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;
	HANDLE process;
	NTSTATUS status;
	int close_process = 0;

	if (pid == 0 || pid == getpid()) return 1;
	if (pid < 0) { errno = ESRCH; return 0; }
	child = __child_find((int)pid);
	if (child) process = child->h;
	else {
		InitializeObjectAttributes(&oa, 0, 0, 0, 0);
		cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
		cid.UniqueThread = 0;
		status = NtOpenProcess(&process, PROCESS_QUERY_LIMITED_INFORMATION,
		                       &oa, &cid);
		if (!NT_SUCCESS(status)) {
			errno = status == STATUS_ACCESS_DENIED ? EPERM : ESRCH;
			return 0;
		}
		close_process = 1;
	}
	status = NtQueryInformationProcess(process, ProcessBasicInformation,
	                                   &pbi, sizeof pbi, 0);
	if (close_process) NtClose(process);
	if (!NT_SUCCESS(status)) {
		errno = ESRCH;
		return 0;
	}
	/* NT may keep a reaped process object openable.  ExitStatus, rather
	 * than openability alone, distinguishes that object from a process
	 * POSIX still considers to exist. */
	if (pbi.ExitStatus != STATUS_PENDING) { errno = ESRCH; return 0; }
	return 1;
}

static struct sched_state *state_for(pid_t pid, int create)
{
	struct sched_state *empty = 0;
	int i;

	if (pid == 0 || pid == getpid()) return 0;
	for (i = 0; i < SCHED_STATE_MAX; i++) {
		if (states[i].pid == pid) return &states[i];
		if (!states[i].pid && !empty) empty = &states[i];
	}
	if (create && empty) {
		empty->pid = pid;
		empty->policy = SCHED_OTHER;
		empty->priority = 0;
		return empty;
	}
	return 0;
}

int sched_get_priority_max(int policy)
{
	if (!policy_valid(policy)) { errno = EINVAL; return -1; }
	return SCHED_PRIORITY_MAX;
}

int sched_get_priority_min(int policy)
{
	if (!policy_valid(policy)) { errno = EINVAL; return -1; }
	return priority_min(policy);
}

int sched_getscheduler(pid_t pid)
{
	struct sched_state *state;
	if (!process_exists(pid)) return -1;
	if (pid == 0 || pid == getpid()) return self_policy;
	state = state_for(pid, 0);
	return state ? state->policy : SCHED_OTHER;
}

int sched_getparam(pid_t pid, struct sched_param *param)
{
	struct sched_state *state;
	if (!param) { errno = EINVAL; return -1; }
	if (!process_exists(pid)) return -1;
	if (pid == 0 || pid == getpid()) param->sched_priority = self_priority;
	else {
		state = state_for(pid, 0);
		param->sched_priority = state ? state->priority : 0;
	}
	return 0;
}

static int set_state(pid_t pid, int policy, int priority)
{
	struct sched_state *state;
	if (!process_exists(pid)) return -1;
	if (pid == 0 || pid == getpid()) {
		self_policy = policy;
		self_priority = priority;
		return 0;
	}
	state = state_for(pid, 1);
	if (!state) { errno = EAGAIN; return -1; }
	state->policy = policy;
	state->priority = priority;
	return 0;
}

int sched_setparam(pid_t pid, const struct sched_param *param)
{
	int policy;
	if (!param) { errno = EINVAL; return -1; }
	policy = sched_getscheduler(pid);
	if (policy < 0) return -1;
	if (!priority_valid(policy, param->sched_priority)) {
		errno = EINVAL;
		return -1;
	}
	return set_state(pid, policy, param->sched_priority);
}

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
	if (!policy_valid(policy) || !param ||
	    !priority_valid(policy, param->sched_priority)) {
		errno = EINVAL;
		return -1;
	}
	return set_state(pid, policy, param->sched_priority);
}

int sched_rr_get_interval(pid_t pid, struct timespec *interval)
{
	if (!interval) { errno = EINVAL; return -1; }
	if (!process_exists(pid)) return -1;
	interval->tv_sec = 0;
	interval->tv_nsec = 10000000L; /* the 10 ms NT scheduler tick */
	return 0;
}

int sched_yield(void)
{
	NtYieldExecution();
	return 0;
}
