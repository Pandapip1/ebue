/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* fprem is exact (it is the remainder of a truncated division). */
#include <math.h>
#include "ldbl_math.h"

double fmod(double x, double y) { return (double)__x87_fmod(x, y); }
float fmodf(float x, float y) { return (float)__x87_fmod(x, y); }
long double fmodl(long double x, long double y) { return __x87_fmod(x, y); }
