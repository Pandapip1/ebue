/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "libc.h"
size_t wcslen_(const WCHAR *s)
{
	size_t n = 0;
	while (s[n]) n++;
	return n;
}
