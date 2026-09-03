/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * x86_64's rt_sigaction(2) SA_RESTORER trampoline -- the x86_64 sibling
 * of arch/aarch64/src/sigreturn_trampoline.S; see that file's own banner
 * for the full story (this project's own raw-syscall CRT, no host libc,
 * so no glibc/vDSO-provided default restorer is LINKED here even though
 * one is technically mapped into every process; see below for why this
 * still writes its own rather than relying on it) and src/signal/linux/
 * plat_signal.c's __plat_sig_install_fault_handlers()/
 * __plat_sig_install_real_handler() for the one real caller, which
 * installs this as k_restorer alongside a real C dispatch function
 * (real_dispatch()) installed directly as k_handler.
 *
 * Unlike aarch64, x86_64's rt_sigaction(2) does NOT strictly require
 * SA_RESTORER/k_restorer to be supplied: arch/x86/kernel/signal.c's real
 * setup_rt_frame() falls back to the kernel-mapped vDSO's own
 * __kernel_rt_sigreturn symbol when SA_RESTORER is unset. That fallback
 * is not used here regardless -- plat_signal.c's own
 * __plat_sig_install_real_handler() is shared, portable C, unconditionally
 * setting SA_RESTORER and referencing __ntlibc_sigreturn_trampoline by
 * name for every arch it supports (see that function's own comment); this
 * file exists so that reference resolves to something REAL on x86_64 too,
 * the same "no host libc, no assumed runtime helper" discipline every
 * other Linux backend in this tree already follows (see plat_mem.c's own
 * banner) -- not because the vDSO fallback would be unsafe to lean on.
 *
 * System V AMD64 ABI: the kernel calls k_handler with rdi/rsi/rdx =
 * (sig, &siginfo, &ucontext) and pushes k_restorer's address as the
 * return address of the (SA_SIGINFO) frame it builds -- so real_dispatch()
 * (an ordinary System V C function) returns directly into this
 * trampoline via its own `ret`, ordinary call-stack unwinding, no
 * special linkage needed on real_dispatch()'s side. All this file has to
 * do, exactly like aarch64's sibling, is ask the kernel to restore the
 * interrupted context from that same already-built signal frame.
 *
 * Confirmed against the real Linux x86_64 syscall table (this sandbox's
 * own real x86_64-linux-gnu glibc headers, asm/unistd_64.h --
 * /usr/include/x86_64-linux-gnu/asm/unistd_64.h in a real x86_64 rootfs,
 * cross-checked against arch/x86/entry/syscalls/syscall_64.tbl):
 * __NR_rt_sigreturn is 15 on this arch (NOT 139, aarch64's number --
 * the two tables are unrelated, not offset by a fixed amount), matching
 * musl's src/signal/x86_64/restore.s (`mov $15, %rax` / `syscall`),
 * which this is a byte-for-byte port of -- the simplest correct
 * sigreturn trampoline shape there is on this arch, same as aarch64's
 * sibling claims for its own.
 *
 * `syscall`, not `int $0x80`: `int $0x80` is the legacy 32-bit
 * (i386/x32-compat) entry gate and does not honor the full 64-bit
 * register file a real rt_sigreturn(2) needs to restore (it silently
 * truncates the argument registers), matching this file's own arch --
 * every other x86_64 raw_syscall() in this tree already uses `syscall`
 * for the identical reason (see crt/linux/crt1.c's own banner).
 * rt_sigreturn(2) takes no argument registers in the ordinary sense --
 * the kernel reads everything it needs (the saved sigcontext, mask,
 * etc) directly off the frame `rsp` already points at, which is exactly
 * why this trampoline needs no register setup beyond the syscall number
 * itself.
 *
 * This file is arch-keyed (arch/x86_64/src/), matching cancel_
 * trampoline.S's own precedent one directory over: the Makefile's
 * ARCH_GLOBS compiles it for every ARCH=x86_64 platform, Linux and the
 * NT/WOA (tcc) build alike, even though rt_sigreturn(2) is a Linux-only
 * concept and nothing on NT ever installs or calls this -- see aarch64's
 * sibling for why building it unconditionally, rather than threading a
 * platform #ifdef through the Makefile, is the simpler correct choice.
 * Unlike aarch64's sibling, this file needs no __TINYC__ split: nothing
 * in this pilot's own NT/WOA x86_64 build path (tcc) has ever probed a
 * missing `syscall` mnemonic the way cancel_trampoline.S's own aarch64
 * banner documents for BRK on tcc's arm64-win32 assembler, and `syscall`
 * is an ordinary, long-supported x86_64 instruction with no analogous
 * gap to work around.
 */
.text
.globl __ntlibc_sigreturn_trampoline
__ntlibc_sigreturn_trampoline:
	mov	$15, %rax		# SYS_rt_sigreturn (x86_64)
	syscall
1:	jmp	1b			# unreachable: rt_sigreturn(2) never returns on success
