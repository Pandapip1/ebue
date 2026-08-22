/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

__wraps int isupper(int c)
{
	return (unsigned)c-'A' < 26;
}
