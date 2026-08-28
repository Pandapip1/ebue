/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
long read(int, void *, size_t);

void bounded_operations(int fd)
{
	char source[16], destination[16];
	memcpy(destination, source, sizeof source);
	memmove(source + 1, source, 8);
	memset(destination, 0, sizeof destination);
	read(fd, destination, sizeof destination);
}
