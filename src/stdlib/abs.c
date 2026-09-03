/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <stdlib.h>
#include <inttypes.h>

/* Negating a might overflow: INT_MIN (etc.) has no representable
 * positive counterpart, so a plain `-a` is undefined right at the one
 * input where callers most need a defined answer. Negating the
 * unsigned reinterpretation instead wraps modulo 2**N (C99 6.2.5p9),
 * giving the same bit pattern every existing caller already relies on
 * (abs(INT_MIN) == INT_MIN) without the intermediate UB. */
int abs(int a) { return a > 0 ? a : (int)-(unsigned)a; }
long labs(long a) { return a > 0 ? a : (long)-(unsigned long)a; }
long long llabs(long long a) { return a > 0 ? a : (long long)-(unsigned long long)a; }
intmax_t imaxabs(intmax_t a) { return a > 0 ? a : (intmax_t)-(uintmax_t)a; }

// NOLINTEND(misc-include-cleaner)
