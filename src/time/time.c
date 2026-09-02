/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>
#include "libc.h"
#include "plat_time.h"

time_t time(time_t *tp)
{
	long long now;
	time_t t;

	__plat_realtime_get(&now);
	t = (time_t)__ticks_to_unix_sec(now);
	if (tp) *tp = t;
	return t;
}
