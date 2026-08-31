/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __delayLoadHelper2(): the real, linker-driven delay-load helper --
 * as opposed to ntlibc_delayLoadHelper2() in src/internal/delayload.c,
 * which backs the hand-authored NTLIBC_DELAY_STUB macros for a tcc that
 * cannot emit delay-import thunks at all. A tcc built with
 * -Wl,--delay-all *can* (see the tinycc fork this targets): its linker
 * turns every ordinary `extern` call to an imported function into a
 * call through a generated thunk, and every such thunk's first call
 * goes through exactly one routine, looked up by this exact name --
 * __delayLoadHelper2(descriptor, &iat_slot) -- with no ntlibc-specific
 * declaration required at the call site. That is what makes an
 * *unmodified* program (plain extern, ordinary call) get $ORIGIN
 * resolution: nothing in this file is reachable from source code that
 * does not already exist in a normal program.
 *
 * ---- Why this lives under crt/, not src/internal/ -----------------------
 *
 * Every other piece of the delay-load facility (rpath.c, pe.c,
 * delayload.c) is an ordinary archive member of lib/libc.a, pulled in
 * only when something actually references it -- the "a program that
 * never delay-loads anything never pays for it" property rpath.h
 * documents. __delayLoadHelper2 cannot work that way: tinycc's
 * pe_build_delay_imports() (tccpe.c) only invents the undefined
 * reference to "__delayLoadHelper2" while *writing* the PE image, well
 * after the point where tcc resolves undefined symbols against archive
 * members -- so a tcc link against `-lc` alone never even looks inside
 * libc.a for a symbol by that name, and reports it unresolved even
 * though delayload2.o (built from this file) sits right there in the
 * archive with exactly that definition. (Confirmed empirically against
 * the -Wl,--delay-all tinycc fork this targets: `-Llib -lc -lntdll`
 * alone fails with "unresolved reference to '__delayLoadHelper2'";
 * adding this file's .o directly to the link command succeeds.)
 *
 * The fix is the same shape as crt1.o itself: this file is built to
 * lib/delayload2.o (via the Makefile's existing crt/ handling -- see
 * CRT_OBJS/CRT_LIBS) and given to the linker as a plain object file
 * alongside crt1.o, not folded into libc.a, so it is never subject to
 * that archive-lookup gap. A program using -Wl,--delay-all links it in
 * explicitly, the same way it already links crt1.o explicitly (see
 * test/delayall.c and the Makefile's obj/test/delayall.exe rule); the
 * ntlibc-tcc wrapper (tools/ntlibc-tcc.in) also adds it automatically
 * whenever it sees --delay-all on its command line, so a program built
 * through the wrapper needs nothing extra at all. A normal (non-
 * --delay-all) program never references __delayLoadHelper2, so linking
 * this one extra object costs it nothing beyond what a program that
 * links crt1.o already pays.
 *
 * ---- The descriptor is RVA-based, not a native pointer -----------------
 *
 * The linker emits a real IMAGE_DELAYLOAD_DESCRIPTOR (PE/COFF spec 4.3)
 * with Attributes.RvaBased = 1: DllNameRVA, ModuleHandleRVA,
 * ImportAddressTableRVA and ImportNameTableRVA are all offsets from the
 * image base, not pointers -- see tinycc's pe_build_delay_imports()
 * (tccpe.c), which is authoritative here since it is the linker that
 * writes these images. This differs from delayload.h's
 * ntlibc_delay_descr_t, whose fields are plain native pointers by
 * ntlibc's own private convention (see that header's comment) -- the
 * two formats are unrelated in memory layout even though they play the
 * same role. __peb->ImageBaseAddress (set by crt1.c from the PEB it
 * reads out of the TEB -- see crt1.c's own comment on why it is read
 * from there, and not from an entry-point argument or an
 * RtlGetCurrentPeb() call) is the base every RVA here is relative to.
 *
 * The one field this file does *not* treat as an RVA is what an IAT
 * slot *contains*: even in the RVA-based descriptor format, the delay
 * import address table itself holds real (absolute) pointers -- initially
 * the thunk's own address (tccpe.c's pe_emit_delay_thunk points each
 * IAT slot at its own thunk via an ordinary base-relocated absolute
 * pointer, so the very first call has *something* jumpable to loop back
 * here), and the resolved function's address once this helper patches
 * it. Distinguishing "still the thunk" from "already resolved" is done
 * by range-checking the slot's current value against the target
 * module's own [base, base+SizeOfImage) (ntlibc_pe_dll_range(),
 * src/internal/pe.c) rather than by remembering the thunk's address
 * separately: a resolved function's address always lands inside its
 * owning DLL's mapped image, and the thunk itself never does (it lives
 * in this executable's own .text). This is also the fast path that
 * makes a second call through an already-patched slot cheap: the
 * generated tail-merge stub (tccpe.c's pe_emit_delay_tailmerge) calls
 * __delayLoadHelper2() unconditionally on *every* call, with no
 * resolved-or-not check of its own before doing so -- so this range
 * check is the only thing standing between "second call" and "walks
 * the export table again".
 *
 * ---- Why ntdll needs its own bootstrap, and how it gets one ------------
 *
 * -Wl,--delay-all means *all*: ntdll.dll is a real entry in this
 * image's own import directory (confirmed with objdump -p on an ntlibc
 * binary -- "DLL Name: ntdll.dll" with named imports), so calls this
 * library itself makes into ntdll -- LdrLoadDll, RtlAllocateHeap,
 * NtWriteFile, all of it -- are just as delay-loaded as anything a
 * program calls directly. Naively resolving *any* delay import by
 * calling LdrLoadDll()/LdrGetProcedureAddress() (as
 * ntlibc_rpath_load()/_sym() in rpath.c do) would, the first time
 * either of those two symbols itself needs resolving, call the very
 * import it is trying to resolve -- through its own not-yet-patched
 * slot, straight back into this function, forever.
 *
 * The fix used throughout this file: symbol resolution never calls
 * LdrGetProcedureAddress at all. ntlibc_pe_find_export()
 * (src/internal/pe.c) hand-parses a module's export directory directly
 * from its mapped image, and that works unconditionally for *any*
 * already-mapped module, ntdll included -- ntdll is mapped into every
 * NT process by the kernel before the entry point ever runs, so it
 * needs no LdrLoadDll call to find. find_mapped_module() below checks
 * the PEB's loader module list (__peb->Ldr->InMemoryOrderModuleList,
 * PEB_LDR_DATA/LDR_DATA_TABLE_ENTRY -- both already in src/internal/
 * nt.h, offsets cross-checked against Wine's include/winternl.h's
 * _PEB_LDR_DATA and _LDR_DATA_TABLE_ENTRY) for a case-insensitive
 * base-name match before ever considering an actual load, for exactly
 * this reason -- "any already-mapped module", not just ntdll, per the
 * design note in pe.h. Only a module *not* found there falls through to
 * ntlibc_rpath_load(), which does call LdrLoadDll -- safe by this
 * point because resolving *that* call's own ntdll.dll import never
 * reaches ntlibc_rpath_load() in the first place (ntdll is always
 * found by the PEB walk), so there is no cycle to break.
 *
 * NT-only, same guard and same reason as rpath.c/delayload.c/pe.c.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if !defined(_WIN32) && (defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer))
