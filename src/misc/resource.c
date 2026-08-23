/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getrlimit()/setrlimit(): getrlimit() reports real numbers this library
 * actually enforces -- FD_MAX for RLIMIT_NOFILE (src/internal/fd.c's
 * fixed-size __fds[] table) and, for RLIMIT_NPROC/RLIMIT_CPU/RLIMIT_AS/
 * RLIMIT_DATA, whatever the last successful setrlimit() call recorded
 * (CHILD_CAP_LIMIT_/RLIM_INFINITY by default) -- and RLIM_INFINITY for
 * every other resource NT has no per-process cap for.
 *
 * setrlimit() is defined for exactly the resources include/sys/
 * resource.h's own comment documents as having a real NT enforcement
 * primitive: RLIMIT_NPROC, RLIMIT_CPU, RLIMIT_AS, and RLIMIT_DATA, via a
 * job object this process creates and assigns itself to on first use
 * (NtCreateJobObject/NtAssignProcessToJobObject, src/internal/nt.h) and
 * whose JobObjectExtendedLimitInformation this then updates
 * (NtSetInformationJobObject) -- ActiveProcessLimit for RLIMIT_NPROC,
 * PerProcessUserTimeLimit for RLIMIT_CPU, ProcessMemoryLimit for
 * RLIMIT_AS/RLIMIT_DATA (the same field for both, since NT does not
 * distinguish total address space from the data segment the way POSIX
 * does). The job-object call is best-effort: this process's own soft/
 * hard state is the source of truth getrlimit() reads back regardless of
 * whether the job object actually accepted the new limit, exactly the
 * way getrlimit() already reported FD_MAX/CHILD_CAP_LIMIT_ without ever
 * asking NT to confirm them.
 *
 * For every other resource (RLIMIT_NOFILE, RLIMIT_STACK, RLIMIT_FSIZE,
 * RLIMIT_CORE, RLIMIT_RSS, RLIMIT_MEMLOCK) there is no NT mechanism that
 * reaches the thing being capped after this process has already started
 * (FD_MAX is a compile-time array bound; NT fixes stack reservation at
 * NtCreateThreadEx() time; there is no per-process max-file-size,
 * core-dump-size, RSS, or mlock-budget primitive at all -- see
 * include/sys/resource.h for the fuller per-resource accounting).
 * setrlimit() for one of these accepts a request only when it does not
 * actually ask for stricter enforcement than the fixed value already in
 * effect (raising, or repeating, the existing ceiling is a harmless
 * no-op); asking to genuinely lower it is rejected with EINVAL rather
 * than silently accepted and then not honored, which is exactly the
 * misrepresentation the header's previous undefined-ok comment warned
 * against.
 *
 * getrusage() reports what NtQueryInformationProcess(ProcessTimes) can
 * answer -- ru_utime/ru_stime -- and leaves every other struct rusage
 * field zero, the same way many real getrusage() implementations do for
 * fields their platform has no counter for (Linux, for one, leaves most
 * of them zero too). RUSAGE_CHILDREN reads the running total
 * src/process/wait.c accumulates at every waitpid()/wait3()/wait4()
 * reap; RUSAGE_SELF and RUSAGE_THREAD both report this process's own
 * times, since this library has no per-thread accounting (the same
 * approximation src/time/clock_gettime.c's cputime_get() already makes
 * for CLOCK_THREAD_CPUTIME_ID).
 *
 * getpriority()/setpriority(): see include/sys/resource.h for the full
 * nice<->NT-base-priority mapping this uses and why it round-trips.
 */
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "libc.h"

/* ---- getrlimit()/setrlimit() -------------------------------------------
 * Soft/hard state for the four resources setrlimit() actually accepts a
 * new value for. Everything else getrlimit() reports is a fixed constant
 * (FD_MAX, or RLIM_INFINITY) computed directly, not stored here. */
