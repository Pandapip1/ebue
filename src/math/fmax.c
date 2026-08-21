/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>

double fmax(double x, double y)
{
	if (x != x) return y;
	if (y != y) return x;
	/* +0 beats -0 */
	if (x == 0 && y == 0) return __signbit(x) ? y : x;
	return x < y ? y : x;
}

double fmin(double x, double y)
{
	if (x != x) return y;
	if (y != y) return x;
	if (x == 0 && y == 0) return __signbit(x) ? x : y;
	return x < y ? x : y;
}
