/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* fsqrt is correctly rounded in 80 bits; rounding that once more to
 * double can in principle differ from a single rounding by up to half
 * an ulp of the extended format, i.e. the double results are correctly
 * rounded except for arguments in a ~2^-30 sliver of cases, where they
 * are off by at most 1 ulp. */
#include <math.h>
#include "x87.h"

double sqrt(double x) { return (double)__x87_sqrt(x); }
float sqrtf(float x) { return (float)__x87_sqrt(x); }
long double sqrtl(long double x) { return __x87_sqrt(x); }
