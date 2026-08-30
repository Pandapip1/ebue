/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fenv_t's real layout on this arch -- see include/fenv.h's own banner
 * for why (the x87 FSTENV/FLDENV 28-byte image, plus MXCSR: on x86_64
 * both units are live, since tcc compiles plain `double` arithmetic to
 * SSE2 here while src/math/ldbl_math.h's helpers stay x87 either way). Moved
 * out of the portable include/fenv.h into this arch's own bits/
 * header, the same way arch/x86_64/bits/setjmp.h already holds
 * __jmp_buf's real, arch-specific layout.
 */
typedef struct {
	unsigned char __x87env[28];
	unsigned int __mxcsr;
} __fenv_t;
