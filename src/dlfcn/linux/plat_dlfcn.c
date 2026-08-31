/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * src/internal/plat_dlfcn.h's Linux backend: a real, from-scratch
 * ELF64 dynamic loader for dlopen()/dlsym()/dlclose()/dlerror(),
 * linked directly into libc.a/the static binary -- no separate ld.so,
 * no PT_INTERP dance, nothing mmap'd off disk and spliced in via hook
 * functions the way glibc's "static dlopen" retrofit works. This file
 * IS the loader.
 *
 * ============================================================
 * WHY THIS SHAPE, NOT GLIBC'S
 * ============================================================
 *
 * glibc's static-dlopen support (see this project's own research notes
 * this pass was briefed with) is not a self-contained static loader:
 * on first dlopen() it mmaps glibc's own separately-built ld.so off
 * disk and splices its relocation/symbol logic into the process via
 * __libc_register_dl_open_hook() and friends. That shape exists
 * because reusing an already-written, separately-linked ld.so was
 * cheaper than writing a second permanent copy of the same logic --
 * not because the ELF/OS model requires a separate image. It is also
 * the direct cause of glibc bug 20802 (getauxval() broken after static
 * dlopen) and long-standing NSS-loading pain: the spliced-in ld.so
 * never goes through a normal, fully-initialized startup.
 *
 * ntlibc's Linux port has no PT_INTERP to begin with -- crt/linux/
 * crt1.c/aarch64/start.S already own process startup from scratch, and
 * the main binary is non-PIE with no PT_DYNAMIC at all (tools/linux-
 * build-crt.sh's own comment). There is no existing ld.so to reuse and
 * nothing gained by inventing a second on-disk image just to mimic
 * glibc's retrofit. So this loader is ordinary code in libc.a: it
 * mmaps a caller-named .so, parses it, relocates it, and hands back an
 * opaque handle, using no more machinery than any other src/.../linux
 * backend in this tree already does: open()/pread()/close()/malloc()
 * through the ordinary public API (this file runs well after __fd_
 * init()/malloc init, unlike the earliest crt1.c-era code, so those are
 * safe to use directly) -- but its OWN raw mmap()/munmap()/mprotect()
 * syscall wrappers rather than <sys/mman.h>'s public functions, for a
 * real, empirically-found reason: see this file's own "raw mmap()/
 * munmap()/mprotect()" banner further down for why the public mmap()
 * front door's own reservation-table design (src/mman/mman.c) is
 * incompatible with this loader's address-space layout.
 *
 * ============================================================
 * WHAT THIS PASS IMPLEMENTS, PRECISELY
 * ============================================================
 *
 *   - ELF64, ELFCLASS64/ELFDATA2LSB, EM_AARCH64 only. aarch64 is the
 *     only Linux arch this whole platform pilot has proven (crt/linux/
 *     crt1.c's own banner); x86_64 needs its own relocation-type table
 *     (R_X86_64_RELATIVE etc. have different numeric values) and TLS
 *     model (variant II, negative tp-relative offsets) before this
 *     file could support it -- out of scope here, matching the rest of
 *     this pilot's own stated scope.
 *   - Only ET_DYN (shared object) input. A dlopen()'d PIE executable
 *     (also ET_DYN under the modern convention) would load the same
 *     way in principle but is not a case this pass tests or claims.
 *   - PT_LOAD segments: mapped faithfully, including the bss
 *     (p_memsz > p_filesz) tail-zeroing recipe every real ELF loader
 *     uses (see map_segments() below).
 *   - PT_DYNAMIC: DT_HASH (for an exact symbol count -- DT_GNU_HASH is
 *     explicitly NOT supported yet, see resolve_dynsym_count()),
 *     DT_SYMTAB/DT_STRTAB/DT_SYMENT, DT_RELA/DT_RELASZ/DT_RELAENT,
 *     DT_JMPREL/DT_PLTRELSZ/DT_PLTREL (PLT relocations are processed
 *     identically to DT_RELA -- this loader always binds eagerly,
 *     there is no lazy PLT stub mechanism here at all, so "RTLD_NOW
 *     vs RTLD_LAZY" is moot the same way it already is on the NT
 *     backend, just for a different underlying reason).
 *   - Relocation types: R_AARCH64_RELATIVE, R_AARCH64_ABS64,
 *     R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT. Anything else --
 *     including every TLS relocation type (R_AARCH64_TLSDESC,
 *     R_AARCH64_TLS_TPREL64, R_AARCH64_TLS_DTPMOD64/DTPREL64) --
 *     is a clean, loud dlopen() failure (see apply_one_reloc()'s
 *     `default:` case), never a silent mis-relocation.
 *   - PT_TLS: detected and REFUSED (dlopen() fails cleanly, before any
 *     memory is even mapped) rather than loaded incorrectly. See
 *     "TLS / per-library thread descriptors" below for why, and what a
 *     real implementation needs that this pass deliberately does not
 *     build.
 *   - DT_NEEDED (dependency .so's): NOT chased. A .so with unresolved
 *     external-library dependencies fails exactly the way an .so with
 *     any other unresolved symbol fails (a specific "undefined symbol"
 *     dlerror(), naming the symbol) -- see "Symbol resolution against
 *     the static binary" for what dependency-chasing would need to
 *     preserve if added later.
 *   - PT_GNU_RELRO: not applied. A segment relocations touch stays
 *     exactly as writable as its own p_flags already said -- no read-
 *     only-after-relocation hardening pass. A real hardening pass is
 *     future work; documented here rather than silently absent.
 *   - No __attribute__((constructor))/DT_INIT/DT_INIT_ARRAY execution.
 *     A dlopen()'d .so's own static initializers do not run. Fine for
 *     the plain-C test object this pass proves against; real C++/
 *     constructor-attribute consumers need this before they would work.
 *
 * ============================================================
 * SYMBOL RESOLUTION AGAINST THE STATIC BINARY (open design question 1)
 * ============================================================
 *
 * The gap: a dlopen()'d object's undefined R_AARCH64_GLOB_DAT/ABS64/
 * JUMP_SLOT relocations need to resolve against libc's/the main
 * program's own statically-linked symbols, and nothing in this tree
 * exports a symbol table for that -- the static link into libc.a
 * leaves no .dynsym at all (there is no PT_DYNAMIC on the main image),
 * so there is nothing shaped like a normal "shared object exports
 * list" to search.
 *
 * Three shapes were on the table (named in this task's own brief):
 * (a) a generated symbol table (a build step that scrapes libc.a's own
 *     symbols into a linked-in array), (b) a linker-script/section
 *     trick (e.g. a synthetic .dynsym-shaped section built at link
 *     time), (c) something else.
 *
 * This file takes (c): at symbol-resolution time, it opens
 * /proc/self/exe -- this same running binary's own file -- and reads
 * ITS OWN ELF section-header table to find .symtab/.strtab, the same
 * way `nm`/`readelf` would from outside the process. See
 * self_symtab_load() below.
 *
 * Why this, and not (a)/(b):
 *
 *   - It needs zero build-system changes: no new tool, no linker
 *     script, no generated table to keep in sync as libc.a's own
 *     symbol set changes across every future commit. (a) and (b) both
 *     require exactly that kind of generated artifact, and this
 *     project's own Makefile PLAT_GLOBS/REPLACED_OBJS machinery is
 *     already the one place a scheme like that would have to hook in
 *     -- real, but real *extra* surface this approach needs none of.
 *   - It is strictly MORE complete than a generated table for this
 *     purpose: a hand-scraped export list would need its own policy
 *     for "which symbols count" (all of libc.a? only ones some
 *     dlopen()'d .so happens to need, decided how?), where reading the
 *     real .symtab directly gets EVERY symbol the linker kept, main
 *     program included -- not just libc's own, which matters exactly
 *     as much as it does for a real dlopen(NULL, ...)/dlsym() global
 *     lookup (dlopen.html's "global symbol table handle for the
 *     currently running process image", not "for libc specifically").
 *   - It is exactly the same mechanism POSIX already asks dlopen(NULL,
 *     ...) + dlsym() to provide, so this file gets that case for free
 *     by construction: MAIN_IMAGE_HANDLE's __plat_dlsym() path below
 *     and a dlopen()'d object's own undefined-symbol resolution are
 *     literally the same function, resolve_main_symbol(). No second
 *     implementation of "look up a name in the main image" exists
 *     anywhere in this file.
 *
 * The real cost, stated plainly rather than hidden: this depends on
 * the running binary NOT being stripped (a stripped binary has no
 * .symtab at all -- self_symtab_load() fails cleanly in that case, and
 * every dlopen() needing an external symbol then fails with a clear
 * "cannot read own symbol table (stripped binary?)" dlerror() rather
 * than resolving anything silently wrong). A production build that
 * strips its output would need a real (a)/(b)-shaped generated table
 * instead -- that is genuinely deferred work, not a hidden gap: the
 * design space for it is exactly the two options this section named
 * and did not choose, should a stripped-binary story become necessary
 * later.
 *
 * Also worth stating for the NSS case flagged for the *next* pass
 * (not implemented here, but checked against this design so it is not
 * obviously precluded): NSS loads several independently-named modules
 * (libnss_files.so, libnss_dns.so, ...) that each need to resolve
 * against glibc's own internal helpers, not against each other. That
 * is exactly this file's existing shape already: every dlopen()'d
 * object resolves its undefined symbols against (its own definitions,
 * then) the one shared static-binary symbol table via
 * resolve_main_symbol() -- an NSS loader built later as a thin
 * wrapper choosing which libnss_<service>.so.<N> to dlopen() by
 * convention, then dlsym()ing a handful of well-known
 * _nss_<service>_* names out of the result, needs nothing new from
 * this file's resolution story. What it WOULD need first is DT_NEEDED
 * chasing if a real NSS module links against a second .so of its own
 * -- explicitly out of scope above, not silently assumed away.
 *
 * ============================================================
 * NAMESPACE ISOLATION / VERSION COEXISTENCE (open design question 2)
 * ============================================================
 *
 * The hard requirement: if A dlopen()s one version of B and C dlopen()s
 * a different version of B, both must coexist correctly, with no
 * global symbol-table collision -- and this has to be plain dlopen()'s
 * DEFAULT behavior, not an opt-in dlmopen()/LM_ID_NEWLM-style variant.
 * musl has no equivalent at all; glibc's dlmopen() is real but opt-in.
 *
 * This file's answer is almost embarrassingly direct, and worth
 * stating as a real, deliberate design choice rather than an
 * oversight: __plat_dlopen() NEVER deduplicates. Every call --
 * including two calls on the byte-identical path -- mmaps a fresh,
 * independent copy, gets its own kernel-chosen base address (mmap(NULL,
 * ...)'s own address-space-layout choice, which on a real Linux kernel
 * is not fixed run to run), and gets its own freshly-applied
 * relocations against that address. Two dlopen() instances of "the
 * same" .so never alias: neither their code pages, their GOT/data
 * pages, nor (once TLS support lands -- see below) their TLS.
 *
 * This is a deliberate, disclosed deviation from dlopen.html's own
 * DESCRIPTION: "Only a single copy of an executable object file shall
 * be brought into the address space, even if dlopen() is invoked
 * multiple times in reference to the file". This backend does not do
 * that. The two things POSIX's "shall" buys a caller -- pointer-
 * identity between repeat dlopen() calls on the same file, and not
 * paying to map the same bytes twice -- are exactly the two things
 * that create the collision hazard the user's requirement rules out:
 * a single shared copy is a single shared GOT, is a single namespace,
 * is exactly the "two versions of B fight over one global symbol
 * table" failure mode dlmopen() exists to opt OUT of on glibc. Making
 * that the exception (an opt-in you have to ask for) rather than the
 * rule is what glibc does; this backend inverts that default, per the
 * user's explicit brief. A caller that specifically wants the POSIX
 * single-copy behavior back can still get pointer-stable identity by
 * caching its own dlopen() result and never calling it twice for one
 * logical library -- exactly what most real programs already do in
 * practice; only a caller relying on the identity-on-repeat-dlopen()
 * guarantee ITSELF (rare, and not exercised by this project's own
 * test/posix-dl.c NT-side coverage, which tests refcounting, not
 * cross-call identity as a *feature* callers depend on) would notice.
 *
 * Because there is no dedup, there is also no refcounting to get
 * right: dlclose() unconditionally tears down exactly the one instance
 * its handle names (unmap the whole reservation, free the bookkeeping
 * struct) -- there is no "was this the last reference" question, since
 * every handle IS its own sole reference by construction. This also
 * means dlopen.html's other "single copy" implementation freedom (skip
 * re-loading a file that was removed before a later dlopen() of the
 * same path) is moot: nothing here is shared in the first place.
 *
 * DT_NEEDED chasing (not yet implemented -- see above) would need to
 * preserve this property, not undo it: a future implementation should
 * load each dependency freshly WITHIN the same top-level dlopen()
 * call's own namespace (never deduped against a sibling top-level
 * dlopen()'s own copy of the identical dependency), so the isolation
 * property this section establishes holds transitively through a
 * dependency graph, not just for a single flat .so.
 *
 * ============================================================
 * TLS / PER-LIBRARY THREAD DESCRIPTORS (open design question 3)
 * ============================================================
 *
 * The hard requirement: each loaded library's TLS block gets its OWN
 * TD (thread descriptor/TCB -- the struct the thread-pointer register
 * addresses), not a slot carved out of one shared per-thread TD the
 * way glibc's dynamic-TLS extension does it (glibc: one TCB per
 * thread, total; a dlopen()'d module's TLS is just a heap block a DTV
 * entry happens to point at, still logically "inside" that one TCB's
 * bookkeeping).
 *
 * crt/linux/crt1.c's linux_setup_tls() (read in full before writing
 * this section) sets TPIDR_EL0 to a freshly mmap'd block shaped
 * `{ dtv; reserved; <TLS data...> }` -- the AAELF64 "variant I" TCB
 * header (2 pointers) immediately followed by the module's own TLS
 * data, with dtv left NULL because there is exactly one static TLS
 * module (the main image) and nothing to index. That function's own
 * comment is explicit that this is single-module only.
 *
 * The concrete question this task asked to resolve: does "own TD per
 * library" mean swapping TPIDR_EL0 around calls into that library's
 * code, or something else (e.g. each library's TD reachable at a fixed
 * offset from the "real" per-thread TD, indexed rather than swapped)?
 *
 * This file's answer, worked through and NOT implemented in this pass
 * (see below for exactly what is missing and why): INDEX, NEVER SWAP.
 * Concretely:
 *
 *   - The real, TPIDR_EL0-addressed TCB gains a real DTV: an array of
 *     pointers, index 0 unused/reserved, index N (a small integer
 *     "TLS module id" assigned at dlopen() time to any loaded object
 *     that has a PT_TLS segment; module 1 is reserved for the main
 *     image) pointing not at a raw data blob, but at a SECOND,
 *     independently heap-allocated block shaped exactly like the
 *     real TCB itself: `{ dtv; reserved; <that module's own TLS
 *     data...> }`. Every loaded module's TLS block is headed by its
 *     own miniature TCB, satisfying "own TD per library" literally --
 *     even though TPIDR_EL0 itself is never pointed at it.
 *   - The compiler-generated access sequence for a `__thread` variable
 *     in a dlopen()'d .so already does the indexing for us: PIC code
 *     with TLS in a shared object uses the General-Dynamic model
 *     (R_AARCH64_TLSDESC, or the older TLSGD relocation pair) --
 *     __tls_get_addr(&tls_index)/the TLSDESC resolver, NOT a bare
 *     tp-relative offset the way the main image's Local-Exec TLS
 *     already works (see linux_setup_tls()'s own comment on why the
 *     main image gets away with pure LE offsets). A real __tls_get_addr
 *     implementation here would be exactly: `real_tcb = (tcb*)
 *     __builtin_thread_pointer(); mod_td = real_tcb->dtv[ti->module_id];
 *     return (char*)mod_td + TCB_HEADER_SIZE + ti->offset;` -- one
 *     pointer-array index off the real, never-swapped TCB, then a
 *     fixed +16 header skip into a SEPARATE td-shaped block. Nothing
 *     about entering or leaving the library's code touches TPIDR_EL0
 *     at all; every access, from anywhere, is this same lookup.
 *
 * Why NOT swap TPIDR_EL0 around calls into loaded-library code -- the
 * reasoning behind rejecting the other option the task asked to weigh,
 * stated concretely rather than asserted:
 *
 *   1. "A call into library code" is not a syntactically closed
 *      boundary a swap-on-entry/restore-on-exit discipline could hook
 *      reliably. dlsym() hands back a bare function pointer; nothing
 *      stops the CALLER from storing it in a struct, handing it to a
 *      third library, and having THAT library invoke it from arbitrary
 *      later control flow -- including from inside a signal handler
 *      that fires mid-call, or after a longjmp() unwinds past the
 *      "restore" half of a swap that a naive thunk assumed would
 *      always run. Getting every one of those paths right needs a
 *      compiler-generated thunk at every call site into and back out
 *      of the library, not something a loader can retrofit at
 *      dlopen()/dlsym() time.
 *   2. It makes TPIDR_EL0 a piece of invisible, dynamically-scoped
 *      global mutable state: two pieces of code that both look like
 *      "read errno" (src/internal/errno.c's own `__thread int
 *      __errno_val`) would silently mean different physical memory
 *      depending on an ambient register value neither reads nor
 *      writes explicitly -- exactly the class of non-local state this
 *      project avoids everywhere else (no hidden globals silently
 *      changing a function's meaning based on "what called it").
 *   3. Indexing needs zero call-site code generation changes: it is
 *      purely a change to what __tls_get_addr()/the TLSDESC resolver
 *      does internally, which is precisely where a compiler already
 *      routes GD/LD-model TLS access on any ELF platform by
 *      convention -- the existing relocation model was built assuming
 *      exactly this shape of indirection, just normally pointing the
 *      DTV slot at a raw data blob instead of at a second TCB header.
 *      Following that grain, rather than fighting it with a swap, is
 *      what "own TD per library" without runtime register-swapping
 *      buys.
 *
 * What is NOT built in this pass, disclosed rather than hidden: no DTV
 * field exists on the real TCB yet (linux_setup_tls() leaves dtv
 * permanently NULL), no module-id allocation exists, no
 * __tls_get_addr()/TLSDESC resolver is implemented, and this file's
 * own loader explicitly REFUSES to dlopen() any object with a PT_TLS
 * segment (see PT_TLS handling in __plat_dlopen() below) rather than
 * mapping one incorrectly. Building the above needs a crt1.c change
 * (extending the main TCB with a real dtv array) that this pass
 * deliberately did not make, to avoid touching the now-proven, working
 * non-dlopen startup path while landing the (already large) rest of
 * this loader -- exactly the kind of scope line this project's own
 * house style asks to be stated plainly rather than silently narrowed.
 * A `.so` with no `__thread` variables at all (the test object this
 * pass actually proves against) has no PT_TLS segment and is
 * unaffected by any of this.
 *
 * ============================================================
 * THREAD SAFETY
 * ============================================================
 *
 * Nothing in this file takes a lock: the self-symtab cache below
 * (self_symtab_load()) is lazily populated on first use with no
 * mutual exclusion, and dlopen()/dlclose() do not serialize against
 * each other either. A concurrent first call from two threads could
 * race the lazy cache init (best case: redundant work; the file being
 * read is immutable for the process's lifetime, so the two threads
 * would compute identical results, not corrupt ones -- but the racy
 * writes to the cache's own static pointers are still undefined
 * behavior by the letter of the C memory model). Disclosed rather than
 * silently assumed single-threaded: a real fix is a mutex around
 * self_symtab_load()'s init-once check, deferred here the same way
 * every other genuinely separable piece of hardening in this file is.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "plat_dlfcn.h"

/* The real kernel page size -- NOT hardcoded, and deliberately not
 * reused from this tree's existing src/mman/mman.c (`MMAP_PAGE 4096u`)
 * or src/unistd/sysconf.c (`_SC_PAGESIZE`/`getpagesize()`, both
 * hardcoded to 4096 too). Found empirically, not anticipated: this
 * loader's first working version assumed 4096 and got a real,
 * reproducible mmap() EINVAL mapping a genuine .so's second PT_LOAD
 * segment on this exact dev host -- `getconf PAGESIZE` on it reports
 * 16384, not 4096 (aarch64 Linux does not fix the page size at 4K the
 * way x86_64 does; 16K and 64K kernels are real and current, not
 * exotic). ELF segment-mapping correctness depends on matching the
 * kernel's ACTUAL page granularity exactly -- mmap()'s offset argument
 * must be a multiple of the real page size or the call fails outright,
 * so this loader cannot silently inherit the rest of this tree's
 * hardcoded assumption the way a less address-space-sensitive piece of
 * code might get away with. Not fixed in mman.c/sysconf.c themselves:
 * that is a separate, pre-existing bug in code this task did not touch
 * and is out of scope to correct here; this file simply does not
 * depend on it. The true value is read once from /proc/self/auxv's
 * AT_PAGESZ entry -- the same kind of "ask the kernel directly via
 * /proc" technique self_symtab_load() below already uses for a
 * different fact the rest of this tree has no reliable way to expose
 * yet -- and cached for the process's lifetime (it cannot change). */
static unsigned long cached_page_size;
#define AT_PAGESZ 6

static unsigned long real_page_size(void)
{
	int fd;
	unsigned long pair[2];

	if (cached_page_size) return cached_page_size;

	fd = open("/proc/self/auxv", O_RDONLY);
	if (fd >= 0) {
		while (read(fd, pair, sizeof pair) == (ssize_t)sizeof pair && pair[0] != 0) {
			if (pair[0] == AT_PAGESZ) { cached_page_size = pair[1]; break; }
		}
		close(fd);
	}
	if (!cached_page_size) cached_page_size = 4096; /* conservative last resort */
	return cached_page_size;
}

static unsigned long pgdown(unsigned long v) { unsigned long p = real_page_size(); return v & ~(p - 1); }
static unsigned long pgup(unsigned long v) { unsigned long p = real_page_size(); return (v + p - 1) & ~(p - 1); }

/* ---- raw mmap()/munmap()/mprotect(), NOT the public <sys/mman.h> ones
 *
 * Found empirically, disclosed here rather than silently worked around:
 * this loader's address-space layout is exactly "reserve one big span,
 * then MAP_FIXED several independent file-backed sub-mappings inside
 * it, at exact addresses this file itself computes" -- and ntlibc's own
 * public mmap() front door (src/mman/mman.c) is NOT built to support
 * that pattern. Read in full once this broke: mman.c keeps its own
 * reservation-table bookkeeping, one reservation PER mmap() call, and
 * its own banner is explicit that "MAP_FIXED cannot replace part of a
 * file-backed mapping, only its entire current extent" -- a real,
 * deliberate restriction that makes sense for mman.c's own conforming-
 * partial-munmap() design goal, but is incompatible with a loader that
 * needs to punch several independent, exactly-placed file-backed
 * mappings into ONE anonymous reservation it made itself. Calling the
 * public mmap() for this was not a small mismatch: it silently mapped
 * fresh anonymous zero pages instead of the requested file content for
 * the second such sub-mapping in every case tested, with no error
 * returned -- exactly the kind of unobservable-until-it-matters
 * divergence mman.c's own banner warns partial-munmap() bookkeeping
 * can cause elsewhere, just hit here from a different angle.
 *
 * The fix is the same one crt/linux/crt1.c's own bootstrap TLS mmap()
 * already uses, for the same class of reason (see crt1.c's own
 * raw_syscall() banner: "needed here only for the crt's own one-time
 * internal bootstrap allocation... pulling in the mman subsystem this
 * early is exactly the kind of ordering hazard TLS setup itself already
 * had to avoid"): talk to the kernel directly, the same discipline
 * src/mman/linux/plat_mem.c's own backend already uses one layer down.
 * This is not a workaround bolted onto a bug -- it is the same
 * "portable POSIX front door vs. this file's own internal, lower-level
 * need" split every other src/internal/plat_*.h seam in this tree
 * already draws, just drawn here inside a single translation unit
 * instead of across a header boundary, because a full plat_dlfcn-level
 * mmap seam would be its own separate, larger piece of work for no
 * benefit this file needs today. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc");
	return x0;
}
#define SYS_mmap     222
#define SYS_munmap   215
#define SYS_mprotect 226

static int is_sys_error(long ret) { return (unsigned long)ret >= (unsigned long)-4095L; }

static void *raw_mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
	long ret = raw_syscall(SYS_mmap, (long)addr, (long)len, (long)prot, (long)flags, (long)fd, off);
	if (is_sys_error(ret)) { errno = (int)-ret; return MAP_FAILED; }
	return (void *)ret;
}
static int raw_munmap(void *addr, size_t len)
{
	long ret = raw_syscall(SYS_munmap, (long)addr, (long)len, 0, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}
static int raw_mprotect(void *addr, size_t len, int prot)
{
	long ret = raw_syscall(SYS_mprotect, (long)addr, (long)len, (long)prot, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* ---- minimal local ELF64 shapes --------------------------------------
 *
 * This project ships no <elf.h> yet -- the same gap crt/linux/crt1.c's
 * own local `struct elf64_phdr` already lives with, for the same
 * reason (a real one is separate future work, and nothing outside one
 * file needs more than a fragment of the format today). This file
 * needs a much larger fragment than crt1.c's TLS bootstrap does
 * (section headers, the dynamic section, symbol/relocation tables, not
 * just program headers), so it keeps its own, deliberately NOT shared
 * with crt1.c's: merging them would mean either growing crt1.c's
 * minimal set for this file's sake or reaching into this file from
 * crt1.c's very early, allocator-free bootstrap context, and neither
 * is worth the coupling for what would still only be a handful of
 * struct definitions duplicated once. Field widths/order below are
 * ELFCLASS64's, architecture-independent (same caveat crt1.c's own
 * comment already states for its Phdr shape). */
typedef struct {
	unsigned char e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum;
	uint16_t e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct {
	uint32_t sh_name, sh_type;
	uint64_t sh_flags, sh_addr, sh_offset, sh_size;
	uint32_t sh_link, sh_info;
	uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;

typedef struct {
	int64_t d_tag;
	uint64_t d_val;
} Elf64_Dyn;

typedef struct {
	uint32_t st_name;
	unsigned char st_info, st_other;
	uint16_t st_shndx;
	uint64_t st_value, st_size;
} Elf64_Sym;

typedef struct {
	uint64_t r_offset, r_info;
	int64_t r_addend;
} Elf64_Rela;

#define EI_CLASS 4
#define EI_DATA  5
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_AARCH64 183
#define ET_DYN 3

#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_TLS     7

#define PF_X 1
#define PF_W 2
#define PF_R 4

#define SHT_SYMTAB 2

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_PLTREL   20
#define DT_JMPREL   23

#define SHN_UNDEF 0

#define STB_LOCAL(info)  (((info) >> 4) == 0)
#define STV_VISIBILITY(other) ((other) & 0x3)
#define STV_DEFAULT 0
#define STV_PROTECTED 3

#define ELF64_R_SYM(i)  ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)((i) & 0xffffffffu))

#define R_AARCH64_ABS64      257
#define R_AARCH64_GLOB_DAT   1025
#define R_AARCH64_JUMP_SLOT  1026
#define R_AARCH64_RELATIVE   1027

/* ---- sticky error state, single instance for this whole backend ----- */
static char err_buf[256];
static unsigned long err_seq;

static void seterr(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err_buf, sizeof err_buf, fmt, ap);
	va_end(ap);
	err_seq++;
}

const char *__plat_dlerror(void) { return err_seq ? err_buf : NULL; }
unsigned long __plat_dlerror_seq(void) { return err_seq; }

/* ---- dlopen(NULL, ...)'s distinguished handle ------------------------
 *
 * Any address unique to this file and never returned by mmap() works;
 * the address of a static object this translation unit alone owns is
 * the simplest such value, the same trick MAIN_IMAGE_HANDLE's NT-side
 * sibling (src/dlfcn/nt/plat_dlfcn.c) plays with __peb->ImageBaseAddress
 * -- just with no PEB to reuse here, so a dedicated sentinel object
 * instead. */
static const char main_handle_token;
#define MAIN_IMAGE_HANDLE ((void *)&main_handle_token)

/* ---- resolving a name against THIS running binary's own symbol table
 *
 * See this file's "SYMBOL RESOLUTION AGAINST THE STATIC BINARY" banner
 * above for why /proc/self/exe, not a generated table. Lazily loaded
 * once per process and kept resident for its lifetime (never freed --
 * a real loader's own symbol tables are resident for the same reason:
 * they may be needed again at any future dlopen()/dlsym() call). See
 * this file's "THREAD SAFETY" banner for the one disclosed gap here. */
static int self_symtab_ready;      /* 0 = not attempted, 1 = ready, -1 = failed permanently */
static Elf64_Sym *self_syms;
static char *self_strs;
static size_t self_nsyms;

static int self_symtab_load(void)
{
	int fd;
	Elf64_Ehdr eh;
	Elf64_Shdr *shdrs = NULL;
	size_t i;
	int symtab_idx = -1;

	if (self_symtab_ready) return self_symtab_ready == 1 ? 0 : -1;

	fd = open("/proc/self/exe", O_RDONLY);
	if (fd < 0) {
		seterr("dlopen: cannot open /proc/self/exe to resolve symbols against the running binary: %s", strerror(errno));
		goto fail;
	}
	if (pread(fd, &eh, sizeof eh, 0) != (ssize_t)sizeof eh ||
	    eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_ident[EI_DATA] != ELFDATA2LSB ||
	    eh.e_shoff == 0 || eh.e_shnum == 0 || eh.e_shentsize != sizeof(Elf64_Shdr)) {
		seterr("dlopen: /proc/self/exe has no usable ELF section header table");
		goto fail;
	}

	shdrs = malloc((size_t)eh.e_shnum * sizeof *shdrs);
	if (!shdrs) { seterr("dlopen: out of memory reading own section headers"); goto fail; }
	if (pread(fd, shdrs, (size_t)eh.e_shnum * sizeof *shdrs, (off_t)eh.e_shoff) !=
	    (ssize_t)((size_t)eh.e_shnum * sizeof *shdrs)) {
		seterr("dlopen: short read on own section header table");
		goto fail;
	}

	for (i = 0; i < eh.e_shnum; i++) {
		if (shdrs[i].sh_type == SHT_SYMTAB) { symtab_idx = (int)i; break; }
	}
	if (symtab_idx < 0) {
		/* A stripped binary has no .symtab left -- see this file's own
		 * banner on why that is this design's one real, disclosed cost. */
		seterr("dlopen: running binary has no .symtab (stripped?) -- cannot resolve symbols against it");
		goto fail;
	}

	{
		Elf64_Shdr *symtab_sh = &shdrs[symtab_idx];
		Elf64_Shdr *strtab_sh = &shdrs[symtab_sh->sh_link];
		size_t nsyms = symtab_sh->sh_size / sizeof(Elf64_Sym);
		Elf64_Sym *syms = malloc(symtab_sh->sh_size);
		char *strs = malloc(strtab_sh->sh_size);

		if (!syms || !strs) {
			free(syms); free(strs);
			seterr("dlopen: out of memory reading own symbol/string table");
			goto fail;
		}
		if (pread(fd, syms, symtab_sh->sh_size, (off_t)symtab_sh->sh_offset) != (ssize_t)symtab_sh->sh_size ||
		    pread(fd, strs, strtab_sh->sh_size, (off_t)strtab_sh->sh_offset) != (ssize_t)strtab_sh->sh_size) {
			free(syms); free(strs);
			seterr("dlopen: short read on own symbol/string table");
			goto fail;
		}
		self_syms = syms;
		self_strs = strs;
		self_nsyms = nsyms;
	}

	free(shdrs);
	close(fd);
	self_symtab_ready = 1;
	return 0;

fail:
	free(shdrs);
	if (fd >= 0) close(fd);
	self_symtab_ready = -1;
	return -1;
}

/* Resolve `name` against the running binary's own symbol table.
 * Returns the address, or NULL if genuinely not found/unreadable --
 * does not itself set the sticky error on a plain "not found" (only
 * self_symtab_load()'s own I/O failures do), since the two call sites
 * below (dlsym(MAIN_IMAGE_HANDLE, ...) and a dlopen()'d object's own
 * undefined-symbol resolution) want different wording for that case. */
static void *resolve_main_symbol(const char *name)
{
	size_t i;
	if (self_symtab_load() != 0) return NULL;
	for (i = 0; i < self_nsyms; i++) {
		Elf64_Sym *s = &self_syms[i];
		if (s->st_shndx == SHN_UNDEF) continue;
		if (s->st_name == 0) continue;
		if (strcmp(self_strs + s->st_name, name) == 0)
			return (void *)(uintptr_t)s->st_value; /* non-PIE: already absolute */
	}
	return NULL;
}

/* ---- a loaded object -------------------------------------------------
 *
 * See "NAMESPACE ISOLATION" above: one of these is created fresh by
 * every __plat_dlopen() call, never shared or deduplicated. */
struct dlobj {
	void *map_base;   /* the whole reservation, for munmap() */
	size_t map_len;
	unsigned long bias; /* ADDR(v) == bias + v, see __plat_dlopen() */
	Elf64_Sym *dynsym;
	char *dynstr;
	size_t dynsym_count;
};

#define ADDR(obj, v) ((void *)((obj)->bias + (uint64_t)(v)))

static Elf64_Dyn *find_dyn_ptr(Elf64_Dyn *dyn, int64_t tag)
{
	for (; dyn->d_tag != DT_NULL; dyn++)
		if (dyn->d_tag == tag) return dyn;
	return NULL;
}

/* Resolve one relocation's symbol reference, whether it is satisfied
 * by the SAME object's own definition (common: a .so taking its own
 * function's address through the GOT) or has to fall through to the
 * static binary (see resolve_main_symbol() above). Returns 1 with
 * *out filled on success, 0 on an unresolvable undefined symbol
 * (caller sets the sticky error with the symbol name for context). */
static int resolve_symref(struct dlobj *obj, uint32_t symidx, uint64_t *out)
{
	Elf64_Sym *sym;
	const char *name;
	if (symidx == 0 || symidx >= obj->dynsym_count) return 0;
	sym = &obj->dynsym[symidx];
	if (sym->st_shndx != SHN_UNDEF) {
		*out = obj->bias + sym->st_value;
		return 1;
	}
	name = obj->dynstr + sym->st_name;
	{
		void *addr = resolve_main_symbol(name);
		if (!addr) return 0;
		*out = (uint64_t)(uintptr_t)addr;
		return 1;
	}
}

static int apply_one_reloc(struct dlobj *obj, const Elf64_Rela *r,
                            unsigned long lo, unsigned long hi)
{
	uint32_t type = ELF64_R_TYPE(r->r_info);
	uint64_t *loc;

	if (r->r_offset < lo || r->r_offset >= hi) {
		seterr("dlopen: relocation offset 0x%llx outside mapped object",
		       (unsigned long long)r->r_offset);
		return -1;
	}
	loc = ADDR(obj, r->r_offset);

	switch (type) {
	case R_AARCH64_RELATIVE:
		*loc = obj->bias + (uint64_t)r->r_addend;
		return 0;
	case R_AARCH64_ABS64:
	case R_AARCH64_GLOB_DAT:
	case R_AARCH64_JUMP_SLOT: {
		uint64_t sym_addr;
		uint32_t symidx = ELF64_R_SYM(r->r_info);
		if (!resolve_symref(obj, symidx, &sym_addr)) {
			const char *name = (symidx && symidx < obj->dynsym_count) ?
				obj->dynstr + obj->dynsym[symidx].st_name : "?";
			seterr("dlopen: undefined symbol: %s", name);
			return -1;
		}
		*loc = sym_addr + (type == R_AARCH64_ABS64 ? (uint64_t)r->r_addend : 0);
		return 0;
	}
	default:
		/* Includes every TLS relocation type -- see this file's own
		 * "TLS / per-library thread descriptors" banner: refusing a
		 * type we cannot correctly apply is the whole point of this
		 * being a `default:` fail rather than an ignored case. */
		seterr("dlopen: unsupported relocation type %u (offset 0x%llx) -- not yet implemented",
		       type, (unsigned long long)r->r_offset);
		return -1;
	}
}

static int apply_reloc_table(struct dlobj *obj, uint64_t tbl_vaddr, uint64_t tbl_size,
                              unsigned long lo, unsigned long hi)
{
	Elf64_Rela *relas;
	size_t count, i;
	if (!tbl_vaddr || !tbl_size) return 0;
	relas = ADDR(obj, tbl_vaddr);
	count = tbl_size / sizeof(Elf64_Rela);
	for (i = 0; i < count; i++)
		if (apply_one_reloc(obj, &relas[i], lo, hi) != 0) return -1;
	return 0;
}

void *__plat_dlopen(const char *file, int mode)
{
	int fd = -1;
	Elf64_Ehdr eh;
	Elf64_Phdr *phdrs = NULL;
	Elf64_Phdr *pt_dynamic = NULL;
	unsigned long lo = (unsigned long)-1, hi = 0;
	void *map_base = MAP_FAILED;
	size_t map_len = 0;
	struct dlobj *obj = NULL;
	unsigned int i;

	(void)mode; /* every loaded object is already its own isolated
	             * namespace (see this file's own banner) and every
	             * relocation is already resolved eagerly -- RTLD_NOW/
	             * LAZY/GLOBAL/LOCAL have nothing left to select
	             * between, the same way they are moot on the NT
	             * backend for its own, different reasons. */

	if (!file) return MAIN_IMAGE_HANDLE;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		seterr("dlopen: %s: %s", file, strerror(errno));
		return NULL;
	}

	if (pread(fd, &eh, sizeof eh, 0) != (ssize_t)sizeof eh ||
	    memcmp(eh.e_ident, "\x7f""ELF", 4) != 0 ||
	    eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_ident[EI_DATA] != ELFDATA2LSB) {
		seterr("dlopen: %s: not a recognizable ELF64 file", file);
		errno = ENOEXEC;
		goto fail;
	}
	if (eh.e_machine != EM_AARCH64) {
		seterr("dlopen: %s: wrong machine type (this build only supports EM_AARCH64=%d, see this file's own banner)", file, EM_AARCH64);
		errno = ENOEXEC;
		goto fail;
	}
	if (eh.e_type != ET_DYN) {
		seterr("dlopen: %s: not ET_DYN (only shared objects are supported)", file);
		errno = ENOEXEC;
		goto fail;
	}
	if (eh.e_phnum == 0 || eh.e_phnum > 256 || eh.e_phentsize != sizeof(Elf64_Phdr)) {
		seterr("dlopen: %s: unusable program header table", file);
		errno = ENOEXEC;
		goto fail;
	}

	phdrs = malloc((size_t)eh.e_phnum * sizeof *phdrs);
	if (!phdrs) { seterr("dlopen: out of memory"); errno = ENOMEM; goto fail; }
	if (pread(fd, phdrs, (size_t)eh.e_phnum * sizeof *phdrs, (off_t)eh.e_phoff) !=
	    (ssize_t)((size_t)eh.e_phnum * sizeof *phdrs)) {
		seterr("dlopen: %s: short read on program header table", file);
		goto fail;
	}

	for (i = 0; i < eh.e_phnum; i++) {
		Elf64_Phdr *ph = &phdrs[i];
		if (ph->p_type == PT_TLS) {
			/* See this file's "TLS / per-library thread descriptors"
			 * banner: refused cleanly, before anything is mapped,
			 * rather than loaded with no working TLS story. */
			seterr("dlopen: %s: has a PT_TLS segment (__thread variables) -- per-object TLS is designed but not yet implemented, see plat_dlfcn.c's own banner", file);
			goto fail;
		}
		if (ph->p_type == PT_DYNAMIC) pt_dynamic = ph;
		if (ph->p_type != PT_LOAD) continue;
		if (ph->p_vaddr < lo) lo = ph->p_vaddr;
		if (ph->p_vaddr + ph->p_memsz > hi) hi = ph->p_vaddr + ph->p_memsz;
	}
	if (hi == 0 || !pt_dynamic) {
		seterr("dlopen: %s: no PT_LOAD/PT_DYNAMIC segments", file);
		goto fail;
	}
	lo = pgdown(lo);
	hi = pgup(hi);
	map_len = hi - lo;

	map_base = raw_mmap(NULL, map_len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map_base == MAP_FAILED) {
		seterr("dlopen: %s: cannot reserve %zu bytes of address space: %s", file, map_len, strerror(errno));
		goto fail;
	}

	obj = malloc(sizeof *obj);
	if (!obj) { seterr("dlopen: out of memory"); goto fail; }
	obj->map_base = map_base;
	obj->map_len = map_len;
	obj->bias = (unsigned long)map_base - lo;

	/* Map every PT_LOAD segment. Mapped read-write initially regardless
	 * of the segment's own p_flags -- relocations below may need to
	 * write into it -- and narrowed down to its real declared
	 * protection in a second pass once every relocation (which may
	 * target any segment, not just the one currently being mapped) has
	 * been applied. See this file's own PT_GNU_RELRO note: this is
	 * NOT a relro-hardening pass, just restoring the object's own
	 * declared (non-relro) permissions. */
	for (i = 0; i < eh.e_phnum; i++) {
		Elf64_Phdr *ph = &phdrs[i];
		unsigned long vstart, filelen, memend, alloclen;
		void *segbase;
		if (ph->p_type != PT_LOAD) continue;

		vstart = pgdown(ph->p_vaddr);
		filelen = pgup((ph->p_vaddr - vstart) + ph->p_filesz);
		memend = pgup((ph->p_vaddr - vstart) + ph->p_memsz);
		segbase = (void *)(obj->bias + vstart);

		if (ph->p_filesz > 0) {
			void *r = raw_mmap(segbase, filelen, PROT_READ | PROT_WRITE,
			               MAP_PRIVATE | MAP_FIXED, fd, (long)pgdown(ph->p_offset));
			if (r == MAP_FAILED) {
				seterr("dlopen: %s: cannot map PT_LOAD segment %u: %s", file, i, strerror(errno));
				goto fail;
			}
			/* Zero the tail of the last file-backed page past p_filesz
			 * -- the ELF loading rule every real loader implements
			 * (System V ABI: "the bytes from the end of the file image
			 * to the end of the memory image are... initialized to
			 * zero"). The kernel already zero-fills the tail of a
			 * mapped page past the underlying file's own extent for a
			 * MAP_PRIVATE file mapping, but that guarantee stops at
			 * the file's real length, not at p_filesz specifically --
			 * writing it explicitly costs nothing and does not depend
			 * on that distinction lining up. */
			memset((char *)segbase + (ph->p_vaddr - vstart) + ph->p_filesz, 0,
			       filelen - ((ph->p_vaddr - vstart) + ph->p_filesz));
		}
		alloclen = memend > filelen ? memend : filelen;
		if (alloclen > filelen) {
			void *r = raw_mmap((char *)segbase + filelen, alloclen - filelen, PROT_READ | PROT_WRITE,
			               MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
			if (r == MAP_FAILED) {
				seterr("dlopen: %s: cannot map bss tail of segment %u: %s", file, i, strerror(errno));
				goto fail;
			}
		}
	}

	{
		Elf64_Dyn *dyn = ADDR(obj, pt_dynamic->p_vaddr);
		Elf64_Dyn *d_hash = find_dyn_ptr(dyn, DT_HASH);
		Elf64_Dyn *d_symtab = find_dyn_ptr(dyn, DT_SYMTAB);
		Elf64_Dyn *d_strtab = find_dyn_ptr(dyn, DT_STRTAB);
		Elf64_Dyn *d_syment = find_dyn_ptr(dyn, DT_SYMENT);
		Elf64_Dyn *d_rela = find_dyn_ptr(dyn, DT_RELA);
		Elf64_Dyn *d_relasz = find_dyn_ptr(dyn, DT_RELASZ);
		Elf64_Dyn *d_relaent = find_dyn_ptr(dyn, DT_RELAENT);
		Elf64_Dyn *d_jmprel = find_dyn_ptr(dyn, DT_JMPREL);
		Elf64_Dyn *d_pltrelsz = find_dyn_ptr(dyn, DT_PLTRELSZ);
		Elf64_Dyn *d_pltrel = find_dyn_ptr(dyn, DT_PLTREL);

		if (!d_hash || !d_symtab || !d_strtab || !d_syment) {
			seterr("dlopen: %s: no DT_HASH/DT_SYMTAB/DT_STRTAB -- DT_GNU_HASH-only objects are not supported yet (see this file's own banner); relink with -Wl,--hash-style=sysv or =both", file);
			goto fail;
		}
		if (d_syment->d_val != sizeof(Elf64_Sym)) {
			seterr("dlopen: %s: unexpected DT_SYMENT", file);
			goto fail;
		}
		if (d_relaent && d_relaent->d_val != sizeof(Elf64_Rela)) {
			seterr("dlopen: %s: unexpected DT_RELAENT", file);
			goto fail;
		}
		if (d_pltrel && d_pltrel->d_val != DT_RELA) {
			seterr("dlopen: %s: DT_PLTREL is not DT_RELA (REL-style PLT relocations are not supported)", file);
			goto fail;
		}

		obj->dynsym = ADDR(obj, d_symtab->d_val);
		obj->dynstr = ADDR(obj, d_strtab->d_val);
		/* DT_HASH's header is { nbucket; nchain; ... } -- nchain equals
		 * the symbol table's own entry count by the SysV ELF hash
		 * table's own specification, giving an exact count with no
		 * GNU-hash bucket walk needed. */
		obj->dynsym_count = ((uint32_t *)ADDR(obj, d_hash->d_val))[1];

		if (apply_reloc_table(obj, d_rela ? d_rela->d_val : 0, d_relasz ? d_relasz->d_val : 0, lo, hi) != 0)
			goto fail;
		if (apply_reloc_table(obj, d_jmprel ? d_jmprel->d_val : 0, d_pltrelsz ? d_pltrelsz->d_val : 0, lo, hi) != 0)
			goto fail;
	}

	/* Second pass: narrow each PT_LOAD segment down to its own declared
	 * protection now that every relocation, wherever it targeted, has
	 * been applied. */
	for (i = 0; i < eh.e_phnum; i++) {
		Elf64_Phdr *ph = &phdrs[i];
		unsigned long vstart, memend;
		int prot;
		if (ph->p_type != PT_LOAD) continue;
		vstart = pgdown(ph->p_vaddr);
		memend = pgup((ph->p_vaddr - vstart) + ph->p_memsz);
		prot = (ph->p_flags & PF_R ? PROT_READ : 0) |
		       (ph->p_flags & PF_W ? PROT_WRITE : 0) |
		       (ph->p_flags & PF_X ? PROT_EXEC : 0);
		if (prot & PROT_WRITE) continue; /* already mapped read-write */
		if (raw_mprotect((void *)(obj->bias + vstart), memend, prot) != 0) {
			seterr("dlopen: %s: cannot finalize protection on segment %u: %s", file, i, strerror(errno));
			goto fail;
		}
	}

	free(phdrs);
	close(fd);
	return obj;

fail:
	free(phdrs);
	if (fd >= 0) close(fd);
	if (map_base != MAP_FAILED) raw_munmap(map_base, map_len);
	free(obj);
	return NULL;
}

void *__plat_dlsym(void *__restrict handle, const char *__restrict name)
{
	struct dlobj *obj = handle;
	size_t i;

	if (handle == MAIN_IMAGE_HANDLE) {
		void *addr = resolve_main_symbol(name);
		if (!addr) seterr("dlsym: symbol not found: %s", name);
		return addr;
	}

	/* Index 0 of .dynsym is always the reserved all-zero null symbol
	 * (ELF spec) -- skip it, same as apply_reloc_table()'s own
	 * `symidx == 0` rejection above. Only STB_GLOBAL/STB_WEAK, default/
	 * protected-visibility, defined symbols count as "this object
	 * exports `name`" for dlsym.html's purposes -- a hidden/internal or
	 * local symbol is not something an outside caller should be able
	 * to reach through dlsym() even though it is present in .dynsym for
	 * this object's OWN relocations (resolve_symref() above) to use. */
	for (i = 1; i < obj->dynsym_count; i++) {
		Elf64_Sym *s = &obj->dynsym[i];
		if (s->st_shndx == SHN_UNDEF) continue;
		if (STB_LOCAL(s->st_info)) continue;
		if (STV_VISIBILITY(s->st_other) != STV_DEFAULT &&
		    STV_VISIBILITY(s->st_other) != STV_PROTECTED) continue;
		if (strcmp(obj->dynstr + s->st_name, name) == 0)
			return ADDR(obj, s->st_value);
	}
	seterr("dlsym: symbol not found: %s", name);
	return NULL;
}

int __plat_dlclose(void *handle)
{
	struct dlobj *obj = handle;
	if (handle == MAIN_IMAGE_HANDLE) return 0; /* see NT backend's identical rationale */
	raw_munmap(obj->map_base, obj->map_len);
	free(obj);
	return 0;
}
