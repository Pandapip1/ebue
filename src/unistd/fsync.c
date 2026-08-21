/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include "libc.h"

int fsync(int fd)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	NTSTATUS st;
	if (!f) return -1;
	if (f->type != __FD_FILE) return 0;
	st = NtFlushBuffersFile(f->h, &io);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int fdatasync(int fd) { return fsync(fd); }
void sync(void) {}
