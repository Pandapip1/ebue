/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
	const wchar_t *p = s + wcslen(s);
	while (p >= s && *p != c) p--;
	return p >= s ? (wchar_t *)p : 0;
}
