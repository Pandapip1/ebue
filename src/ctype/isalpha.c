/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

int isalpha(int c)
{
	return ((unsigned)c|32)-'a' < 26;
}
