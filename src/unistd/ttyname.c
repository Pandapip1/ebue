/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <string.h>
#include <errno.h>

int ttyname_r(int fd, char *buf, size_t len)
{
	if (!isatty(fd)) return errno;
	if (len < 5) return ERANGE;
	memcpy(buf, "CON", 4);
	return 0;
}

char *ttyname(int fd)
{
	static char buf[8];
	if (ttyname_r(fd, buf, sizeof buf)) return 0;
	return buf;
}

char *ctermid(char *s)
{
	static char buf[8];
	if (!s) s = buf;
	strcpy(s, "CON");
	return s;
}

pid_t tcgetpgrp(int fd) { (void)fd; return 1; }
int tcsetpgrp(int fd, pid_t p) { (void)fd; (void)p; return 0; }
