/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stack probe for tcc on aarch64 Windows (WOA), needed once a frame is
 * large enough to trigger it (tcc emits `bl __chkstk` for such frames).
 *
 * Unlike arch/{x86_64,i386}/src/chkstk.S, this is NOT the same calling
 * convention re-encoded in different mnemonics: Microsoft's documented
 * ARM64 __chkstk contract (learn.microsoft.com/cpp/build/
 * arm64-windows-abi-conventions, "The __chkstk helper") differs
 * genuinely from x86/x64's. The requested frame size arrives in X15 as
 * a count of 16-byte units (not bytes, not X0/W0), __chkstk probes
 * downward from the CURRENT SP without ever touching SP itself, may
 * clobber only X16/X17 (intra-procedure-call scratch, never
 * callee-saved), and the CALLER performs the actual
 * `SUB SP, SP, X15, LSL #4` after the call returns -- unlike x86/x64
 * where __chkstk both probes and establishes the frame itself.
 *
 * Adapted from LLVM compiler-rt's implementation
 * (compiler-rt/lib/builtins/aarch64/chkstk.S) with one mechanical
 * substitution: `b.gt` becomes `bgt`, since tcc's assembler doesn't
 * implement the dot-suffix conditional branch form.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(__chkstk)
_(__chkstk):
	lsl	x16, x15, #4		/* x16 = requested size in bytes */
	mov	x17, sp
1:
	sub	x17, x17, #4096		/* next page down */
	subs	x16, x16, #4096		/* remaining -= one page */
	ldr	xzr, [x17]		/* touch it (commits a guard page) */
	bgt	1b
	ret
