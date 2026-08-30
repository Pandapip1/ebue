/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fenv_t's real layout on this arch: AArch64 has one floating-point
 * unit and its entire control/status state lives in exactly two
 * 32-bit registers, FPCR (rounding mode, trap enables) and FPSR
 * (sticky exception flags) -- no 28-byte FSTENV image, no separate
 * SSE unit to track alongside it (contrast arch/x86_64/bits/fenv.h).
 * See src/math/fenv.c's own aarch64 banner for the read/write
 * sequences (mrs/msr) and the FE_*-bit-position translation this
 * struct's two fields need at the src/math/fenv.c boundary.
 */
typedef struct {
	unsigned int __fpcr;
	unsigned int __fpsr;
} __fenv_t;
