/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface src/misc/sched.c, times.c and resource.c's
 * POSIX-facing front doors call into instead of raw
 * Nt{OpenProcess,QueryInformationProcess,SetInformationProcess,Close,
 * YieldExecution,CreateJobObject,AssignProcessToJobObject,
 * SetInformationJobObject} calls.  See src/misc/nt/plat_misc.c for the
 * implementation these declare.
 *
 * Every function here takes POSIX-shaped arguments and returns a
 * POSIX-shaped result -- errno already set on failure, never a raw NT
 * status/struct for the front door to interpret.  Two functions make a
 * DIFFERENT errno decision than __errno_from_status()'s generic table
 * would for the exact same underlying failure, because two different
 * front-door call sites need two different answers to "a foreign
 * process could not be opened": sched.c's process_exists() (kill()-
 * adjacent: [EPERM] for a real access refusal, [ESRCH] for everything
 * else) versus resource.c's getpriority()/setpriority() (uniformly
 * [ESRCH], no distinction at all).  That decision is made here, with
 * the real NTSTATUS in hand, not reconstructed from errno afterward --
 * see __plat_process_open_checked() vs __plat_process_open() below.
 */
#ifndef _NTLIBC_PLAT_MISC_H
#define _NTLIBC_PLAT_MISC_H

#include <sys/types.h>
#include <sys/resource.h>
#include "plat_handle.h"

/* sched_yield(): relinquish the processor.  No return value -- see
 * sched.c's own banner on why NtYieldExecution's "did I actually switch
 * away" answer is not a POSIX failure and sched_yield() checks nothing. */
void __plat_yield(void);

/* Whether the process referred to by `h` (a handle this library already
 * owns -- a known child, or one just opened via __plat_process_open_
 * checked() below) is still alive: sched.c's process_exists() reads
 * "alive" as "opens and queries fine, and its ProcessBasicInformation.
 * ExitStatus is still STATUS_PENDING". 1 if alive; 0 with errno set to
 * [ESRCH] otherwise -- every failure reason (the query itself failing,
 * or a real exit status) reports the same [ESRCH] uniformly here, so
 * there is no per-status decision being hidden by the uniform answer. */
int __plat_process_alive(__plat_handle_t h);

/* Open pid's process object for a query-only check (PROCESS_QUERY_
 * LIMITED_INFORMATION), the way sched.c's process_exists() needs:
 * [EPERM] iff the platform's own access check specifically refused
 * (STATUS_ACCESS_DENIED), [ESRCH] for every other failure (no such
 * pid). See this header's own banner for why that distinction cannot
 * wait until only errno is left to look at. */
int __plat_process_open_checked(pid_t pid, __plat_handle_t *out);

/* Open pid's process object the same way, for resource.c's
 * getpriority()/setpriority(), which report [ESRCH] for every failure
 * uniformly -- no distinction to make, no status to keep in hand. */
int __plat_process_open(pid_t pid, __plat_handle_t *out);

/* This process's own user/kernel CPU time, in 100ns units (NT's own
 * granularity for these fields, ProcessTimes' UserTime/KernelTime) --
 * times.c and resource.c's getrusage(RUSAGE_SELF|RUSAGE_THREAD) both
 * want exactly this and convert it their own way afterward, which is
 * why the conversion itself stays in the front door: it is this
 * library's own choice of tick size, nothing NT-specific. 0/-1(errno)
 * via return. */
int __plat_process_times_self(unsigned long long *user100ns, unsigned long long *kernel100ns);

/* `h`'s base priority, converted to a nice value by this library's own
 * nice<->NT-base-priority mapping (include/sys/resource.h has the full
 * writeup and the round-trip argument) -- getpriority()'s half.
 * 0/-1(errno) via return, *nice_out set on success. */
int __plat_priority_get(__plat_handle_t h, int *nice_out);

/* Set `h`'s priority class, derived from `nice_value` by the same
 * mapping's other half (see include/sys/resource.h for why the finer-
 * grained ProcessBasePriority class is not used instead: STATUS_NOT_
 * IMPLEMENTED on the Wine this project's own CI runs against) --
 * setpriority()'s half.  `foreground` mirrors PROCESS_PRIORITY_CLASS's
 * own field; resource.c always passes 0 (see its own comment on why
 * NT's foreground-boost bit is never set by this library). 0/-1(errno)
 * via return. */
int __plat_priority_set(__plat_handle_t h, int foreground, int nice_value);

/* Set THIS process's own priority class the same way -- setpriority()'s
 * self case, which (unlike every other caller of __plat_priority_set())
 * has no handle of its own to hand in. */
int __plat_priority_set_self(int foreground, int nice_value);

/* The offset a write on `h` will start at: end-of-file when `append`,
 * else the descriptor's current position -- write.html's "starting
 * position" for resource.c's RLIMIT_FSIZE clamp.  Deliberately separate
 * from plat_fd.h's __plat_seek_query(), which this would otherwise
 * duplicate exactly: THAT call sets errno on failure (lseek()'s own
 * contract), and this one must NOT -- it is consulted on write()'s hot
 * path purely to decide whether a limit applies, a query that cannot be
 * answered is not itself an error any caller of this ever reports, and
 * __fsize_clamp()/fsize_start() (resource.c) just treat "cannot answer"
 * as "no limit applies" and carry on. Setting errno here would leave a
 * stray value behind a write() call that itself goes on to succeed.
 * 0/-1 via return (errno untouched either way); *out set only on 0. */
int __plat_write_start_offset(__plat_handle_t h, int append, long long *out);

/* Best-effort push of the current NPROC/CPU/AS/DATA soft limits onto a
 * lazily-created job object this process assigns itself to the first
 * time it is needed.  See resource.c's own banner for why this is
 * entirely best-effort: failure here is never reported to the caller,
 * and getrlimit() never asks NT to confirm what was actually accepted.
 * RLIM_INFINITY in any argument means "no limit for that resource",
 * exactly like every other setrlimit() caller already sees it. */
void __plat_job_apply_limits(rlim_t nproc_cur, rlim_t cpu_cur, rlim_t as_cur, rlim_t data_cur);

#endif
