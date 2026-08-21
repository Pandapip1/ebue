/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>
#include "libc.h"

time_t time(time_t *tp)
{
	LARGE_INTEGER now;
	time_t t;

	NtQuerySystemTime(&now);
	t = (time_t)__nt_to_unix_sec(now);
	if (tp) *tp = t;
	return t;
}