#error "delayload2.c is NT-only; see the comment above and src/internal/rpath.c"
#endif
#include <string.h>
#include "libc.h"
#include "pe.h"
#include "rtlib.h"
#include "ntlibc/rpath.h"

/* Real (linker-built) delay-load descriptor -- PE/COFF spec 4.3,
 * cross-checked against tinycc's own pe_build_delay_imports() (tccpe.c),
 * which is the linker that emits it. Every *RVA field here is an offset
 * from the image base; see the file header comment for the one field
 * (an IAT slot's contents) that is not. */
typedef struct _IMAGE_DELAYLOAD_DESCRIPTOR { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling follows the NT ABI
	ULONG Attributes;
	ULONG DllNameRVA;
	ULONG ModuleHandleRVA;
	ULONG ImportAddressTableRVA;
	ULONG ImportNameTableRVA;
	ULONG BoundImportAddressTableRVA;
	ULONG UnloadInformationTableRVA;
	ULONG TimeDateStamp;
} IMAGE_DELAYLOAD_DESCRIPTOR;

/* Case-insensitive (ASCII range only -- every DLL name this ever
 * compares is plain ASCII) compare of a plain (non-allocated) ASCII
 * C string against a UTF-16 buffer of known length `wn`, with no
 * conversion or allocation of any kind: deliberately, since this runs
 * inside the delay-load resolution path itself (see below). */
