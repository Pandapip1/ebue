/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <errno.h>
#include "libc.h"

int close(int fd)
{
	struct __fd *f = __fd_get(fd);
	NTSTATUS st;
	if (!f) return -1;
	st = NtClose(f->h);
	f->h = 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
