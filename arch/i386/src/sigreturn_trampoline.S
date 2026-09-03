/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * i386's rt_sigaction(2) SA_RESTORER trampoline -- the i386 sibling of
 * arch/aarch64/src/sigreturn_trampoline.S and arch/x86_64/src/
 * sigreturn_trampoline.S; see the aarch64 file's own banner for the full
 * story (this project's own raw-syscall CRT, no host libc, so nothing
 * here leans on a glibc/vDSO-provided default restorer even where the
 * kernel happens to offer one) and src/signal/linux/plat_signal.c's
 * __plat_sig_install_fault_handlers()/__plat_sig_install_real_handler()
 * for the one real caller, which installs this as k_restorer alongside
 * a real C dispatch function (real_dispatch()) installed directly as
 * k_handler.
 *
 * cdecl: the kernel calls k_handler with the signal frame it built on
 * this thread's own stack, using i386's ordinary cdecl (stack-based)
 * argument-passing convention for a 3-argument SA_SIGINFO handler (sig,
 * &siginfo, &ucontext pushed onto the stack, in that order) -- an
 * ordinary C function needs no special linkage to receive that, and its
 * own `ret` pops the return address this trampoline's own address was
 * written as, landing here exactly the way real_dispatch()'s x86_64/
 * aarch64 siblings already do on their own arches. All this file has to
 * do is what a real sigreturn actually requires: ask the kernel to
 * restore the interrupted context from that same already-built frame.
 *
 * Confirmed against the real Linux i386 syscall table (this sandbox's
 * own real x86_64-linux-gnu glibc headers' i386 half, asm/unistd_32.h --
 * /usr/include/x86_64-linux-gnu/asm/unistd_32.h in a real multi-arch
 * rootfs, cross-checked against arch/x86/entry/syscalls/syscall_32.tbl):
 * __NR_rt_sigreturn is 173 on this arch (neither aarch64's 139 nor
 * x86_64's 15 -- three genuinely unrelated numbers, not one table offset
 * three ways), matching musl's src/signal/i386/restore.s (`movl $173,
 * %eax` / `int $0x80`), which this is a byte-for-byte port of -- the
 * simplest correct sigreturn trampoline shape there is on this arch,
 * same as its aarch64/x86_64 siblings claim for their own.
 *
 * `int $0x80`, not `sysenter`: matching every OTHER i386 raw syscall
 * this tree issues (crt/linux/crt1.c's/src/fcntl/linux/plat_fcntl.c's/
 * src/signal/linux/plat_signal.c's own i386 raw_syscall() bodies) --
 * `sysenter` is a real, faster alternative on hardware that supports it
 * but clobbers registers `int $0x80` does not and needs a matching
 * `sysexit`-based return path this project does not otherwise use
 * anywhere; consistency with this tree's own established i386 syscall
 * convention matters more here than the (immeasurable, for a signal
 * trampoline that runs once per delivered signal) throughput difference.
 * rt_sigreturn(2) takes no argument registers of its own -- the kernel
 * reads everything it needs (the saved sigcontext, mask, etc) directly
 * off the frame %esp already points at, which is exactly why this
 * trampoline needs no register setup beyond the syscall number itself,
 * unlike this tree's other i386 raw_syscall() bodies (no ebx/ecx/edx/
 * esi/edi/ebp argument-marshalling dance -- there is nothing to marshal).
 *
 * This file is arch-keyed (arch/i386/src/), matching cancel_trampoline.S's
 * own precedent in this same directory: the Makefile's ARCH_GLOBS
 * compiles it for every ARCH=i386 platform, Linux and the NT/WOA (tcc)
 * build alike, even though rt_sigreturn(2) is a Linux-only concept and
 * nothing on NT ever installs or calls this -- see the aarch64 sibling
 * for why building it unconditionally, rather than threading a platform
 * #ifdef through the Makefile, is the simpler correct choice. No
 * __TINYC__ split needed: `int $0x80` is an ordinary, long-supported
 * i386 instruction with no analogous tcc-assembler gap the way aarch64's
 * SVC/BRK probe found on tcc's arm64-win32 assembler.
 */
.text
.globl __ntlibc_sigreturn_trampoline
__ntlibc_sigreturn_trampoline:
	movl	$173, %eax		# SYS_rt_sigreturn (i386)
	int	$0x80
1:	jmp	1b			# unreachable: rt_sigreturn(2) never returns on success
