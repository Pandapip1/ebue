/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <math.h>
#include <stdint.h>
#include "ldbl_math.h"

double copysign(double x, double y) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	union { double f; uint64_t i; } ux = { x }, uy = { y };
	ux.i = (ux.i & ((uint64_t)-1 >> 1)) | (uy.i & (uint64_t)1 << 63);
	return ux.f;
}

float copysignf(float x, float y) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	union { float f; uint32_t i; } ux = { x }, uy = { y };
	ux.i = (ux.i & 0x7fffffff) | (uy.i & 0x80000000u);
	return ux.f;
}

/* ntlibc is built with two compilers with two different long double
 * formats - see the NTLIBC_LDBL_EXTENDED comment in src/math/ldbl_math.h.
 *
 * Under tcc, "long double" is really just "double" (8 bytes, no 80-bit
 * extended format), so this is the same bit trick as copysign() above
 * (using the 80-bit/16-byte layout a native long double would need
 * would read/write past the actual object).
 *
 * Under gcc/mingw, "long double" is the true 80-bit x87 extended
 * format: the sign bit is the top bit of the little-endian uint16_t
 * sign+exponent half that follows the 64-bit mantissa in memory (see
 * __fpclassifyl's comment in fpclassify.c) - matching musl's
 * LDBL_MANT_DIG==64 copysignl(). */
long double copysignl(long double x, long double y) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
#if NTLIBC_LDBL_EXTENDED
	union { long double f; struct { uint64_t m; uint16_t se; } i; } ux = { x }, uy = { y };
	ux.i.se &= 0x7fff;
	ux.i.se |= uy.i.se & 0x8000;
	return ux.f;
#else
	union { long double f; uint64_t i; } ux = { x }, uy = { y };
	ux.i = (ux.i & ((uint64_t)-1 >> 1)) | (uy.i & (uint64_t)1 << 63);
	return ux.f;
#endif
}

// NOLINTEND(misc-include-cleaner)
