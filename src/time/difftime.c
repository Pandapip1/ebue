/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>

double difftime(time_t a, time_t b)
{
	return (double)a - (double)b;
}
