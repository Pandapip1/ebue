/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>
#include <string.h>
#include <stdint.h>

wchar_t *wmemmove(wchar_t *d withtok(writable_elements(n)),
	const wchar_t *s withtok(readable_elements(n)), size_t n)
{
	size_t i;
	if ((uintptr_t)d < (uintptr_t)s) {
		for (i = 0; i < n; i++) d[i] = s[i];
	} else if (d != s) {
		for (i = n; i > 0; i--) d[i - 1] = s[i - 1];
	}
	return d;
}