/* ascii is required: the post-loop `return ascii[wn] == 0;` reads
 * ascii[wn] unconditionally on every path, including wn == 0 (the
 * loop body itself never runs then, but the final statement always
 * does). This function's one real caller, find_mapped_module()
 * below, always forwards its own now-required dllname (see that
 * function's own comment).
 *
 * w is required too, despite w[i] only ever being read inside the
 * loop, guarded by `i < wn`: this function's one real call site
 * always passes `e->BaseDllName.Buffer, e->BaseDllName.Length /
 * sizeof(WCHAR)` for (w, wn) -- BaseDllName is LDR_DATA_TABLE_ENTRY's
 * own UNICODE_STRING (populated by the NT loader for every loaded
 * module at LdrpAllocateDataTableEntry time), and a valid
 * UNICODE_STRING's own invariant (NT itself, not this tree) is that
 * Buffer is never NULL for a populated entry -- the same "genuine
 * invariant established by the real caller, not derivable from a
 * bound check alone" reasoning crt/crt1.c's own split_cmdline
 * already relies on for its own p. */
static int name_eq_ci(const char *ascii, const WCHAR *w, size_t wn)
    __attribute__((nonnull(1, 2)));
static int name_eq_ci(const char *ascii, const WCHAR *w, size_t wn)
{
	size_t i;
	for (i = 0; i < wn; i++) {
		unsigned char ca = (unsigned char)ascii[i];
		WCHAR cb = w[i];
		if (!ca) return 0; /* ascii is shorter than w */
		if (ca >= 'a' && ca <= 'z') ca = (unsigned char)(ca - 0x20);
		if (cb >= 'a' && cb <= 'z') cb = (WCHAR)(cb - 0x20);
		if ((WCHAR)ca != cb) return 0;
	}
	return ascii[wn] == 0; /* same length too */
}

/* Finds `dllname` (a plain ASCII/UTF-8 name, e.g. "ntdll.dll") already
 * mapped into this process by walking the PEB's loader module list.
 *
 * Deliberately does *no* heap allocation (name_eq_ci compares directly
 * against dllname, unconverted, rather than going through
 * __utf8_to_utf16()+__malloc() as every other caller of that pattern
 * does): __malloc() ultimately calls RtlAllocateHeap, itself an ntdll
 * import and therefore itself delay-loaded under --delay-all. The very
 * first delay-load resolution of *any* symbol in the whole program is,
 * in general, RtlAllocateHeap's own -- crt1.c's __libc_start_main()
 * allocates before main ever runs (build_environ(), split_cmdline()) --
 * so if resolving it (or resolving anything else, this early) needed to
 * allocate first, that allocation would itself need RtlAllocateHeap
 * resolved, which needs to allocate, forever: confirmed the hard way,
 * as a real stack overflow under Wine (virtual_setup_exception), not a
 * hang -- each recursion pushes another __delayLoadHelper2 frame until
 * the thread's stack is exhausted. Comparing without allocating
 * breaks that cycle: resolving ntdll's own exports (this function's
 * entire job) never itself needs anything resolved. */
/* e->BaseDllName below is a disclosed, deliberately unmarked residual:
 * e is not a parameter of this function at all (it is CONTAINING_
 * RECORD-computed from `cur`, a pointer-arithmetic offset off a live
 * circular list walk), so there is no signature for `nonnull` to
 * describe this on -- the same "struct/local-derived-pointer, not a
 * parameter" class this tree's own d24fe86 commit already established
 * for wait.c's discover_self_stops() and friends. Verified sound by
 * hand regardless: PEB_LDR_DATA's own InMemoryOrderModuleList is a
 * genuinely circular, always-populated list for any live NT process
 * (ntdll.dll and the executable's own module are always entries), an
 * OS loader invariant, not something any guard in this function's own
 * body could add. */
