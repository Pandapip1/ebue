/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>

int wcscmp(const wchar_t *l, const wchar_t *r)
{
	for (; *l == *r && *l; l++, r++);
	return (int)*l - (int)*r;
}
