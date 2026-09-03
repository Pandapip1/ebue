/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unlike strspn.c/strcspn.c's 256-bit stack bitset, membership here is a
 * linear wcschr()-style scan: a wchar_t bitset would need 65536 bits
 * (8KiB) per call, a stack-overflow hazard in this libc's deeply-nested
 * call chains, for separator sets that are always short.
 */
#include <wchar.h>

/* Membership test.  Never called with c == 0: wcschr(set, 0) would
 * report a hit on the set's own terminator, which is why every loop
 * below guards on *s before asking. */
static int inset(const wchar_t *set, wchar_t c) __attribute__((pure));
static int inset(const wchar_t *set, wchar_t c)
{
	for (; *set; set++) if (*set == c) return 1;
	return 0;
}

size_t wcsspn(const wchar_t *s, const wchar_t *set)
{
	const wchar_t *a = s;
	while (*s && inset(set, *s)) s++;
	return (size_t)(s - a);
}

size_t wcscspn(const wchar_t *s, const wchar_t *set)
{
	const wchar_t *a = s;
	while (*s && !inset(set, *s)) s++;
	return (size_t)(s - a);
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *set)
{
	s += wcscspn(s, set);
	return *s ? (wchar_t *)s : 0;
}
