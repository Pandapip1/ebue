/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real ELF entry point for a native x86_64 Linux target. Cannot be
 * plain C, same reasoning as crt/linux/aarch64/start.S: the kernel
 * jumps here with RSP pointing directly at argc (System V AMD64 ABI),
 * no valid return address, and no frame for a compiler-generated
 * prologue to build on top of.
 *
 * RSP is passed straight through as crt/linux/crt1.c's
 * __linux_start_main(long *sp) argument (rdi, the first integer
 * argument register); nothing here interprets the stack layout itself.
 */
	.text
	.global _start
	.type _start,@function
_start:
	xor	%ebp, %ebp	// SysV AMD64 ABI convention: rbp == 0 marks the
				// deepest stack frame for an unwinder. The 32-bit
				// %ebp form zero-extends into %rbp for free.
	mov	%rsp, %rdi	// rdi = sp, pointing at argc
	and	$-16, %rsp	// belt-and-braces: ABI already guarantees this,
				// but cheap to assert rather than trust blindly
	call	__linux_start_main
	// unreachable: __linux_start_main is _Noreturn. Fallback kept
	// anyway -- falling through to whatever follows in memory would be
	// worse than a redundant syscall.
	xor	%edi, %edi
	mov	$231, %eax	// SYS_exit_group (x86_64)
	syscall
	.size _start, . - _start
