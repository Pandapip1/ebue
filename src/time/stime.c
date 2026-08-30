/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Setting the system clock needs SeSystemtimePrivilege, which a normal
 * process token doesn't hold; NtSetSystemTime fails with
 * STATUS_PRIVILEGE_NOT_HELD in that case, which __set_errno_status maps
 * to EPERM -- the same failure mode POSIX documents for stime().
 */
#include <time.h>
#include "libc.h"
#include "plat_time.h"

int stime(const time_t *tp)
{
	long long nt;
	if (!__unix_to_nt(*tp, 0, &nt)) { errno = EOVERFLOW; return -1; }
	return __plat_realtime_set(nt);
}
