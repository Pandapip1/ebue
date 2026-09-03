/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wchar.h>

int wcswidth(const wchar_t *pwcs, size_t n)
{
	int total = 0, w;
	size_t i;

	for (i = 0; i < n && pwcs[i]; i++) {
		w = wcwidth(pwcs[i]);
		if (w < 0) return -1;
		total += w;
	}
	return total;
}
