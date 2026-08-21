/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <errno.h>
#include "libc.h"

int isatty(int fd)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return 0;
	if (f->type != __FD_CONSOLE) { errno = ENOTTY; return 0; }
	return 1;
}
