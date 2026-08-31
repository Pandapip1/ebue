/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real ELF entry point for a native i386 Linux target -- the
 * PLAT_ARCH_GLOBS override this platform+arch combination needs (see
 * Makefile's own comment on that glob axis, and crt/linux/aarch64/
 * start.S's own banner for why this cannot be plain C: the kernel
 * jumps here with ESP pointing directly at argc, System V i386 ABI, no
 * valid return address anywhere, and no frame for a compiler-generated
 * prologue to build on top of -- the exact same reasoning, just this
 * arch's own registers/instructions).
 *
 * Unlike aarch64/x86_64's AAPCS64/SysV-AMD64 (first argument in a
 * register), i386's cdecl convention -- what __linux_start_main() is
 * compiled to expect -- passes its sole argument ON THE STACK, so this
 * file has to leave a pointer to sp sitting where a `call` makes a
 * cdecl callee find its first argument, not just hand sp to a register
 * and jump.
 */
	.text
	.global _start
	.type _start,@function
_start:
	xor	%ebp, %ebp	// per the SysV i386 ABI's own convention: mark
				// the deepest stack frame (ebp == 0) so an
				// unwinder knows to stop here -- same as
				// x86_64/start.S's identical first instruction.
	mov	%esp, %eax	// eax = sp, pointing at argc (kernel's initial
				// stack layout, before this instruction
				// touches esp at all)
	and	$-16, %esp	// belt-and-braces 16-byte align, same spirit
				// as aarch64/start.S and x86_64/start.S's own
				// identical asserts -- SysV i386 already
				// guarantees this on entry.
	push	%eax		// __linux_start_main(long *sp)'s sole cdecl
				// argument, pushed where `call` makes the
				// callee find it (4(%esp) once inside the
				// callee, past its own return address).
	call	__linux_start_main
	// unreachable: see x86_64/start.S's identical comment for why a
	// fallback exit is kept anyway even though __linux_start_main
	// itself is _Noreturn.
	xor	%ebx, %ebx
	mov	$1, %eax	// SYS_exit (i386) -- exit_group(252) would be
				// the more literal match to the other two
				// arches' fallback (see crt/linux/crt1.c's own
				// SYS_mmap/SYS_write banner for how these
				// numbers get confirmed), but plain exit(1)
				// is simpler and just as correct here: this
				// fallback only ever runs for a single-
				// threaded process that has already fallen
				// off the end of a function documented as
				// never returning, so "end this one thread"
				// and "end the whole process" coincide.
	int	$0x80
	.size _start, . - _start
