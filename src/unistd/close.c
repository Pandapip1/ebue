/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <errno.h>
#include "libc.h"
#include "plat_fd.h"

int close(int fd)
{
	struct __fd *f = __fd_get(fd);
	int r;
	if (!f) return -1;
	__mq_fd_closed(fd);
	r = __plat_close(f->h);
	f->h = __PLAT_HANDLE_NULL;
	return r;
}
