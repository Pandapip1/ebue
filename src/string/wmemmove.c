/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>
#include <string.h>

wchar_t *wmemmove(wchar_t *d withtok(writable_elements(n)),
	const wchar_t *s withtok(readable_elements(n)), size_t n)
{
	return memmove(d, s, n * sizeof(wchar_t));
}
