/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/time.h>

#define __NEED_id_t
#include <bits/alltypes.h>

typedef unsigned long long rlim_t;

struct rlimit {
	rlim_t rlim_cur;
	rlim_t rlim_max;
};

/* No trailing reserved padding: this reports what NT can actually tell
 * us (src/misc/resource.c) and nothing more.  A `long __reserved[16]`
 * tail lived here for a while; nothing ever read it, and it made the
 * struct far larger than any caller had reason to expect.  See the
 * symbol-preemption note in tools/asan-build.sh for why an oversized
 * struct here is actively dangerous in the native test build. */
struct rusage {
	struct timeval ru_utime;
	struct timeval ru_stime;
	long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt;
	long ru_nswap, ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv;
	long ru_nsignals, ru_nvcsw, ru_nivcsw;
};

int getrlimit (int, struct rlimit *);

/* setrlimit(): src/misc/resource.c defines it for the resources NT has a
 * real enforcement primitive for -- RLIMIT_NPROC, RLIMIT_CPU, RLIMIT_AS,
 * RLIMIT_DATA, via a job object this process creates and assigns itself to
 * (NtCreateJobObject/NtAssignProcessToJobObject/NtSetInformationJobObject,
 * src/internal/nt.h) -- and tracks the soft/hard pair itself for every
 * other RLIMIT_* (including the ones below that NT genuinely cannot
 * enforce past process start) so getrlimit() always reports back exactly
 * what the last successful setrlimit() call recorded. RLIMIT_NOFILE,
 * RLIMIT_STACK, RLIMIT_FSIZE, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_MEMLOCK
 * still have no NT mechanism that can shrink what they actually cap after
 * this process has started (FD_MAX is a compile-time array bound, not a
 * runtime ceiling; NT fixes stack reservation at NtCreateThreadEx() time;
 * there is no per-process max-file-size, core-dump-size, RSS, or
 * mlock-budget primitive at all) -- setrlimit() accordingly only accepts
 * a request for one of these that does not ask for stricter enforcement
 * than the fixed value it already reports (asking to raise, or to repeat,
 * the existing ceiling is a harmless no-op; asking to lower it would be
 * exactly the misrepresentation this comment used to warn about, so that
 * is rejected with EINVAL instead of silently accepted). */
int setrlimit (int, const struct rlimit *);
int getrusage (int, struct rusage *);

/* getpriority()/setpriority(): POSIX.1-2017 base functions (moved from XSI
 * to BASE in Issue 5, getpriority.html "Standards Status"). NZERO is the
 * default nice value; the valid *returned* nice range is
 * [-NZERO, NZERO-1].
 *
 * Mapping onto NT: this process's own nice value is tracked directly (the
 * authoritative source getpriority() reads back for PRIO_PROCESS on
 * itself), and mirrored onto NtSetInformationProcess(ProcessPriorityClass)
 * as best-effort real NT-visible effect. NtSetInformationProcess's other
 * plausible route, the finer-grained ProcessBasePriority class, turns out
 * not to be implemented by every NT workalike this library's test suite
 * runs against (confirmed: STATUS_NOT_IMPLEMENTED from the Wine build
 * this project's own CI uses, even though a newer Wine tree does
 * implement it -- src/misc/resource.c has the detail); ProcessPriorityClass
 * is the coarser but far more portable mechanism (it is what kernel32's
 * SetPriorityClass() has always been built on), so that is what is used:
 *     nice == 0         -> PROCESS_PRIOCLASS_NORMAL
 *     0  < nice < 10     -> PROCESS_PRIOCLASS_BELOW_NORMAL
 *     10 <= nice <= 19   -> PROCESS_PRIOCLASS_IDLE
 * (each successive class is less favorable to the process, matching the
 * POSIX direction of higher nice meaning friendlier to other processes).
 * Since an unprivileged caller may never set a negative nice value at all
 * (see EACCES below), the classes above NORMAL are never reached from
 * this process's own setpriority() calls, and are listed here only for
 * completeness (src/internal/nt.h). This maps 20 nice values onto 3
 * classes -- badly lossy -- but every setpriority() this process issues
 * against itself is remembered verbatim in a small piece of process-local
 * state, so getpriority() on one's own process always reads back exactly
 * what was last set, regardless of how coarse the NT-visible side effect
 * is. A *foreign* process's nice value (queried, never cached) is derived
 * from PROCESS_BASIC_INFORMATION.BasePriority via
 * nice = clamp(8 - bp, -20, 19), best-effort in two ways: ntlibc has no
 * cache for a priority it did not itself set, and that BasePriority field
 * is not reliably populated for a 32-bit process running under WOW64 on
 * at least the Wine build this project tests against (its own test
 * suite, dlls/ntdll/tests/info.c, marks exactly that field `todo_wine`
 * under is_wow64 after a priority change) -- both honestly approximate,
 * not exact, for a process this one did not set the priority of itself.
 *
 * PRIO_PGRP/PRIO_USER: ntlibc models exactly one process group (this
 * process is always its own and only member -- getpgrp() is a hardcoded
 * 1) and exactly one user (geteuid() is a hardcoded 1000), so who==0,
 * who==getpgrp(), or who==geteuid() all honestly denote "this process,
 * the sole member of its own group and the sole process running as this
 * uid" and behave exactly like PRIO_PROCESS on self. Any other who value
 * cannot name a group or user this library tracks, so it is ESRCH -- no
 * group/user directory exists to search. */
#define NZERO 20
#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2
int getpriority (int, id_t);
int setpriority (int, id_t, int);

#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD 1

#define RLIM_INFINITY (~0ULL)
#define RLIM_SAVED_CUR RLIM_INFINITY
#define RLIM_SAVED_MAX RLIM_INFINITY

#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_RSS     5
#define RLIMIT_NPROC   6
#define RLIMIT_NOFILE  7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS      9
#define RLIMIT_NLIMITS 16

#ifdef __cplusplus
}
#endif
#endif
