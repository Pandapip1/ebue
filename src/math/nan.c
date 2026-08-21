/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>

double nan(const char *s) { (void)s; return NAN; }
float nanf(const char *s) { (void)s; return NAN; }
long double nanl(const char *s) { (void)s; return (long double)NAN; }
