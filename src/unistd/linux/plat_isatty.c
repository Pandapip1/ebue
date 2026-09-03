/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * classify_fd() (plat_fd_init.c) never assigns __FD_CONSOLE on Linux, so
 * the NT approach doesn't work here. This does a live tcgetattr()/
 * ioctl(TCGETS) probe instead, the same way glibc/musl actually
 * implement isatty(). */
#include <unistd.h>
#include <termios.h>

int isatty(int fd)
{
	struct termios t;
	return tcgetattr(fd, &t) == 0;
}
