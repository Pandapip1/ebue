/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>
#include "libc.h"
#include "plat_time.h"

int timespec_get(struct timespec *ts, int base)
{
	long long now;

	if (base != TIME_UTC) return 0;
	__plat_realtime_get(&now);
	ts->tv_sec = (time_t)__nt_to_unix_sec(now);
	ts->tv_nsec = __nt_to_unix_nsec(now);
	return TIME_UTC;
}
