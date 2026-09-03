/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real ELF entry point for a native aarch64 Linux target -- the
 * PLAT_ARCH_GLOBS override this platform+arch combination needs (see
 * Makefile's own comment on that glob axis).
 *
 * Cannot be plain C: the kernel jumps here with SP pointing directly at
 * argc (System V ABI), no valid return address, and no frame for a
 * compiler-generated prologue to build on top of -- an ordinary C
 * function would get a real prologue with no guarantee it runs before
 * the compiler's own codegen reads the incoming stack.
 *
 * SP is passed straight through as crt/linux/crt1.c's
 * __linux_start_main(long *sp) argument (x0, AAPCS64's first integer
 * argument register); nothing here interprets the stack layout itself.
 */
	.text
	.global _start
	.type _start,%function
_start:
	mov	x0, sp
	and	sp, x0, #-16	// belt-and-braces: ABI already guarantees this,
				// but cheap to assert rather than trust blindly
	bl	__linux_start_main
	// unreachable: __linux_start_main is _Noreturn. Fallback kept
	// anyway -- falling through to whatever follows in memory would be
	// worse than a redundant syscall.
	mov	x0, #0
	mov	x8, #94		// SYS_exit_group (aarch64)
	svc	#0
	.size _start, . - _start
