/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
int open(const char *, int, ...);
int close(int);
long write(int, const void *, size_t);

void opaque(int fd)
{
	write(fd, "x", 1); /* ownership-expect: resource-unproved */
}

void release_twice(void)
{
	int fd = open("name", 0);
	if (fd < 0)
		return;
	close(fd);
	close(fd); /* ownership-expect: resource-released */
}

void use_after_release(void)
{
	int fd = open("name", 0);
	if (fd < 0)
		return;
	close(fd);
	write(fd, "x", 1); /* ownership-expect: resource-use-released */
}
