/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real ELF entry point for a native i386 Linux target. Cannot be
 * plain C, same reasoning as crt/linux/aarch64/start.S.
 *
 * Unlike aarch64/x86_64 (first argument in a register), i386's cdecl
 * convention passes its sole argument ON THE STACK, so this file has
 * to push a pointer to sp where `call` makes a cdecl callee find its
 * first argument, not just hand sp to a register and jump.
 */
	.text
	.global _start
	.type _start,@function
_start:
	xor	%ebp, %ebp	// mark the deepest stack frame (ebp == 0), same
				// as x86_64/start.S
	mov	%esp, %eax	// eax = sp, pointing at argc
	and	$-16, %esp	// belt-and-braces 16-byte align
	push	%eax		// __linux_start_main(long *sp)'s sole cdecl
				// argument, at 4(%esp) once inside the callee
	call	__linux_start_main
	// unreachable: __linux_start_main is _Noreturn; fallback kept anyway.
	xor	%ebx, %ebx
	mov	$1, %eax	// SYS_exit (i386), not exit_group: this fallback
				// only ever runs for a single-threaded process
				// that already fell off a never-returning
				// function, so "end this thread" and "end the
				// process" coincide.
	int	$0x80
	.size _start, . - _start
