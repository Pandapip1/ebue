/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>
#include <string.h>

wchar_t *wmemmove(wchar_t *d, const wchar_t *s, size_t n)
{
	return memmove(d, s, n * sizeof(wchar_t));
}
