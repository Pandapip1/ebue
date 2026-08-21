/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* fscale is exact (it only moves the exponent), so a single rounding
 * happens when the 80-bit result is stored to the destination type;
 * even results subnormal in double are correct because the scaled value
 * is exact in extended precision. */
#include <math.h>
#include "x87.h"

long double scalbnl(long double x, int n) { return __x87_scalbn(x, n); }
double scalbn(double x, int n) { return (double)__x87_scalbn(x, n); }
float scalbnf(float x, int n) { return (float)__x87_scalbn(x, n); }

double ldexp(double x, int n) { return scalbn(x, n); }
float ldexpf(float x, int n) { return scalbnf(x, n); }
long double ldexpl(long double x, int n) { return scalbnl(x, n); }
