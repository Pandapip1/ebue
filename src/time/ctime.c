/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>

char *ctime_r(const time_t *tp, char *buf)
{
	struct tm tm;
	if (!localtime_r(tp, &tm)) return NULL;
	return asctime_r(&tm, buf); // NOLINT(bugprone-unsafe-functions,cert-msc24-c,cert-msc33-c) -- ctime_r is required to produce asctime's fixed format in its caller-provided result buffer
}

char *ctime(const time_t *tp)
{
	static char buf[32];
	return ctime_r(tp, buf);
}
