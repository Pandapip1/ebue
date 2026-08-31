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
 * the kind of ordering hazard TLS setup itself already had to avoid.
 *
 * Three bodies below, one per arch's own raw syscall calling
 * convention -- same "own syscall table per file" discipline every
 * src/.../linux/plat_*.c backend already follows (see e.g. src/fcntl/
 * linux/plat_fcntl.c's banner), just arch-guarded within one file
 * instead of split across PLAT_ARCH_GLOBS files: this trampoline is
 * six lines of near-boilerplate per arch, not enough to earn its own
 * source tree split the way crt/linux/$(ARCH)/start.S's real entry
 * point (a different instruction set and calling convention, not just
 * different register names) needs one. */
#if defined(__aarch64__)
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
#elif defined(__x86_64__)
/* System V AMD64 syscall convention: rax = number, rdi/rsi/rdx/r10/r8/
 * r9 = up to 6 arguments (r10, not rcx -- `syscall` itself clobbers
 * rcx with the return address), result in rax. rcx and r11 (clobbered
 * by `syscall` for the return address and saved rflags respectively)
 * must be declared clobbered even though nothing here reads them
 * afterward -- leaving them off would let the compiler assume they
 * still hold whatever it last put there. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	register long r9  __asm__("r9")  = a6;
	__asm__ volatile("syscall"
	                 : "=a"(ret)
	                 : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
	                 : "rcx", "r11", "memory");
	return ret;
}
#define SYS_mmap 9
#define SYS_write 1
#elif defined(__i386__)
/* i386 has no register wide enough to hold both an argument AND stay
 * free for `int $0x80`'s own use, once all of ebx/ecx/edx/esi/edi are
 * spoken for by the first five arguments -- a 6th (only SYS_mmap2
 * below actually needs one) has nowhere left to go but ebp, which cdecl
 * also reserves as this function's own frame-pointer register. The
 * fix (the same one real low-level i386 syscall code uses): build an
 * explicit array of the seven words this syscall needs (nr, then
 * a1..a6), point eax at it, load ebx/ecx/edx/esi/edi/ebp from memory
 * through that pointer, load eax itself LAST (overwriting the pointer
 * with the actual syscall number, the very last thing needed before
 * `int $0x80`), then manually save/restore ebx and ebp around the
 * whole sequence since the compiler was never told they get clobbered
 * (both are cdecl callee-saved; they cannot appear in this asm's own
 * clobber list without a "cannot find a register" error given how
 * heavily this function already needs). */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long args[7];
	long ret;
	args[0] = nr; args[1] = a1; args[2] = a2; args[3] = a3;
	args[4] = a4; args[5] = a5; args[6] = a6;
	__asm__ volatile(
		"pushl %%ebp\n\t"
		"pushl %%ebx\n\t"
		"movl 4(%%eax), %%ebx\n\t"
		"movl 8(%%eax), %%ecx\n\t"
		"movl 12(%%eax), %%edx\n\t"
		"movl 16(%%eax), %%esi\n\t"
		"movl 20(%%eax), %%edi\n\t"
		"movl 24(%%eax), %%ebp\n\t"
		"movl (%%eax), %%eax\n\t"
		"int $0x80\n\t"
		"popl %%ebx\n\t"
		"popl %%ebp"
		: "=a"(ret)
		: "a"(args)
		: "ecx", "edx", "esi", "edi", "memory", "cc");
	return ret;
}
/* SYS_mmap2, not the old single-struct-arg SYS_mmap (90): mmap2 takes
 * its six arguments in plain registers, matching every other syscall
 * this trampoline calls, at the cost of the offset argument being in
 * PAGE units rather than bytes -- moot here, every call site below
 * only ever passes offset 0. */
#define SYS_mmap 192
#define SYS_write 4
#else
#error "crt/linux/crt1.c: unsupported architecture (expected __aarch64__, __x86_64__ or __i386__)"
#endif

/* Minimal local ELF/auxv shapes -- this project ships no <elf.h> yet (a
 * real one is separate future work; nothing outside this file needs
 * PT_TLS/AT_PHDR today), so just enough of the System V ABI to find
 * one program header. auxv itself (a flat array of word-pairs) is the
 * same shape on every arch this file supports -- `unsigned long`
 * already matches each arch's own native word size (64-bit on aarch64/
 * x86_64, 32-bit on i386), exactly like __linux_start_main()'s own
 * `long *sp` below. The program header struct is NOT arch-independent,
 * though, and it would be a real bug to treat it as such: ELFCLASS64's
 * Elf64_Phdr and ELFCLASS32's Elf32_Phdr do not just differ in field
 * width, their FIELD ORDER differs too (p_flags is the second field in
 * Elf64_Phdr but the second-to-last in Elf32_Phdr) -- so i386 gets its
 * own struct, not a narrowed reuse of the 64-bit one. */
