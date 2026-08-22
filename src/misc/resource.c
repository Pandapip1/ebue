/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getrlimit()/getrusage(): the read-only half of <sys/resource.h>.
 *
 * getrlimit() reports real numbers this library actually enforces --
 * FD_MAX for RLIMIT_NOFILE (src/internal/fd.c's fixed-size __fds[]) and
 * CHILD_CAP_LIMIT_ for RLIMIT_NPROC (src/process/children.c's child
 * table ceiling, also what sysconf(_SC_CHILD_MAX) reports) -- and
 * RLIM_INFINITY for everything else NT has no per-process cap for.
 * setrlimit() is deliberately not implemented (include/sys/resource.h,
 * undefined-ok): there is no way to make either of those numbers
 * actually change what open()/fork() will accept, and a setrlimit()
 * that accepted a lower value without enforcing it would misrepresent
 * itself the same way a lockf() built on this library's no-op file
 * locks would (see include/unistd.h's lockf() undefined-ok note).
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
 */
#include <sys/resource.h>
#include <errno.h>
#include <string.h>
#include "libc.h"

int getrlimit(int resource, struct rlimit *rl)
{
	if (!rl) { errno = EFAULT; return -1; }
	switch (resource) {
	case RLIMIT_NOFILE:
		rl->rlim_cur = rl->rlim_max = FD_MAX;
		break;
	case RLIMIT_NPROC:
		rl->rlim_cur = rl->rlim_max = CHILD_CAP_LIMIT_;
		break;
	case RLIMIT_CPU: case RLIMIT_FSIZE: case RLIMIT_DATA:
	case RLIMIT_STACK: case RLIMIT_CORE: case RLIMIT_RSS:
	case RLIMIT_MEMLOCK: case RLIMIT_AS:
		rl->rlim_cur = rl->rlim_max = RLIM_INFINITY;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	return 0;
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
