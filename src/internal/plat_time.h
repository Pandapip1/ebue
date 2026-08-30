/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform-time interface src/time/{time,clock,stime,timespec_get,
 * clock_gettime,timer}.c's POSIX-facing front doors call into instead of
 * a raw NtQuerySystemTime/NtSetSystemTime/NtQueryPerformanceCounter/
 * NtQueryInformationProcess(ProcessTimes) call, or the NT thread/event
 * calls timer.c's manager thread is built on.  See src/time/nt/plat_time.c
 * for the implementation these declare.
 *
 * Every function here takes/returns plain integers -- ticks in the
 * platform's own units (NT's 100ns system-time ticks since 1601, or its
 * free-running performance-counter units), never a raw NTSTATUS/
 * LARGE_INTEGER for the front door to interpret.  The epoch conversion
 * (NT ticks <-> unix-epoch seconds/nanoseconds) and the QPC-to-timespec
 * scaling are portable arithmetic, not NT-specific, so they stay exactly
 * where they already were -- src/internal/libc.h's __nt_to_unix_sec()/
 * __unix_to_nt()/__clock_qpc_to_timespec()/__clock_combine_cpu_ticks()
 * and friends -- and are NOT part of this interface.
 */
#ifndef _NTLIBC_PLAT_TIME_H
#define _NTLIBC_PLAT_TIME_H

#include "plat_handle.h"

/* NtQuerySystemTime(): the current value of the system's realtime clock,
 * in 100ns ticks since 1601-01-01 (NT's own epoch/unit).  This has no
 * documented failure mode on any NT/Wine implementation this library
 * targets, so -- like the code this replaces -- the result is never
 * checked for failure. */
void __plat_realtime_get(long long *nt_ticks);

/* NtSetSystemTime(): set the system's realtime clock to `nt_ticks`.
 * 0/-1(errno) -- fails [EPERM] without SeSystemtimePrivilege, which a
 * normal process token does not hold. */
int __plat_realtime_set(long long nt_ticks);

/* NtQueryPerformanceCounter(): a free-running counter and its frequency
 * (both in the platform's own arbitrary units) -- the basis for
 * CLOCK_MONOTONIC and friends.  0/-1(errno). */
int __plat_perfcounter_get(long long *count, long long *freq);

/* NtQueryInformationProcess(ProcessTimes): the current process's
 * combined kernel+user CPU time, each in 100ns ticks.  0/-1(errno). */
int __plat_process_cpu_ticks(long long *kernel, long long *user);

/* Start timer.c's per-process manager thread, which runs `loop` (a
 * function that never returns) on its own native thread, and create the
 * auto-reset wake event it waits on.  Called at most once per process --
 * idempotency against a second call is the front door's job
 * (timer_create()'s `manager_started` latch), not this one's.
 * 0 with *wake_out set / -1(errno==EAGAIN) on failure -- including,
 * deliberately, under the native (non-NT) sanitizer/fuzz build, which has
 * no NT thread or signal-delivery transport to create either of these
 * against; SIGEV_NONE timers there need neither (see timer.c). */
int __plat_timer_manager_start(void (*loop)(void), __plat_handle_t *wake_out);

/* NtSetEvent(): wake the manager thread immediately rather than letting
 * it sleep until its current wait's deadline -- called by timer_settime()/
 * timer_delete() whenever a timer operation may have invalidated the
 * deadline the manager is currently waiting on. */
void __plat_timer_wake(__plat_handle_t wake);

/* NtWaitForSingleObject(): wait on the manager's (auto-reset) wake event
 * until it is signalled or, when `has_deadline` is nonzero, until `ticks`
 * (100ns units, relative, already clamped to >=0 by the caller) elapses,
 * whichever comes first.  `has_deadline` zero waits indefinitely -- no
 * timer in the process currently has a due time. */
void __plat_timer_manager_wait(__plat_handle_t wake, long long ticks, int has_deadline);

#endif