#if defined(__i386__)
struct elf_phdr {
	unsigned int p_type;
	unsigned int p_offset;
	unsigned int p_vaddr;
	unsigned int p_paddr;
	unsigned int p_filesz;
	unsigned int p_memsz;
	unsigned int p_flags;
	unsigned int p_align;
};
#else
struct elf_phdr {
	unsigned int p_type;
	unsigned int p_flags;
	unsigned long p_offset;
	unsigned long p_vaddr;
	unsigned long p_paddr;
	unsigned long p_filesz;
	unsigned long p_memsz;
	unsigned long p_align;
};
#endif
#define PT_TLS 7

struct auxv_entry {
	unsigned long a_type;
	unsigned long a_val;
};
#define AT_NULL  0
#define AT_PHDR  3
#define AT_PHENT 4
#define AT_PHNUM 5

/* Shared across all three arches: walk auxv for AT_PHDR/AT_PHENT/
 * AT_PHNUM, then the program header table itself, looking for PT_TLS.
 * Everything arch-specific (the TCB layout, where the module's data
 * sits relative to the thread pointer, and how the thread pointer
 * register itself gets set) lives in linux_setup_tls() below, per
 * arch -- this helper is pure ELF/auxv bookkeeping, identical logic
 * regardless of TLS "variant". See this same nonnull-vs-disclosed-
 * residual pattern this function inherits verbatim from its own
 * former single-arch body, previously inline in linux_setup_tls()
 * itself. */
static struct elf_phdr *find_tls_phdr(long *auxv)
    __attribute__((nonnull(1)));
static struct elf_phdr *find_tls_phdr(long *auxv)
{
	unsigned long phdr = 0, phent = 0, phnum = 0;
	unsigned long i;

	for (; auxv[0] != AT_NULL; auxv += 2) {
		if (auxv[0] == AT_PHDR) phdr = (unsigned long)auxv[1];
		else if (auxv[0] == AT_PHENT) phent = (unsigned long)auxv[1];
		else if (auxv[0] == AT_PHNUM) phnum = (unsigned long)auxv[1];
	}
	if (!phdr || !phent || !phnum) return 0; /* no auxv -- nothing to set up */

	/* ph->p_type below is a disclosed, deliberately unmarked residual,
	 * surfaced only after auxv's own nonnull mark let this checker
	 * explore further into this function than before (the "deeper
	 * exploration unlocked" effect prior sweeps in this tree already
	 * measured, not a regression): ph is `(struct elf_phdr *)(phdr + i
	 * * phent)`, a local computed from phdr -- itself not a parameter,
	 * but a VALUE read out of the kernel-supplied auxiliary vector's
	 * own AT_PHDR entry a few lines above, guarded by `if (!phdr ||
	 * ...) return;` before this loop is ever reached. `nonnull` has no
	 * parameter to describe either fact on. Verified sound by hand
	 * regardless: the Linux kernel's own ELF auxiliary vector contract
	 * guarantees AT_PHDR points to the running image's real, mapped
	 * program headers table whenever it is present at all -- the same
	 * "external, non-in-tree, documented platform contract" class as
	 * auxv/sp themselves. */
	for (i = 0; i < phnum; i++) {
		struct elf_phdr *ph = (struct elf_phdr *)(phdr + i * phent);
		if (ph->p_type == PT_TLS) return ph;
	}
	return 0;
}

#if defined(__aarch64__)
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
 */
