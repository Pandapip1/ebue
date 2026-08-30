/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <stdint.h>
#include "ldbl_math.h"

/* Rather than shift the sign/exponent bits off the top -- which is
 * exactly the "does this discard set bits" pattern
 * -fsanitize=unsigned-shift-base exists to flag, even though it is
 * well-defined modular arithmetic in C -- mask them off instead.  Same
 * bits examined, same result, and nothing here looks like an overflow
 * to begin with. */
int __fpclassify(double x)
{
	union { double f; uint64_t i; } u = { x };
	int e = (int)(u.i >> 52 & 0x7ff);
	if (!e) return u.i & 0x7fffffffffffffffULL ? FP_SUBNORMAL : FP_ZERO;
	if (e == 0x7ff) return u.i & 0xfffffffffffffULL ? FP_NAN : FP_INFINITE;
	return FP_NORMAL;
}

int __fpclassifyf(float x)
{
	union { float f; uint32_t i; } u = { x };
	int e = (int)(u.i >> 23 & 0xff);
	if (!e) return u.i & 0x7fffffffUL ? FP_SUBNORMAL : FP_ZERO;
	if (e == 0xff) return u.i & 0x7fffffUL ? FP_NAN : FP_INFINITE;
	return FP_NORMAL;
}

/* ntlibc is built with two compilers with two different long double
 * formats - see the NTLIBC_LDBL_EXTENDED comment in src/math/ldbl_math.h.
 *
 * Under tcc, "long double" is really just "double" (8 bytes, no 80-bit
 * extended format), so this classifies the same 64-bit layout as
 * __fpclassify() above (reading the 80-bit/16-byte layout a native
 * long double would need used to read two bytes past the actual
 * object, i.e. uninitialized stack garbage).
 *
 * Under gcc/mingw, "long double" is the true 80-bit x87 extended
 * format: 1 sign bit + 15 exponent bits, packed in memory as a
 * little-endian uint64_t explicit-integer-bit mantissa followed by a
 * uint16_t sign+exponent half, occupying the low 10 bytes of a 12- or
 * 16-byte object.  Layout and classification here match musl's
 * LDBL_MANT_DIG==64 union-ldshape implementation (src/internal/libm.h,
 * src/math/__fpclassifyl.c): a missing explicit integer bit (msb of
 * the mantissa) with a nonzero exponent means the encoding is
 * unsupported/invalid on real x87 hardware and is reported as NaN. */
int __fpclassifyl(long double x)
{
#if NTLIBC_LDBL_EXTENDED
	union { long double f; struct { uint64_t m; uint16_t se; } i; } u = { x };
	int e = u.i.se & 0x7fff;
	int msb = (int)(u.i.m >> 63);
	if (!e && !msb) return u.i.m ? FP_SUBNORMAL : FP_ZERO;
	if (e == 0x7fff) return (u.i.m & 0x7fffffffffffffffULL) ? FP_NAN : (msb ? FP_INFINITE : FP_NAN);
	if (!msb) return FP_NAN;
	return FP_NORMAL;
#else
	union { long double f; uint64_t i; } u = { x };
	int e = (int)(u.i >> 52 & 0x7ff);
	if (!e) return u.i & 0x7fffffffffffffffULL ? FP_SUBNORMAL : FP_ZERO;
	if (e == 0x7ff) return u.i & 0xfffffffffffffULL ? FP_NAN : FP_INFINITE;
	return FP_NORMAL;
#endif
}

int __signbit(double x)
{
	union { double f; uint64_t i; } u = { x };
	return (int)(u.i >> 63);
}

int __signbitf(float x)
{
	union { float f; uint32_t i; } u = { x };
	return (int)(u.i >> 31);
}

/* same NTLIBC_LDBL_EXTENDED split as __fpclassifyl() above: under the
 * true 80-bit format the sign bit lives in the top bit of the
 * sign+exponent halfword, not bit 63 of an 8-byte object. */
int __signbitl(long double x)
{
#if NTLIBC_LDBL_EXTENDED
	union { long double f; struct { uint64_t m; uint16_t se; } i; } u = { x };
	return u.i.se >> 15;
#else
	union { long double f; uint64_t i; } u = { x };
	return (int)(u.i >> 63);
#endif
}
