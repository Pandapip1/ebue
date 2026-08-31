/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_misc.h -- see that header for
 * the contract each function makes.  Everything here was, until this
 * file existed, inline inside src/misc/sched.c, times.c and
 * resource.c; nothing changed in substance, only location and the
 * addition of a POSIX-shaped return (errno already set) in place of a
 * raw NTSTATUS or a status-shaped decision the front door had to make
 * for itself.
 */
#include <errno.h>
#include <string.h>
#include <sys/resource.h>
#include "libc.h"
#include "plat_misc.h"

void __plat_yield(void)
{
	NtYieldExecution();
}

/* out required: written unconditionally (`*out = h;`) on the success
 * path with no NULL check; both real callers below forward their own
 * now-required out with no guard of their own. */
static int open_process(pid_t pid, ACCESS_MASK want, __plat_handle_t *out)
    __attribute__((nonnull(3)));
static int open_process(pid_t pid, ACCESS_MASK want, __plat_handle_t *out)
{
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;
	HANDLE h;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
	cid.UniqueThread = 0;
	st = NtOpenProcess(&h, want, &oa, &cid);
	if (!NT_SUCCESS(st)) return st == (NTSTATUS)STATUS_ACCESS_DENIED ? -2 : -1;
	*out = h;
	return 0;
}

int __plat_process_open_checked(pid_t pid, __plat_handle_t *out)
{
	int r = open_process(pid, PROCESS_QUERY_LIMITED_INFORMATION, out);
	if (r == 0) return 0;
	errno = r == -2 ? EPERM : ESRCH;
	return -1;
}

int __plat_process_open(pid_t pid, __plat_handle_t *out)
{
	if (open_process(pid, PROCESS_QUERY_LIMITED_INFORMATION, out) < 0) {
		errno = ESRCH;
		return -1;
	}
	return 0;
}

int __plat_process_alive(__plat_handle_t h)
{
	PROCESS_BASIC_INFORMATION pbi;
	NTSTATUS st = NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof pbi, 0);
	if (!NT_SUCCESS(st)) { errno = ESRCH; return 0; }
	/* NT may keep a reaped process object openable.  ExitStatus, rather
	 * than openability alone, distinguishes that object from a process
	 * POSIX still considers to exist. */
	if (pbi.ExitStatus != (NTSTATUS)STATUS_PENDING) { errno = ESRCH; return 0; }
	return 1;
}

int __plat_process_times_self(unsigned long long *user100ns, unsigned long long *kernel100ns)
{
	KERNEL_USER_TIMES kt;
	NTSTATUS st = NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &kt, sizeof kt, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*user100ns = (unsigned long long)kt.UserTime;
	*kernel100ns = (unsigned long long)kt.KernelTime;
	return 0;
}

/* This process's nice<->NT-base-priority mapping.  See include/sys/
 * resource.h for the full writeup: only three priority classes are
 * actually reachable from an unprivileged caller, and the finer-grained
 * ProcessBasePriority class is STATUS_NOT_IMPLEMENTED on the Wine this
 * project's own CI runs against. */
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

int __plat_priority_get(__plat_handle_t h, int *nice_out)
{
	PROCESS_BASIC_INFORMATION pbi;
	NTSTATUS st = NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof pbi, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*nice_out = nice_from_baseprio((int)pbi.BasePriority);
	return 0;
}

int __plat_priority_set(__plat_handle_t h, int foreground, int nice_value)
{
	PROCESS_PRIORITY_CLASS pc;
	NTSTATUS st;

	pc.Foreground = (BOOLEAN)foreground;
	pc.PriorityClass = priorityclass_from_nice(nice_value);
	st = NtSetInformationProcess(h, ProcessPriorityClass, &pc, sizeof pc);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_priority_set_self(int foreground, int nice_value)
{
	return __plat_priority_set(NtCurrentProcess(), foreground, nice_value);
}

int __plat_write_start_offset(__plat_handle_t h, int append, long long *out)
{
	IO_STATUS_BLOCK io;

	if (append) {
		FILE_STANDARD_INFORMATION si;
		if (!NT_SUCCESS(NtQueryInformationFile(h, &io, &si, sizeof si, FileStandardInformation)))
			return -1;
		*out = si.EndOfFile;
	} else {
		FILE_POSITION_INFORMATION pi;
		if (!NT_SUCCESS(NtQueryInformationFile(h, &io, &pi, sizeof pi, FilePositionInformation)))
			return -1;
		*out = pi.CurrentByteOffset;
	}
	return 0;
}

/* Job object this process lazily creates and assigns itself to the
 * first time setrlimit() needs to reflect a limit onto NT.  See
 * resource.c's own comment on why every failure past this point is
 * absorbed rather than reported. */
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

void __plat_job_apply_limits(rlim_t nproc_cur, rlim_t cpu_cur, rlim_t as_cur, rlim_t data_cur)
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
