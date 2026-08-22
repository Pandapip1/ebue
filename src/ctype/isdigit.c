/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

__wraps int isdigit(int c)
{
	return (unsigned)c-'0' < 10;
}
