/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>

wchar_t *wcsncpy(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	wchar_t *a = d;
	while (n && *s) n--, *d++ = *s++;
	wmemset(d, 0, n);
	return a;
}
