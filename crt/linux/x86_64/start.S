/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real ELF entry point for a native x86_64 Linux target -- the
 * PLAT_ARCH_GLOBS override this platform+arch combination needs (see
 * Makefile's own comment on that glob axis, and crt/linux/aarch64/
 * start.S's own banner for why this cannot be plain C: the kernel
 * jumps here with RSP pointing directly at argc, System V AMD64 ABI,
 * no valid return address anywhere, and no frame for a compiler-
 * generated prologue to build on top of -- the exact same reasoning,
 * just this arch's own registers/instructions).
 *
 * RSP is passed straight through as crt/linux/crt1.c's
 * __linux_start_main(long *sp) argument (rdi, the System V AMD64 ABI's
 * first integer argument register) -- that function reads argc/argv/
 * envp/auxv out of it directly; nothing here interprets the stack
 * layout itself.
 */
	.text
	.global _start
	.type _start,@function
_start:
	xor	%ebp, %ebp	// per the SysV AMD64 ABI's own convention: mark
				// the deepest stack frame (ebp/rbp == 0) so an
				// unwinder knows to stop here -- there is no
				// caller frame to walk into. Using the 32-bit
				// %ebp form zero-extends into %rbp for free,
				// one byte shorter than an explicit 64-bit xor.
	mov	%rsp, %rdi	// rdi = sp, pointing at argc (kernel's initial
				// stack layout, before this instruction
				// touches rsp at all)
	and	$-16, %rsp	// belt-and-braces: the ABI already guarantees
				// 16-byte alignment on entry, but nothing
				// before this instruction has touched rsp to
				// disturb it, so this is a cheap invariant to
				// just assert rather than trust blindly --
				// same spirit as aarch64/start.S's own `and
				// sp, x0, #-16`.
	call	__linux_start_main
	// unreachable: __linux_start_main is _Noreturn (ends in
	// __plat_terminate(), itself an infinite retry loop around
	// exit_group(2)). A raw fallback exit_group is kept anyway, the
	// same defensive-not-decorative spirit __plat_terminate() itself
	// documents and aarch64/start.S's own fallback already follows --
	// if it were ever reached, falling through to whatever garbage
	// follows this function in memory would be worse than a slightly
	// redundant syscall.
	xor	%edi, %edi
	mov	$231, %eax	// SYS_exit_group (x86_64) -- see
				// crt/linux/crt1.c's own SYS_mmap/SYS_write
				// banner for how this number was confirmed
				// (arch/x86/entry/syscalls/syscall_64.tbl).
	syscall
	.size _start, . - _start
