/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#define FLT_EVAL_METHOD 0

/* Unlike arch/x86_64/bits/float.h (a Windows target, where `long double`
 * beyond `double` means the 80-bit x87 extended format), a real
 * AArch64 C ABI's `long double` -- when it is not simply `double` -- is
 * IEEE 754 binary128 ("quad"): 15 exponent bits (the same width x87
 * uses, hence the matching MAX_EXP/MAX_10_EXP values below) but a
 * 112-bit stored
 * mantissa (113 bits counting the implicit leading one) versus x87's
 * 64 explicit mantissa bits. Reusing the x86_64 branch's constants
 * here would report the wrong precision (LDBL_MANT_DIG=64 instead of
 * 113) and wrong epsilon/subnormal bounds, even though LDBL_MIN/MAX
 * happen to print identically for the digits shown -- same exponent
 * range, different mantissa width. clang on this target predefines
 * __SIZEOF_LONG_DOUBLE__ == 16 for real quad `long double`; this
 * project's tcc (NT-only, never builds for this arch) predefines
 * nothing, the same signal src/math/x87.h already relies on to tell
 * "genuinely extended" apart from "long double is just double" -- see
 * that file and arch/x86_64/bits/float.h's own comment.
 */
#if defined(__SIZEOF_LONG_DOUBLE__) && __SIZEOF_LONG_DOUBLE__ > 8

#define LDBL_TRUE_MIN 6.47517511943802511092443895822764655e-4966L
#define LDBL_MIN     3.36210314311209350626267781732175260e-4932L
#define LDBL_MAX     1.18973149535723176508575932662800702e+4932L
#define LDBL_EPSILON 1.92592994438723585305597794258492732e-34L

#define LDBL_MANT_DIG 113
#define LDBL_MIN_EXP (-16381)
#define LDBL_MAX_EXP 16384

#define LDBL_DIG 33
#define LDBL_MIN_10_EXP (-4931)
#define LDBL_MAX_10_EXP 4932

#define DECIMAL_DIG 36

#else /* long double is really just double here -- mirror DBL_* exactly */

#define LDBL_TRUE_MIN 4.94065645841246544177e-324L
#define LDBL_MIN     2.22507385850720138309e-308L
#define LDBL_MAX     1.79769313486231570815e+308L
#define LDBL_EPSILON 2.22044604925031308085e-16L

#define LDBL_MANT_DIG 53
#define LDBL_MIN_EXP (-1021)
#define LDBL_MAX_EXP 1024

#define LDBL_DIG 15
#define LDBL_MIN_10_EXP (-307)
#define LDBL_MAX_10_EXP 308

#define DECIMAL_DIG 17

#endif
