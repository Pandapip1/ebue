/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <math.h>
#include <stdint.h>
#include "ldbl_math.h"

double fabs(double x)
{
	union { double f; uint64_t i; } u = { x };
	u.i &= (uint64_t)-1 >> 1;
	return u.f;
}

float fabsf(float x)
{
	union { float f; uint32_t i; } u = { x };
	u.i &= 0x7fffffff;
	return u.f;
}

/* ntlibc is built with two compilers with two different long double
 * formats - see the NTLIBC_LDBL_EXTENDED comment in src/math/ldbl_math.h.
 *
 * Under tcc, "long double" is really just "double" (8 bytes, no 80-bit
 * extended format), so this is the same bit trick as fabs() above
 * (using the 80-bit/16-byte layout a native long double would need
 * would read/write past the actual object).
 *
 * Under gcc/mingw, "long double" is the true 80-bit x87 extended
 * format: the sign bit is the top bit of the little-endian uint16_t
 * sign+exponent half that follows the 64-bit mantissa in memory (see
 * __fpclassifyl's comment in fpclassify.c) - matching musl's
 * LDBL_MANT_DIG==64 fabsl(). */
long double fabsl(long double x)
{
#if NTLIBC_LDBL_EXTENDED
	union { long double f; struct { uint64_t m; uint16_t se; } i; } u = { x };
	u.i.se &= 0x7fff;
	return u.f;
#else
	union { long double f; uint64_t i; } u = { x };
	u.i &= (uint64_t)-1 >> 1;
	return u.f;
#endif
}

// NOLINTEND(misc-include-cleaner)
