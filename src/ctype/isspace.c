/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

int isspace(int c)
{
	return c == ' ' || (unsigned)c-'\t' < 5;
}
