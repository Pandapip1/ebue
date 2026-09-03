/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * isatty() against an NT console: __FD_CONSOLE is a real, structural fd
 * classification on this platform (src/internal/nt/plat_fd_init.c
 * assigns it from a real NtQueryVolumeInformationFile/
 * NtQueryInformationFile pair at fd-creation time -- see that file's own
 * banner), so testing it here is a real answer, not a guess. NT-only:
 * this whole file's body is wrapped in `#ifndef __linux__` below, the
 * same split src/termios/termios.c's own banner already uses and
 * explains -- see src/unistd/linux/plat_isatty.c for why the identical
 * static-classification check is NOT a real answer on Linux, and what
 * this library does there instead. */
#ifndef __linux__
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
#endif
