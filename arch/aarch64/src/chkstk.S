/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stack probe for tcc on aarch64 Windows (WOA): confirmed real and
 * needed, not speculative -- linking a nontrivial PLATFORM=nt
 * ARCH=aarch64 program (obj/sh/sh.exe, the first thing in this tree
 * with a large enough stack frame to trigger it) fails with `tcc:
 * error: unresolved reference to '__chkstk'` without this file.
 *
 * Unlike arch/{x86_64,i386}/src/chkstk.S, this is NOT the same
 * calling convention re-encoded in different mnemonics: Microsoft's
 * ARM64 __chkstk has a genuinely different contract from the x86/x64
 * one. x86/x64 tcc replaces a large frame's whole prologue with a
 * call to __chkstk, which must both probe the new pages AND establish
 * the frame itself (push the caller's saved fp, lower the real stack
 * pointer) before returning, because x86 has no cheap way to adjust
 * SP separately from committing it. ARM64's documented convention
 * (https://learn.microsoft.com/cpp/build/arm64-windows-abi-conventions,
 * "The __chkstk helper") is simpler and does not do that: the
 * requested frame size arrives in X15 as a count of 16-byte units
 * (not bytes, and not X0/W0 -- ARM64 reserves X15 specifically for
 * this), __chkstk probes downward from the CURRENT SP without ever
 * touching SP itself, may clobber only X16/X17 (the platform's own
 * intra-procedure-call scratch registers, never callee-saved), and
 * the CALLER performs the actual `SUB SP, SP, X15, LSL #4` after the
 * call returns. Getting this contract right (rather than mechanically
 * porting the x86 shape) matters: a __chkstk that adjusted SP itself,
 * or read the size from the wrong register, would silently corrupt
 * every large-frame caller's stack layout.
 *
 * Adapted from LLVM compiler-rt's real, authoritative implementation
 * (compiler-rt/lib/builtins/aarch64/chkstk.S -- Apache-2.0 WITH
 * LLVM-exception, fetched from the upstream llvm-project repository
 * rather than reconstructed from memory, this port's established
 * discipline) with exactly one mechanical substitution: `b.gt` (the
 * dot-suffix generic conditional branch form LLVM's own assembler
 * accepts) becomes the bare named mnemonic `bgt`, which tcc's inline/
 * .S assembler implements where the dot-suffix form does not (`b.ne`
 * fails "not implemented", `bne`/`bgt` do not -- see this port's
 * commit history for the wider mnemonic probe this comes from). Every
 * other instruction (lsl/mov/sub/subs/ldr/ret) and the loop's logic
 * are unchanged from the LLVM original.
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
