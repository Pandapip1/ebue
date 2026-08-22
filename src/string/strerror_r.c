/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <errno.h>
#include "libc.h"


int strerror_r(int err, char *buf, size_t buflen)
{
	const char *msg = __strerror_msg(err);
	size_t l = strlen(msg);
	if (l >= buflen) {
		if (buflen) {
			memcpy(buf, msg, buflen-1);
			buf[buflen-1] = 0;
		}
		return ERANGE;
	}
	memcpy(buf, msg, l+1);
	return 0;
}
