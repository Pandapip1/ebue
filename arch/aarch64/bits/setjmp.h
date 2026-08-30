/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Same oversized generic buffer shape as arch/x86_64/bits/setjmp.h.
 * Unused so far: this arch has no arch/aarch64/src/setjmp.S yet (no
 * setjmp/longjmp implementation has been ported to the Linux platform
 * pilot), so nothing currently reads or writes specific offsets into
 * this buffer. Sized generously (32 8-byte slots = 256 bytes) so that
 * whenever that port happens -- aarch64's real callee-saved set is
 * x19-x30, sp, and d8-d15, 20 slots -- there is room without reopening
 * this header.
 */

typedef unsigned long long __jmp_buf[32];
