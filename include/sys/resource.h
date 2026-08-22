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

/* No trailing reserved padding: this struct's size (144 bytes on
 * x86_64) deliberately matches glibc's. On the native/ASan/fuzz build
 * (tools/asan-build.sh, fuzz/Makefile) this getrusage() is linked
 * statically alongside precompiled runtime code (libFuzzer, compiler-rt,
 * libstdc++) that was itself compiled against glibc's struct rusage and
 * stack-allocates it by that size; because ntlibc's getrusage() (hidden
 * visibility or not) still wins the intra-executable link for any call
 * to the symbol "getrusage", a larger ntlibc struct here means its
 * memset(ru, 0, sizeof *ru) (src/misc/resource.c) overflows that
 * caller's smaller stack slot -- which is exactly what a stray
 * `long __reserved[16]` here once did: it smashed the return address of
 * libFuzzer's GetPeakRSSMb() and crashed every harness at execution #2,
 * on any machine where the linker's -fvisibility=hidden objects still
 * take priority over -lc at static link time. Keep this layout equal to
 * glibc's; if a new field is ever needed here, add it to
 * src/misc/resource.c's fill logic without growing the struct beyond
 * what NT actually reports. */
struct rusage {
	struct timeval ru_utime;
	struct timeval ru_stime;
	long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt;
	long ru_nswap, ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv;
	long ru_nsignals, ru_nvcsw, ru_nivcsw;
};

int getrlimit (int, struct rlimit *);
int setrlimit (int, const struct rlimit *);  /* undefined-ok: getrlimit()
	(src/misc/resource.c) reports real numbers this library enforces
	(FD_MAX, CHILD_CAP_LIMIT_) or RLIM_INFINITY, but nothing here can make
	open()/fork() actually honor a *lower* value -- FD_MAX is a
	compile-time array bound, not a runtime ceiling. A setrlimit() that
	accepted a request without enforcing it would misrepresent itself the
	same way a lockf() built on this library's no-op advisory locks would
	(see lockf() below): it would look like real resource limiting while
	providing none */
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
