/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * clock() wants CPU time (kernel + user) in CLOCKS_PER_SEC units;
 * KERNEL_USER_TIMES gives it in 100ns units, so scale by
 * CLOCKS_PER_SEC/__TICKS_PER_SEC (1e6/1e7 = 1/10).
 */
#include <time.h>
#include "libc.h"
#include "plat_time.h"

clock_t clock(void)
{
	long long kernel, user, ticks;
	if (__plat_process_cpu_ticks(&kernel, &user) < 0) return (clock_t)-1;
	if (!__clock_combine_cpu_ticks(kernel, user, &ticks))
		return (clock_t)-1;
	return (clock_t)(ticks / (__TICKS_PER_SEC / CLOCKS_PER_SEC));
}
