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
