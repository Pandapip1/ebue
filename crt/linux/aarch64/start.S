/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real ELF entry point for a native aarch64 Linux target -- the
 * PLAT_ARCH_GLOBS override this platform+arch combination needs (see
 * Makefile's own comment on that glob axis).
 *
 * Cannot be plain C, for the same reason src/thread/linux/clone_
 * aarch64.S cannot: the kernel jumps here with SP pointing directly at
 * argc (System V ABI), no valid return address anywhere, and no frame
 * for a compiler-generated prologue to build on top of. A `_start`
 * written as an ordinary C function would still get a real prologue
 * (stack frame setup, possibly a stack-alignment `and sp, ...`
 * generated AFTER SP has already been used to compute something) with
 * no guarantee it runs before the compiler's own codegen reads
 * anything off the incoming stack -- musl and glibc both keep this
 * exact boundary in hand-written per-arch assembly for the same reason;
 * this is that same shape, one arch, matching this whole platform
 * pilot's own "aarch64 first, x86_64 designed but not yet ported"
 * scope (crt/linux/crt1.c's own banner).
 *
 * SP is passed straight through as crt/linux/crt1.c's __linux_start_
 * main(long *sp) argument (x0, AAPCS64's first integer argument
 * register) -- that function reads argc/argv/envp/auxv out of it
 * directly; nothing here interprets the stack layout itself.
 */
	.text
	.global _start
	.type _start,%function
_start:
	mov	x0, sp
	and	sp, x0, #-16	// belt-and-braces: the ABI already guarantees
				// 16-byte alignment on entry, but nothing
				// before this instruction has touched sp to
				// disturb it, so this is a cheap invariant to
				// just assert rather than trust blindly.
	bl	__linux_start_main
	// unreachable: __linux_start_main is _Noreturn (ends in
	// __plat_terminate(), itself an infinite retry loop around
	// exit_group(2)). A raw fallback exit_group is kept anyway, the
	// same defensive-not-decorative spirit __plat_terminate() itself
	// documents -- if it were ever reached, falling through to
	// whatever garbage follows this function in memory would be worse
	// than a slightly redundant syscall.
	mov	x0, #0
	mov	x8, #94		// SYS_exit_group (aarch64) -- see
				// src/exit/linux/plat_exit.c's own banner
				// for how this number was confirmed.
	svc	#0
	.size _start, . - _start
