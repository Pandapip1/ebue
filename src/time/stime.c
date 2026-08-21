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

int stime(const time_t *tp)
{
	LARGE_INTEGER nt = __unix_to_nt(*tp, 0);
	NTSTATUS st = NtSetSystemTime(&nt, NULL);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
