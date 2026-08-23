/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include "x87.h"

double floor(double x) { return (double)__x87_rndint(x, 1); }
float floorf(float x) { return (float)__x87_rndint(x, 1); }
long double floorl(long double x) { return __x87_rndint(x, 1); }

double ceil(double x) { return (double)__x87_rndint(x, 2); }
float ceilf(float x) { return (float)__x87_rndint(x, 2); }
long double ceill(long double x) { return __x87_rndint(x, 2); }

double trunc(double x) { return (double)__x87_rndint(x, 3); }
float truncf(float x) { return (float)__x87_rndint(x, 3); }
long double truncl(long double x) { return __x87_rndint(x, 3); }

double rint(double x) { return (double)__x87_rndint(x, -1); }
float rintf(float x) { return (float)__x87_rndint(x, -1); }
long double rintl(long double x) { return __x87_rndint(x, -1); }