static void linux_setup_tls(long *auxv)
{
	struct elf_phdr *tls = find_tls_phdr(auxv);
	unsigned long tcb_size, data_align, alloc_size;
	long mm;
	unsigned char *base, *data;

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

	memcpy(data, (void *)(unsigned long)tls->p_vaddr, tls->p_filesz);
	memset(data + tls->p_filesz, 0, tls->p_memsz - tls->p_filesz);

	{
		unsigned char *tp = data - tcb_size;
		((void **)tp)[0] = 0; /* dtv -- unused, single static module */
		((void **)tp)[1] = 0; /* reserved */
		__asm__ volatile("msr tpidr_el0, %0" : : "r"(tp) : "memory");
	}
}
#elif defined(__x86_64__) || defined(__i386__)
/* x86_64/i386's TLS layout (the "variant II" the AAELF64 comment above
 * contrasts itself with): the module's own TLS data sits BEFORE the
 * thread pointer, at negative offsets from it, and the thread pointer
 * itself addresses a TCB whose first word is a SELF-pointer (tp->self
 * == tp) -- not a dtv slot the way variant I's header starts. A
 * `__thread` access under the Local Exec model (the only model a non-
 * PIE, non-dlopen'd, single-module binary like this one ever needs,
 * same as aarch64's own TLSLE note above) compiles to `%fs:(tpoff)` /
 * `%gs:(tpoff)` with tpoff a small NEGATIVE compile-time constant the
 * static linker already baked in (R_X86_64_TPOFF32 / R_386_TLS_TPOFF,
 * resolved at LINK time against the TLS segment's own declared size --
 * no runtime relocation left for this file to process) -- so this
 * function's only job, same as aarch64's, is making sure the memory at
 * `tp + tpoff` for every such already-computed tpoff really does
 * contain a correctly-sized, correctly-initialized copy of PT_TLS's
 * data, with tp placed exactly `round_up(p_memsz, p_align)` bytes
 * after that data begins. Modeled on musl's own x86_64/i386 static-TLS
 * bootstrap (`__init_tp`), independently re-derived here against the
 * x86-64/i386 psABI TLS chapters.
 *
 * What sets the thread pointer register itself differs between the two
 * -- x86_64 has a dedicated arch_prctl(2) syscall for FS_BASE; i386 has
 * no such syscall at all (arch_prctl is x86_64-only) and must instead
 * install a full GDT-style segment descriptor via set_thread_area(2)
 * and then load %gs with the resulting selector -- so THAT half is
 * further arch-guarded below, while the TCB layout/allocation logic
 * above it is shared between the two, both being variant II. */
