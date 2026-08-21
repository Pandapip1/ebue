/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

int isalnum(int c)
{
	return isalpha(c) || isdigit(c);
}
