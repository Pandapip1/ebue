/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

int islower(int c)
{
	return (unsigned)c-'a' < 26;
}