static void linux_setup_tls(long *auxv)
{
	struct elf_phdr *tls = find_tls_phdr(auxv);
	unsigned long tls_size, data_align, tcb_size, alloc_size;
	long mm;
	unsigned char *base, *data, *tp;

	/* No PT_TLS segment: see aarch64's identical comment above -- the
	 * thread pointer register simply stays whatever it was on entry. */
	if (!tls) return;

	/* data_align MUST be the segment's own true p_align here, NOT
	 * inflated to some larger minimum the way aarch64's identical-
	 * looking `data_align` above safely is: aarch64's tpoff is a
	 * POSITIVE `16 + link-time-offset`, so padding the header out to a
	 * larger alignment only ever adds slack after a fixed point and
	 * cannot disagree with anything the linker assumed. Variant II's
	 * tpoff is NEGATIVE and computed by the linker as exactly `offset
	 * - round_up(p_memsz, p_align)` using the segment's OWN declared
	 * p_align -- silently rounding up to a larger alignment here would
	 * grow tls_size beyond what every already-compiled `%fs:(tpoff)` /
	 * `%gs:(tpoff)` access expects, moving tp away from where this
	 * function's own data copy actually landed. Caught empirically,
	 * not anticipated: an inflated minimum here (this function's own
	 * former version) passed compilation and even ran without
	 * faulting, but silently read the wrong memory for a fresh
	 * `__thread` variable's own static initializer -- disassembly of
	 * the failing case showed the compiled access as `%fs:0` (tp's own
	 * self-pointer) then `-0x8(%rax)`, i.e. tpoff=-8, matching
	 * round_up(p_memsz=8, the real p_align=4)=8 exactly, NOT the
	 * inflated 16 this function used to compute tls_size with. */
	data_align = tls->p_align ? tls->p_align : 1;
	tcb_size = sizeof(void *); /* just the self-pointer -- nothing here
	                            * ever reads a dtv, so no second word is
	                            * needed the way aarch64's 2-pointer
	                            * header carries one unconditionally.
	                            * May end up more loosely aligned than a
	                            * pointer store would ideally want if
	                            * data_align < sizeof(void*) -- harmless
	                            * on x86: an unaligned store just is not
	                            * atomic/fast, and nothing outside this
	                            * function's own immediately-following
	                            * arch_prctl()/set_thread_area() call
	                            * ever reads this word back. */
	tls_size = (tls->p_memsz + data_align - 1) & ~(data_align - 1);
	alloc_size = tls_size + tcb_size + data_align; /* slack for alignment */

	mm = raw_syscall(SYS_mmap, 0, (long)alloc_size, PROT_READ | PROT_WRITE,
	                 MAP_PRIVATE | MAP_ANONYMOUS, -1L, 0L);
	if ((unsigned long)mm >= (unsigned long)-4095L) return; /* bootstrap alloc failed -- leave tp unset */
	base = (unsigned char *)mm;

	data = (unsigned char *)(((unsigned long)base + data_align - 1) & ~(data_align - 1));
	tp = data + tls_size; /* the TCB starts immediately AFTER the TLS
	                       * data block, not before it -- the defining
	                       * difference from aarch64's layout above. */

	memcpy(data, (void *)(unsigned long)tls->p_vaddr, tls->p_filesz);
	memset(data + tls->p_filesz, 0, tls->p_memsz - tls->p_filesz);

	*(unsigned char **)tp = tp; /* TCB self-pointer -- the one field
	                             * every variant II ABI guarantees a
	                             * plain `mov %fs:0, %reg` (or %gs) can
	                             * always read back, even though nothing
	                             * in this pass's own code ever needs to
	                             * take that path itself. */

#if defined(__x86_64__)
#define ARCH_SET_FS 0x1002
#define SYS_arch_prctl 158
	raw_syscall(SYS_arch_prctl, ARCH_SET_FS, (long)tp, 0, 0, 0, 0);
#else /* __i386__ */
	/* i386 has no arch_prctl(2) -- the thread pointer is the %gs
	 * segment register, and %gs needs a real GDT-shaped descriptor
	 * behind it before it can be loaded at all (an arbitrary selector
	 * value loaded into %gs with no matching descriptor present just
	 * faults). set_thread_area(2) is exactly "install one such
	 * descriptor into a free GDT slot the kernel picks (entry_number
	 * == -1 on entry, filled in with the real slot on return), based
	 * on the fields below" -- base_addr is where %gs:0 should point
	 * (tp itself), limit/limit_in_pages/seg_32bit give it the full
	 * 4 GiB flat span every ordinary i386 data segment gets, and
	 * contents/read_exec_only/seg_not_present/useable are the
	 * remaining Intel segment-descriptor-access-byte bits
	 * set_thread_area(2)'s own ABI expects spelled out as individual
	 * ones, not packed into raw descriptor bytes by this caller.
	 * Field layout confirmed against the real Linux kernel UAPI
	 * (struct user_desc, include/uapi/asm-generic/... via
	 * arch/x86/include/uapi/asm/ldt.h): six bitfields packed into one
	 * trailing 32-bit word after entry_number/base_addr/limit, in
	 * exactly the declaration order below. */
	struct user_desc {
		unsigned int entry_number;
		unsigned int base_addr;
		unsigned int limit;
		unsigned int seg_32bit : 1;
		unsigned int contents : 2;
		unsigned int read_exec_only : 1;
		unsigned int limit_in_pages : 1;
		unsigned int seg_not_present : 1;
		unsigned int useable : 1;
	} u;
	unsigned short gs_selector;
#define SYS_set_thread_area 243

	u.entry_number = (unsigned int)-1;
	u.base_addr = (unsigned int)(unsigned long)tp;
	u.limit = 0xfffff;
	u.seg_32bit = 1;
	u.contents = 0;         /* MODIFY_LDT_CONTENTS_DATA */
	u.read_exec_only = 0;
	u.limit_in_pages = 1;   /* limit is in 4 KiB pages -> a 4 GiB span */
	u.seg_not_present = 0;
	u.useable = 1;

	if ((unsigned long)raw_syscall(SYS_set_thread_area, (long)&u, 0, 0, 0, 0, 0) >= (unsigned long)-4095L)
		return; /* could not install the descriptor -- leave %gs unset */

	/* GDT selector: (index << 3) | RPL 3 | TI=0 (GDT, not LDT) --
	 * ordinary user-mode segment-selector encoding, the same shape
	 * every i386 segment register value follows. */
	gs_selector = (unsigned short)((u.entry_number << 3) | 3);
	__asm__ volatile("movw %w0, %%gs" : : "r"(gs_selector) : "memory");
#endif
}
#endif

/* sp is required: `long argc = sp[0];` below is this function's very
 * first statement, dereferencing sp unconditionally with no guard.
 * Its one real caller is not anything in this tree but the kernel
 * itself: crt/linux/aarch64/start.S's own _start does `mov x0, sp` and
 * `bl __linux_start_main`, so sp is always the live initial stack
 * pointer the kernel set up for this process per the System V ABI's
 * own process-startup contract -- an external, non-in-tree caller with
 * a documented platform contract, the same class as this file's own
 * NT-side sibling crt/crt1.c's exception_handler() precedent. */
_Noreturn void __linux_start_main(long *sp)
    __attribute__((nonnull(1)));
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

	/* *auxv below is a disclosed, deliberately unmarked residual,
	 * surfaced only after sp's own nonnull mark let this checker
	 * explore further into this function than before: auxv is a local
	 * (`(long *)envp`, itself `argv + argc + 1`, itself `(char **)(sp
	 * + 1)`), not sp itself, so nonnull has no parameter left to
	 * describe this on. Verified sound by hand regardless, by the same
	 * System V ABI contract sp's own comment above already establishes
	 * (see __linux_start_main's own comment below): this whole chain
	 * of pointer arithmetic stays within the same kernel-provided
	 * initial stack block sp already points into. */
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
