/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcsnlen(): the wchar_t mirror of strnlen() (src/string/strnlen.c),
 * with strnlen.html's contract read for wide characters -- "the number
 * of wide characters preceding the first null wide character, if ws
 * contains a null wide character within the first maxlen wide
 * characters; otherwise maxlen".
 *
 * Implemented over wmemchr() the way strnlen.c is over memchr(), so
 * the bound is counted in wchar_t units at every step and never in
 * bytes.  Like strnlen(), it must not read past the bound: a caller
 * may legitimately pass a maxlen that runs off the end of a
 * non-terminated array.
 */
#include <wchar.h>

size_t wcsnlen(const wchar_t *s, size_t n)
{
	const wchar_t *p = wmemchr(s, 0, n);
	return p ? (size_t)(p - s) : n;
}
