/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <inttypes.h>

div_t div(int n, int d) { div_t r; r.quot = n / d; r.rem = n % d; return r; }
ldiv_t ldiv(long n, long d) { ldiv_t r; r.quot = n / d; r.rem = n % d; return r; }
lldiv_t lldiv(long long n, long long d) { lldiv_t r; r.quot = n / d; r.rem = n % d; return r; }
imaxdiv_t imaxdiv(intmax_t n, intmax_t d) { imaxdiv_t r; r.quot = n / d; r.rem = n % d; return r; }
