/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * aarch64's rt_sigaction(2) SA_RESTORER trampoline -- see
 * src/signal/linux/plat_signal.c's __plat_sig_install_fault_handlers()
 * for the one real caller, which installs this as k_restorer alongside
 * a real C dispatch function (fault_dispatch()) installed directly as
 * k_handler.
 *
 * Raw rt_sigaction(2) on aarch64 (no glibc, no vDSO trampoline the
 * kernel supplies for you -- both real on x86_64, neither real here)
 * REQUIRES SA_RESTORER: the kernel refuses to build a signal frame at
 * all otherwise (arch/arm64/kernel/signal.c's setup_return() is
 * unconditional about this on this arch). What SA_RESTORER buys is
 * nothing this file has to do by hand: the kernel, not this code, is
 * what calls k_handler with x0/x1/x2 = (sig, &siginfo, &ucontext) and,
 * critically, LR (x30) already pointing HERE before it does -- fault_
 * dispatch() is an ordinary AAPCS64 C function, so its own `ret` at the
 * end lands on this trampoline automatically, with SP still exactly
 * where the kernel's signal frame put it. All that is left for this
 * file to do is what a real sigreturn actually requires: ask the kernel
 * to restore the interrupted context from that same frame.
 *
 * Confirmed against the real Linux syscall table on this host
 * (asm-generic/unistd.h, the table aarch64 uses): __NR_rt_sigreturn is
 * 139, matching musl's src/signal/aarch64/restore.s (`mov x8,#139` /
 * `svc 0`), which this is a byte-for-byte port of -- the simplest
 * correct sigreturn trampoline shape there is, not a design choice this
 * file invented.
 *
 * This file is arch-keyed (arch/aarch64/src/), matching cancel_
 * trampoline.S's own precedent in this same directory: the Makefile's
 * ARCH_GLOBS compiles it for every ARCH=aarch64 platform, Linux and the
 * NT/WOA (tcc) build alike, even though rt_sigreturn(2) is a Linux-only
 * concept and nothing on NT ever installs or calls this. See that
 * file's own banner for why building it unconditionally, rather than
 * threading a platform #ifdef through the Makefile, is the simpler
 * correct choice; the __TINYC__ split below is this file's own version
 * of the identical accommodation cancel_trampoline.S already makes for
 * tcc's arm64-win32 assembler.
 */
.text
.globl __ntlibc_sigreturn_trampoline
__ntlibc_sigreturn_trampoline:
#ifdef __TINYC__
	/* tcc's arm64-win32 assembler does not implement SVC ("not
	 * implemented", the same probed limitation cancel_trampoline.S
	 * cites for BRK). Never reached on this platform regardless --
	 * nothing on NT ever installs this trampoline -- so an unreachable
	 * infinite loop is a correct substitute, exactly as cancel_
	 * trampoline.S already does for its own unreachable tail. */
1:	b	1b
#else
	mov	x8, #139		// SYS_rt_sigreturn (aarch64)
	svc	#0
2:	b	2b			// unreachable: rt_sigreturn(2) never returns on success
#endif
