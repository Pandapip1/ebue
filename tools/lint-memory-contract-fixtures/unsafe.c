/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);

void oversized(void)
{
	char source[4], destination[4];
	memcpy(destination, source, 8); /* memory-contract-expect */
}

void opaque(void *buffer, size_t length)
{
	memset(buffer, 0, length); /* memory-contract-expect */
}

void overlapping(void)
{
	char buffer[16];
	memcpy(buffer + 1, buffer, 8); /* memory-contract-expect */
}
