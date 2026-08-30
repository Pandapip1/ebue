/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fenv_t's real layout on this arch -- see include/fenv.h's own banner
 * for why (the x87 FSTENV/FLDENV 28-byte image; no MXCSR on i386,
 * where tcc never emits SSE for plain `double` arithmetic). Moved out
 * of the portable include/fenv.h into this arch's own bits/ header
 * the same way arch/i386/bits/setjmp.h already holds __jmp_buf's real,
 * arch-specific layout -- a future non-x86 arch's floating-point unit
 * has nothing resembling this image at all (see arch/aarch64/bits/
 * fenv.h: two plain 32-bit registers, FPCR and FPSR).
 */
typedef struct {
	unsigned char __x87env[28];
} __fenv_t;
