/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
size_t strlen(const char *);
int strcmp(const char *, const char *);

size_t opaque(const char *text)
{
	return strlen(text); /* memory-contract-expect */
}

int unterminated(void)
{
	char bytes[3] = {'a', 'b', 'c'};
	return strcmp(bytes, "abc"); /* memory-contract-expect */
}
