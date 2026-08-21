/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <strings.h>

int ffs(int i)
{
	unsigned u = i;
	int n;
	if (!u) return 0;
	for (n = 1; !(u & 1); n++) u >>= 1;
	return n;
}