static rlim_t nproc_cur = CHILD_CAP_LIMIT_, nproc_max = CHILD_CAP_LIMIT_;
static rlim_t cpu_cur = RLIM_INFINITY, cpu_max = RLIM_INFINITY;
static rlim_t as_cur = RLIM_INFINITY, as_max = RLIM_INFINITY;
static rlim_t data_cur = RLIM_INFINITY, data_max = RLIM_INFINITY;

int getrlimit(int resource, struct rlimit *rl)
{
	if (!rl) { errno = EFAULT; return -1; }
	switch (resource) {
	case RLIMIT_NOFILE:
		rl->rlim_cur = rl->rlim_max = FD_MAX;
		break;
	case RLIMIT_NPROC:
		rl->rlim_cur = nproc_cur; rl->rlim_max = nproc_max;
		break;
	case RLIMIT_CPU:
		rl->rlim_cur = cpu_cur; rl->rlim_max = cpu_max;
		break;
	case RLIMIT_AS:
		rl->rlim_cur = as_cur; rl->rlim_max = as_max;
		break;
	case RLIMIT_DATA:
		rl->rlim_cur = data_cur; rl->rlim_max = data_max;
		break;
	case RLIMIT_STACK: case RLIMIT_CORE: case RLIMIT_RSS:
	case RLIMIT_MEMLOCK: case RLIMIT_FSIZE:
		rl->rlim_cur = rl->rlim_max = RLIM_INFINITY;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	return 0;
}

/* Job object this process lazily creates and assigns itself to the first
 * time setrlimit() needs to reflect a limit onto NT. Best-effort: if job
 * objects are unavailable (or this NT-workalike's job-object support is a
 * stub, as Wine's NtQueryInformationJobObject is), the soft/hard state
 * above is still exactly what getrlimit() reports back, so the round
 * trip setrlimit() then getrlimit() promises stays intact either way. */
static HANDLE job_handle;

static HANDLE ensure_job(void)
{
	OBJECT_ATTRIBUTES oa;
	HANDLE h;

	if (job_handle) return job_handle;
	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	if (!NT_SUCCESS(NtCreateJobObject(&h, JOB_OBJECT_ALL_ACCESS, &oa)))
		return 0;
	if (!NT_SUCCESS(NtAssignProcessToJobObject(h, NtCurrentProcess()))) {
		NtClose(h);
		return 0;
	}
	job_handle = h;
	return job_handle;
}

/* Push the current soft limits for the four enforceable resources onto
 * the job object, best-effort (failure is not reported to the caller --
 * see the comment above ensure_job()). */
static void apply_job_limits(void)
{
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli;
	HANDLE h = ensure_job();

	if (!h) return;
	memset(&eli, 0, sizeof eli);
	if (nproc_cur != RLIM_INFINITY) {
		eli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
		eli.BasicLimitInformation.ActiveProcessLimit = nproc_cur > 0xFFFFFFFFu ? 0xFFFFFFFFu : (ULONG)nproc_cur;
	}
	if (cpu_cur != RLIM_INFINITY) {
		eli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
		eli.BasicLimitInformation.PerProcessUserTimeLimit = (LARGE_INTEGER)(cpu_cur * 10000000ULL);
	}
	if (as_cur != RLIM_INFINITY || data_cur != RLIM_INFINITY) {
		rlim_t lim = as_cur;
		if (data_cur != RLIM_INFINITY && (as_cur == RLIM_INFINITY || data_cur < as_cur))
			lim = data_cur;
		eli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
		eli.ProcessMemoryLimit = (SIZE_T)lim;
	}
	NtSetInformationJobObject(h, JobObjectExtendedLimitInformation, &eli, sizeof eli);
}

int setrlimit(int resource, const struct rlimit *rl)
{
	struct rlimit cur;

	if (!rl) { errno = EFAULT; return -1; }
	if (getrlimit(resource, &cur) != 0) return -1;  /* validates + EINVAL */

	/* "the new rlim_cur exceeds the new rlim_max" */
	if (rl->rlim_cur > rl->rlim_max) { errno = EINVAL; return -1; }

	switch (resource) {
	case RLIMIT_NPROC: case RLIMIT_CPU: case RLIMIT_AS: case RLIMIT_DATA:
		/* "Only a process with appropriate privileges can raise a
		 * hard limit" -- this library's one always-unprivileged
		 * user (src/unistd/ids.c) never has that. */
		if (rl->rlim_max > cur.rlim_max) { errno = EPERM; return -1; }
		switch (resource) {
		case RLIMIT_NPROC: nproc_cur = rl->rlim_cur; nproc_max = rl->rlim_max; break;
		case RLIMIT_CPU:   cpu_cur   = rl->rlim_cur; cpu_max   = rl->rlim_max; break;
		case RLIMIT_AS:    as_cur    = rl->rlim_cur; as_max    = rl->rlim_max; break;
		case RLIMIT_DATA:  data_cur  = rl->rlim_cur; data_max  = rl->rlim_max; break;
		}
		apply_job_limits();
		return 0;
	default:
		/* RLIMIT_NOFILE/STACK/FSIZE/CORE/RSS/MEMLOCK: no NT
		 * mechanism can actually move the fixed ceiling these
		 * already report (see the file banner comment). Accept the
		 * call only when it does not ask for anything stricter than
		 * what is already true -- a harmless no-op -- and refuse
		 * (EINVAL) a request that would require enforcement this
		 * library cannot provide, rather than silently lying about
		 * having applied it. */
		if (rl->rlim_cur < cur.rlim_cur || rl->rlim_max < cur.rlim_max) {
			errno = EINVAL;
			return -1;
		}
		return 0;
	}
}

int getrusage(int who, struct rusage *ru)
{
	KERNEL_USER_TIMES kt;
	NTSTATUS st;

	if (!ru) { errno = EFAULT; return -1; }
	switch (who) {
	case RUSAGE_CHILDREN:
		__rusage_children(ru);
		return 0;
	case RUSAGE_SELF:
	case RUSAGE_THREAD:
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	memset(ru, 0, sizeof *ru);
	st = NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &kt, sizeof kt, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	ru->ru_stime.tv_sec = (time_t)(kt.KernelTime / 10000000LL);
	ru->ru_stime.tv_usec = (suseconds_t)((kt.KernelTime % 10000000LL) / 10);
	ru->ru_utime.tv_sec = (time_t)(kt.UserTime / 10000000LL);
	ru->ru_utime.tv_usec = (suseconds_t)((kt.UserTime % 10000000LL) / 10);
	return 0;
}

/* ---- getpriority()/setpriority() ----------------------------------------
 * See include/sys/resource.h for the full writeup of why this maps nice
 * values onto ProcessPriorityClass (3 classes actually reachable from an
 * unprivileged caller) rather than the finer-grained ProcessBasePriority:
 * the latter is STATUS_NOT_IMPLEMENTED on the Wine build this project's
 * own CI runs against, confirmed directly --
 *
 *   NtSetInformationProcess(NtCurrentProcess(), ProcessBasePriority,
 *                            &bp, sizeof bp)
 *
 * returns STATUS_NOT_IMPLEMENTED (-> ENOSYS) on that Wine even though
 * NtQueryInformationProcess(ProcessBasicInformation) happily reports
 * BasePriority back -- support for *setting* it was only added to Wine
 * in commit b9dd7d114 ("ntdll: Implement ProcessBasePriority class in
 * NtSetInformationProcess."), first released in wine-10.7, well after
 * the wine-9.0 this project's own CI environment ships. */
static UCHAR priorityclass_from_nice(int nice)
{
	if (nice <= 0) return PROCESS_PRIOCLASS_NORMAL;
	if (nice < 10) return PROCESS_PRIOCLASS_BELOW_NORMAL;
	return PROCESS_PRIOCLASS_IDLE;
}

static int nice_from_baseprio(int bp)
{
	int nice = 8 - bp;
	if (nice < -NZERO) nice = -NZERO;
	if (nice > NZERO - 1) nice = NZERO - 1;
	return nice;
}

/* This process's own nice value: the authoritative source getpriority()
 * reads back for PRIO_PROCESS on self, so that set-then-get is always
 * exact for this process regardless of where the mapping above is lossy
 * (see include/sys/resource.h). Starts at the POSIX default, 0. */
static int self_nice;

int getpriority(int which, id_t who)
{
	int self;
	struct __child *c;
	HANDLE h;
	NTSTATUS st;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;
	PROCESS_BASIC_INFORMATION pbi;

	switch (which) {
	case PRIO_PROCESS: self = (who == 0 || who == (id_t)getpid()); break;
	case PRIO_PGRP:     self = (who == 0 || who == (id_t)getpgrp()); break;
	case PRIO_USER:     self = (who == 0 || who == (id_t)geteuid()); break;
	default: errno = EINVAL; return -1;
	}

	if (self) return self_nice;

	/* Not self: ntlibc tracks no group/user directory, so only a
	 * foreign PRIO_PROCESS pid can possibly be found. */
	if (which != PRIO_PROCESS) { errno = ESRCH; return -1; }

	c = __child_find((int)who);
	if (c) {
		h = c->h;
	} else {
		InitializeObjectAttributes(&oa, 0, 0, 0, 0);
		cid.UniqueProcess = (HANDLE)(ULONG_PTR)who;
		cid.UniqueThread = 0;
		st = NtOpenProcess(&h, PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid);
		if (!NT_SUCCESS(st)) { errno = ESRCH; return -1; }
	}
	st = NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof pbi, 0);
	if (!c) NtClose(h);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return nice_from_baseprio((int)pbi.BasePriority);
}

int setpriority(int which, id_t who, int value)
{
	int self;
	struct __child *c;
	HANDLE h;
	NTSTATUS st;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;
	PROCESS_PRIORITY_CLASS pc;

	switch (which) {
	case PRIO_PROCESS: self = (who == 0 || who == (id_t)getpid()); break;
	case PRIO_PGRP:     self = (who == 0 || who == (id_t)getpgrp()); break;
	case PRIO_USER:     self = (who == 0 || who == (id_t)geteuid()); break;
	default: errno = EINVAL; return -1;
	}

	if (value < -NZERO) value = -NZERO;
	if (value > NZERO - 1) value = NZERO - 1;

	if (self) {
		/* "Only a process with appropriate privileges can lower its
		 * nice value" -- this library's one user is always
		 * unprivileged, so any value below the POSIX default (0) is
		 * always refused; anywhere in [0, NZERO-1], including back
		 * down to a value this process held a moment ago, is always
		 * allowed. */
		if (value < 0) { errno = EACCES; return -1; }
		pc.Foreground = 0;
		pc.PriorityClass = priorityclass_from_nice(value);
		st = NtSetInformationProcess(NtCurrentProcess(), ProcessPriorityClass, &pc, sizeof pc);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		self_nice = value;
		return 0;
	}

	if (which != PRIO_PROCESS) { errno = ESRCH; return -1; }

	c = __child_find((int)who);
	if (c) {
		pc.Foreground = 0;
		pc.PriorityClass = priorityclass_from_nice(value);
		st = NtSetInformationProcess(c->h, ProcessPriorityClass, &pc, sizeof pc);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		return 0;
	}

	/* A process exists but this library did not spawn it: "the real
	 * [or] effective user ID of the executing process [does not]
	 * match the effective user ID of the process whose nice value is
	 * being changed" is the only way that can be true here, since
	 * ntlibc's one-user model has nothing else to check. */
	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	cid.UniqueProcess = (HANDLE)(ULONG_PTR)who;
	cid.UniqueThread = 0;
	st = NtOpenProcess(&h, PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid);
	if (!NT_SUCCESS(st)) { errno = ESRCH; return -1; }
	NtClose(h);
	errno = EPERM;
	return -1;
}
