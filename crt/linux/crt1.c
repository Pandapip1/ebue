/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Program startup for a real, native Linux target -- the platform-axis
 * override of crt/crt1.c (see Makefile's PLAT_GLOBS comment: this file
 * replaces the NT crt1.o object wholesale for PLATFORM=linux, the same
 * mechanism REPLACED_OBJS already gave every src/<module>/nt/plat_*.c
 * file).
 *
 * NT's crt1.c reads argv/environ out of the PEB, because Windows hands
 * a process one command-line string and one environment block that
 * still need parsing. Linux needs none of that: the kernel places
 * already-split argc/argv[]/envp[]/auxv[] directly on the initial
 * stack (System V ABI, all three 64-bit targets this project cares
 * about), so this file's job shrinks to reading them, not building
 * them -- no split_cmdline()/build_environ() equivalent exists here at
 * all, and no allocator is needed for argv/environ (they simply alias
 * the kernel-provided arrays in place).
 *
 * What NT's loader does for free and this file must do by hand instead:
 * TLS. A dynamically-linked Windows PE gets its TLS block set up by the
 * OS loader before the entry point runs; a statically-linked, no-libc
 * Linux ELF binary gets NOTHING -- the kernel does not even look at
 * PT_TLS. errno alone (src/internal/errno.c's `static __thread int
 * __errno_val`) makes this load-bearing for literally the first syscall
 * wrapper this program calls that can fail, so __linux_setup_tls()
 * below runs before anything else, including before __fd_init().
 *
 * What crt/crt1.c's __libc_start_main() also does that this file
 * deliberately does NOT yet call, each a real, separate, disclosed
 * gap rather than a silent omission:
 *
 *   __signal_init()   src/signal/signal.c calls RtlAddVectoredException
 *                      Handler directly, an ntdll import with no Linux
 *                      backend and no plat_* seam yet -- calling it here
 *                      would not even link. A real Linux signal-delivery
 *                      front end (sigaction-based fault/vectored-handler
 *                      equivalent) is separate, larger, future work.
 *   __fenv_init()      src/math/fenv.c's fegetenv()/fesetenv() are
 *                      unconditional x87/SSE inline asm (`fnstenv`,
 *                      `stmxcsr`) with no aarch64 branch at all --
 *                      calling it would not even COMPILE on this arch.
 *                      Floating-point environment control has no aarch64
 *                      (FPCR/FPSR) implementation yet on any platform.
 *   exit()'s front door src/exit/exit.c's exit()/_Exit() call
 *   (not called here)  __child_resume_stopped() (src/process/children.c)
 *                      before __plat_terminate() -- job-control
 *                      bookkeeping this file has not audited for Linux
 *                      safety. This file calls __plat_terminate()
 *                      (src/internal/plat_exit.h) directly instead,
 *                      the same terminal call exit()'s front door
 *                      itself ends in, just without that one
 *                      unaudited step in front of it. A program built
 *                      against this crt that calls exit()/_Exit()
 *                      itself still goes through the full front door;
 *                      only THIS file's own post-main() call is
 *                      narrowed.
 *
 * None of these block argv/environ/TLS/errno/main()/exit-status working
 * end to end, which is what this file exists to prove -- see
 * tools/linux-build-crt.sh's verification program.
 */
#include <sys/mman.h>
#include <string.h>
#include "libc.h"
#include "plat_exit.h"

int main(int, char **, char **);

char **environ;
char **__argv;
int __argc;
char *__progname;
char *__progname_full;

/* Same raw syscall trampoline every Linux backend in this tree defines
 * for itself -- see src/mman/linux/plat_mem.c's banner for why this is
 * never `extern long syscall(long, ...)` (that resolves against the
 * HOST's glibc at link time; this file's own link has no host libc at
 * all, so that spelling would not even link, the same gap just fixed
 * in src/exit/linux/plat_exit.c). Needed here only for the one
 * bootstrap mmap() the TLS block requires, issued before __fd_init()
 * and long before any general allocator is safe to assume -- deliberately
 * NOT routed through src/internal/plat_mem.h's __plat_mmap_anon(): that
 * interface is for the library's own callers (mmap() the public
 * function), not for the crt's own one-time internal bootstrap
 * allocation, and pulling in the mman subsystem this early is exactly
 * the kind of ordering hazard TLS setup itself already had to avoid. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	register long x8 __asm__("x8") = nr;
	__asm__ volatile("svc #0"
	                 : "+r"(x0)
	                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
	                 : "memory", "cc");
	return x0;
}

#define SYS_mmap 222
#define SYS_write 64

/* Minimal local ELF64/auxv shapes -- this project ships no <elf.h> yet
 * (a real one is separate future work; nothing outside this file needs
 * PT_TLS/AT_PHDR today), so just enough of the System V ABI to find one
 * program header. Field widths and order are architecture-independent
 * for ELFCLASS64 -- this is not an aarch64-specific shape, unlike the
 * raw_syscall trampoline above. */
struct elf64_phdr {
	unsigned int p_type;
	unsigned int p_flags;
	unsigned long p_offset;
	unsigned long p_vaddr;
	unsigned long p_paddr;
	unsigned long p_filesz;
	unsigned long p_memsz;
	unsigned long p_align;
};
#define PT_TLS 7

struct auxv_entry {
	unsigned long a_type;
	unsigned long a_val;
};
#define AT_NULL  0
#define AT_PHDR  3
#define AT_PHENT 4
#define AT_PHNUM 5

/* aarch64's TLS layout (the ARM AAELF64 ABI's "variant I"): the thread
 * pointer (TPIDR_EL0) addresses a fixed 2-pointer TCB header (dtv slot,
 * reserved -- both unused here: this program is a single, statically
 * linked TLS module, so there is no dynamic module array to point a
 * dtv at), and the module's own TLS data begins immediately after that
 * header, at tp + 16, rounded up to the segment's own alignment. A
 * `__thread`-qualified variable's address is `tp + 16 + <link-time
 * offset within the TLS segment>` -- the compiler and linker already
 * computed that offset into every access (R_AARCH64_TLSLE_* Local
 * Exec relocations, the only kind a non-PIE, non-dlopen'd, single-
 * module binary like this one ever needs); this function's only job is
 * making sure `tp + 16` really does land on a correctly-sized,
 * correctly-initialized copy of PT_TLS's data. Modeled on musl's own
 * arch/aarch64 static-TLS bootstrap, independently re-derived here
 * against the AAELF64 spec since this project links against neither
 * musl nor glibc's crt.
 *
 * x86_64 is NOT implemented here (variant II: the TLS block sits
 * BEFORE tp, at negative offsets, and tp is set via
 * arch_prctl(ARCH_SET_FS) rather than a plain register write) -- see
 * this file's own "x86_64 next" note below main(); every call site
 * below is aarch64-only pending that port, matching this whole
 * platform pilot's own declared scope elsewhere in the tree.
 */
static void linux_setup_tls(long *auxv)
{
	unsigned long phdr = 0, phent = 0, phnum = 0;
	struct elf64_phdr *tls = 0;
	unsigned long i;
	unsigned long tcb_size, data_align, alloc_size;
	long mm;
	unsigned char *base, *data;

	for (; auxv[0] != AT_NULL; auxv += 2) {
		if (auxv[0] == AT_PHDR) phdr = (unsigned long)auxv[1];
		else if (auxv[0] == AT_PHENT) phent = (unsigned long)auxv[1];
		else if (auxv[0] == AT_PHNUM) phnum = (unsigned long)auxv[1];
	}
	if (!phdr || !phent || !phnum) return; /* no auxv -- nothing to set up */

	for (i = 0; i < phnum; i++) {
		struct elf64_phdr *ph = (struct elf64_phdr *)(phdr + i * phent);
		if (ph->p_type == PT_TLS) { tls = ph; break; }
	}

	/* No PT_TLS segment at all (e.g. a test program with no __thread
	 * variables reachable from its own translation units) is not an
	 * error -- TPIDR_EL0 simply stays whatever it was on entry (0 on a
	 * fresh Linux thread), and nothing this program actually runs will
	 * ever dereference it. */
	if (!tls) return;

	data_align = tls->p_align > 16 ? tls->p_align : 16;
	tcb_size = 16; /* dtv + reserved, fixed by the ABI */
	alloc_size = tcb_size + tls->p_memsz + data_align; /* slack for alignment */

	mm = raw_syscall(SYS_mmap, 0, (long)alloc_size, PROT_READ | PROT_WRITE,
	                 MAP_PRIVATE | MAP_ANONYMOUS, -1L, 0L);
	if ((unsigned long)mm >= (unsigned long)-4095L) return; /* bootstrap alloc failed -- leave tp at 0 */
	base = (unsigned char *)mm;

	data = base + tcb_size;
	data = (unsigned char *)(((unsigned long)data + data_align - 1) & ~(data_align - 1));

	memcpy(data, (void *)tls->p_vaddr, tls->p_filesz);
	memset(data + tls->p_filesz, 0, tls->p_memsz - tls->p_filesz);

	{
		unsigned char *tp = data - tcb_size;
		((void **)tp)[0] = 0; /* dtv -- unused, single static module */
		((void **)tp)[1] = 0; /* reserved */
		__asm__ volatile("msr tpidr_el0, %0" : : "r"(tp) : "memory");
	}
}

_Noreturn void __linux_start_main(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;
	long *auxv = (long *)envp;
	char *slash;
	int rc;

	/* Checked before anything else in this function -- see
	 * src/internal/ldbl_layout_check.c's own banner. Needs no argv/
	 * envp/auxv/TLS, nothing this function sets up below; safe, and
	 * necessary, to check before all of it. Unlike the NT side (crt/
	 * crt1.c), fd 2 needs no setup at all -- it is stderr from the
	 * kernel's own exec(2) contract, before a single instruction of
	 * this program has run -- so this can both report AND run as the
	 * literal first statement, no reordering trade-off to make. A raw
	 * write(2), not stdio: stdio does not exist yet, and the message
	 * is a single static string, so nothing here needs to allocate.
	 * Exit code 111 is a plain, distinct sentinel (this platform's
	 * __plat_terminate() takes an ordinary process exit code, not a
	 * rich status the way NT's NtTerminateProcess() does) -- picked to
	 * be unambiguous in a shell's $? or a test harness's exit-status
	 * log, not mistakable for this program's own exit(3) value or a
	 * signal-death encoding (128+n). */
	if (!__verify_ldbl_layout()) {
		static const char msg[] =
			"ntlibc: long double bit-layout assumption failed at startup\n";
		raw_syscall(SYS_write, 2, (long)msg, sizeof msg - 1, 0, 0, 0);
		__plat_terminate(111);
	}

	while (*auxv) auxv++;
	auxv++; /* skip envp's own NULL terminator -- auxv starts right after */

	linux_setup_tls(auxv);

	__argc = (int)argc;
	__argv = argv;
	environ = envp;
	__progname_full = argc > 0 ? argv[0] : "";
	slash = __progname_full;
	for (char *p = __progname_full; *p; p++)
		if (*p == '/') slash = p + 1;
	__progname = slash;

	__fd_init();

	rc = main((int)argc, argv, envp);
	__plat_terminate(rc);
}
