/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>

int wmemcmp(const wchar_t *l, const wchar_t *r, size_t n)
{
	for (; n && *l == *r; n--, l++, r++);
	return n ? (int)*l - (int)*r : 0;
}