static void *find_mapped_module(const char *dllname)
{
	PLIST_ENTRY head, cur;

	if (!__peb || !__peb->Ldr) return 0;

	head = &__peb->Ldr->InMemoryOrderModuleList;
	for (cur = head->Flink; cur != head; cur = cur->Flink) {
		/* InMemoryOrderLinks is LDR_DATA_TABLE_ENTRY's *second* LIST_ENTRY
		 * member (nt.h): recover the entry by subtracting that member's
		 * offset, the same pattern CONTAINING_RECORD expands to. */
		LDR_DATA_TABLE_ENTRY *e = (LDR_DATA_TABLE_ENTRY *)
			((unsigned char *)cur - offsetof(LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks));
		if (name_eq_ci(dllname, e->BaseDllName.Buffer, e->BaseDllName.Length / sizeof(WCHAR)))
			return e->DllBase;
	}
	return 0;
}

void *__delayLoadHelper2(void *vdescr, void **piat)
{
	IMAGE_DELAYLOAD_DESCRIPTOR *descr = (IMAGE_DELAYLOAD_DESCRIPTOR *)vdescr;
	unsigned char *base;
	void **modhandle;
	void **iat;
	ULONG *nametable;
	const char *dllname;
	unsigned long index;
	const char *name;
	void *dll;
	void *proc;
	void *rstart, *rend;

	/* __peb is set by crt1.c's __libc_start_main() before main ever
	 * runs (and, per this file's header comment, before any ntdll call
	 * that could reach a delay-load stub at all) -- so this is not a
	 * real runtime possibility, only a defensive, fail-loud guard
	 * against computing every RVA below off a null base if it somehow
	 * were. */
	if (!__peb) ntlibc_rpath_fail("<ntlibc>", "__peb not initialized");

	base = (unsigned char *)__peb->ImageBaseAddress;
	modhandle = (void **)(base + descr->ModuleHandleRVA);
	iat = (void **)(base + descr->ImportAddressTableRVA);
	nametable = (ULONG *)(base + descr->ImportNameTableRVA);
	dllname = (const char *)(base + descr->DllNameRVA);

	index = (unsigned long)(piat - iat);
	/* Each name-table entry is an RVA to a 2-byte "hint" (unused, always
	 * 0 here -- tccpe.c never fills it in) followed by the NUL-terminated
	 * import name; skip the hint the same way the real loader does.
	 *
	 * nametable[index] below is a disclosed, deliberately unmarked
	 * residual, surfaced only after vdescr's own nonnull mark let this
	 * checker explore further into this function than before (the same
	 * "deeper exploration unlocked" effect prior sweeps in this tree
	 * already measured, not a regression): nametable is `base +
	 * descr->ImportNameTableRVA`, a local computed by pointer
	 * arithmetic, not a parameter of this function -- the same
	 * "struct/local-derived pointer, not a parameter" class this
	 * file's own find_mapped_module() comment already established.
	 * Verified sound by hand regardless: base is always __peb-derived
	 * and descr is now required (see above), and a real,
	 * linker-emitted IMAGE_DELAYLOAD_DESCRIPTOR's own
	 * ImportNameTableRVA always lands inside the same mapped image
	 * (PE/COFF spec 4.3), never NULL. */
	name = (const char *)(base + nametable[index]) + sizeof(USHORT);

	dll = *modhandle;

	/* Fast path: already resolved by an earlier call through this same
	 * slot. See the file header comment for why a range check against
	 * the owning DLL's mapped image, not a sentinel compare, is what
	 * distinguishes this from "still holds the thunk's own address". */
	if (dll && ntlibc_pe_dll_range(dll, &rstart, &rend) && *piat >= rstart && *piat < rend)
		return *piat;

	if (!dll) {
		dll = find_mapped_module(dllname);
		if (!dll) dll = ntlibc_rpath_load(dllname);
		if (!dll) ntlibc_rpath_fail(dllname, "<module>");
		*modhandle = dll;
	}

	proc = ntlibc_pe_find_export(dll, name);
	if (!proc) ntlibc_rpath_fail(dllname, name);

	*piat = proc;
	return proc;
}

// NOLINTEND(misc-include-cleaner)
