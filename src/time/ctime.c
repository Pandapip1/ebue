/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>

char *ctime_r(const time_t *tp, char *buf)
{
	struct tm tm;
	if (!localtime_r(tp, &tm)) return NULL;
	return asctime_r(&tm, buf);
}

char *ctime(const time_t *tp)
{
	static char buf[32];
	return ctime_r(tp, buf);
}
