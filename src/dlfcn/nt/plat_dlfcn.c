/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * src/internal/plat_dlfcn.h's NT backend -- what used to be the whole
 * of src/dlfcn/dlfcn.c before that file was split into a thin,
 * platform-agnostic front door plus this backend (the same "platform-
 * abstraction migration" src/stat/chmod.c's own history already went
 * through: see src/internal/plat_stat.h's banner for the precedent).
 * This is a relocation, not a rewrite -- every function below is the
 * same logic that used to live directly in dlopen()/dlsym()/dlclose()/
 * dlerror(), renamed to the __plat_dl*() names plat_dlfcn.h declares.
 * Built directly on ntdll's LdrLoadDll()/LdrGetProcedureAddress()/
 * LdrUnloadDll() through include/ntlibc/rpath.h's existing wrappers
 * (ntlibc_rpath_load()/_sym()/_unload()/_error()/_error_seq(),
 * implemented in src/internal/rpath.c). Nothing in this file talks to
 * ntdll directly; every NT-specific decision -- what counts as an
 * absolute path, where $ORIGIN is, the sticky-error bookkeeping -- is
 * already made there.
 *
 * ---- mode flags: RTLD_NOW/RTLD_LAZY/RTLD_GLOBAL/RTLD_LOCAL ------------
 *
 * `mode` is accepted but never inspected. Three of the four bits name a
 * choice NT's loader does not offer:
 *
 *   RTLD_NOW / RTLD_LAZY: LdrLoadDll() resolves every relocation before
 *   it returns, unconditionally -- there is no ntdll mechanism to defer
 *   an ordinary (non-delay-load) import's binding to first use the way
 *   RTLD_LAZY describes. So RTLD_NOW is trivially satisfied by every
 *   dlopen() call, and RTLD_LAZY can only ever be honoured as an alias
 *   for it: no symbol RTLD_LAZY promises to resolve "by first use" is
 *   ever left unresolved, it is simply resolved earlier than the mode
 *   asked for, which POSIX's own wording allows ("ranging from the
 *   time of the dlopen() call until the first reference").
 *
 *   RTLD_GLOBAL: also unconditionally what LdrLoadDll() does -- every
 *   module's exports are visible to LdrGetProcedureAddress() the
 *   moment it is mapped, process-wide, with no per-handle opt-in.
 *
 *   RTLD_LOCAL: genuinely N/A, not merely unimplemented. dlsym()
 *   (below) always resolves through LdrGetProcedureAddress() against
 *   one specific handle's own export table, so RTLD_LOCAL's *direct*
 *   promise -- "this dlsym(handle, ...) call only sees handle's own
 *   exports" -- already holds by construction, the same way it would
 *   for RTLD_GLOBAL. What RTLD_LOCAL actually asks for beyond that is
 *   negative: that this module's symbols must NOT be used to satisfy a
 *   *different*, later-loaded module's own unresolved imports. NT's
 *   loader has no primitive that narrows a module's exports back out
 *   of another module's import resolution at load time -- every DLL's
 *   export directory is reachable the instant LdrLoadDll() maps it,
 *   with no scoping knob (this is exactly why delay-load thunk
 *   resolution, include/ntlibc/delayload.h, has to bind one specific
 *   import slot to one specific resolved address rather than asking
 *   the loader to search narrowly). So RTLD_LOCAL cannot be built
 *   without reimplementing PE import resolution wholesale the way
 *   rpath.c/pe.c narrowly do for the ntdll delay-load bootstrap case
 *   only -- out of scope here. The bit is still accepted (so the
 *   common `RTLD_NOW | RTLD_LOCAL` combination still compiles and
 *   loads) but never isolates anything.
 *
 * ---- bare (no-slash) `file`: $ORIGIN search, not the plain NT one ----
 *
 * dlopen.html DESCRIPTION is explicit that this is an implementation's
 * call to make: "If file contains a slash character, the file argument
 * is used as the pathname for the file. Otherwise, file is used in an
 * implementation-defined manner to yield a pathname." A `file` with a
 * slash (or backslash, or drive letter -- rpath.c's has_path_component/
 * is_absolute) is passed straight to ntlibc_rpath_load(), which uses it
 * as-is if absolute or resolves it against the image directory
 * ($ORIGIN) if relative -- never against the current working
 * directory, per rpath.h's own documented threat model.
 *
 * A bare `file` (no path component at all) is what needs the real
 * decision: ntlibc_rpath_load() routes it through __rpath, the
 * program's own opt-in search list, resolved the same $ORIGIN-relative
 * way -- NOT through LdrLoadDll's own ordinary unqualified-name search,
 * which is deliberately never invoked anywhere in rpath.c. The
 * consequence, stated once here since it is dlopen()'s doorway into
 * that decision: a program that calls dlopen("foo.dll", ...) with a
 * bare name and never defines __rpath gets a clean failure (dlerror()
 * reports "DLL not found"), not a load from the current working
 * directory, PATH, or the system directories -- unlike a typical
 * dlopen() elsewhere, which usually does fall back to platform search
 * rules for a bare name. That is intentional, not a missing feature:
 * dlopen() taking a bare name and searching an ambient, often
 * attacker-influenceable path list (starting with the CWD) is a classic
 * DLL-hijacking vector, and rpath.h's own header comment explains at
 * length why this codebase refuses that fallback for its delay-load
 * mechanism already. dlopen() sitting on the same primitive inherits
 * the same protection for free, at the cost of needing __rpath (or a
 * path-qualified `file`) to load anything by a short name at all. A
 * caller that specifically wants the ordinary system search can still
 * get it by adding an absolute directory to __rpath, or by simply
 * passing dlopen() a fully-qualified path in the first place.
 *
 * ---- reference counting: LdrLoadDll()/LdrUnloadDll() already do it --
 *
 * dlopen.html DESCRIPTION: "Only a single copy of an executable object
 * file shall be brought into the address space, even if dlopen() is
 * invoked multiple times", and implementations may refcount to do it.
 * dlclose.html DESCRIPTION: mirrors this with "may unload ... when
 * ... the reference count drops to 0". Confirmed against Wine's
 * dlls/ntdll/loader.c (the reference implementation of the ntdll ABI
 * this project targets, not a POSIX libc): LdrLoadDll's own callees
 * increment a per-module WINE_MODREF.ldr.LoadCount every time an
 * already-mapped module is loaded again rather than mapping a second
 * copy (e.g. the `if (LoadCount != -1) LoadCount++` sites reached from
 * both the implicit-import walk and LdrLoadDll's own explicit-load
 * path), and LdrUnloadDll()/MODULE_DecRefCount() decrements that same
 * field, only actually unloading -- walking dependencies and freeing
 * the WINE_MODREF -- once it reaches zero (a LoadCount of -1, used for
 * modules pinned at process start such as ntdll itself, short-circuits
 * to a same-value success instead of ever going negative). So this
 * layer tracks no reference count of its own: ntlibc_rpath_load() and
 * ntlibc_rpath_unload() (thin wrappers over LdrLoadDll()/
 * LdrUnloadDll()) already give dlopen()/dlclose() this contract for
 * free, including returning the identical handle from two dlopen()
 * calls on the same fully-qualified path (test_dlclose_refcounts,
 * test/posix-dl.c, exercises exactly this).
 *
 * ---- dlopen(NULL, ...): the process-global symbol set ----------------
 *
 * dlopen.html DESCRIPTION: "If file is a null pointer, dlopen() shall
 * return a global symbol table handle for the currently running
 * process image." __peb->ImageBaseAddress (src/internal/nt.h's PEB,
 * populated by the OS before crt1.c ever runs) is exactly that: the
 * base address of the main executable's own already-mapped PE image,
 * and is used as the distinguished global handle.  dlsym() recognises
 * it and walks PEB_LDR_DATA's load-order list, asking each module for
 * the symbol until one succeeds.  That includes the original image,
 * startup-loaded DLLs, and later LdrLoadDll modules in loader order.
 *
 * An ordinary tcc-linked EXE still has no export directory, so symbols
 * defined only by the main program require an explicit linker export.
 * That does not make the global set empty: startup and loaded DLLs are
 * searched after the executable.
 *
 * ---- native ASan/UBSan build -------------------------------------------
 *
 * NT-only for the same reason and by the same guard as
 * src/internal/rpath.c/pe.c, which this file is a thin layer over:
 * every function here ultimately calls an ntlibc_rpath_*() entry point
 * that does not exist in a native build (rpath.c refuses to compile
 * there itself), and __peb->ImageBaseAddress is only ever populated by
 * the NT loader. See rpath.c's own comment for the full rationale,
 * including why this has to be a compile-time #error rather than
 * simply an unimplemented function: tools/asan-build.sh's native run
 * links every compiled source file's object into every test
 * unconditionally, so a stray undefined ntlibc_rpath_*() reference here
 * would break every *other* test's native link too, not just this
 * file's own (confirmed: omitting this guard produced exactly that --
 * every test in obj/asan/unlinkable.txt failing on undefined
 * references to ntlibc_rpath_load/_sym/_unload/_error/_error_seq).
 * tools/asan-build.sh's posix-dl skip entry already covers this file
 * too, since posix-dl.c is the only test that exercises it and that
 * test is already excluded from the native run for the identical
 * reason. This guard moved here verbatim from src/dlfcn/dlfcn.c when
 * that file was split (see src/internal/plat_dlfcn.h's own banner):
 * PLAT_GLOBS already keeps this file out of a PLATFORM=linux build
 * entirely, but the native-ASan-on-a-PLATFORM=nt-config build this
 * guard defends against is a separate axis from PLATFORM, so the
 * explicit #error stays. */
#ifndef __has_feature
#define __has_feature(x) 0 /* not clang: never claim a clang-only feature */
#endif
#if !defined(_WIN32) && (defined(_NTLIBC_NATIVE_BUILD) || \
                        defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer))
#error "plat_dlfcn.c is NT-only (built on ntlibc_rpath_*(), which is NT-only itself); see src/internal/rpath.c's comment for why this guard exists"
#endif
#include <stddef.h>
#include <dlfcn.h>
#include <string.h>
#include "libc.h"
#include "plat_dlfcn.h"
#include "ntlibc/rpath.h"

/* dlclose()'s no-op special case below compares against this rather
 * than hardcoding NULL, so it reads the same way dlopen(NULL, ...)'s
 * own special case does. */
#define MAIN_IMAGE_HANDLE (__peb->ImageBaseAddress)

void *__plat_dlopen(const char *file, int mode)
{
	(void)mode; /* see this file's header comment: every bit is either
	             * already what LdrLoadDll() does, or N/A on NT */

	if (!file)
		return MAIN_IMAGE_HANDLE;

	return ntlibc_rpath_load(file);
}

/* entry->DllBase below is a disclosed, deliberately unmarked residual:
 * entry is not a parameter of this function at all -- it is
 * CONTAINING_RECORD-computed from `link`, a pointer-arithmetic offset
 * off a live circular list walk -- the same "struct/local-derived
 * pointer, not a parameter" class crt/delayload2.c's own
 * find_mapped_module() comment already established for the identical
 * PEB_LDR_DATA walk shape (InMemoryOrderModuleList there,
 * InLoadOrderModuleList here). Verified sound by hand regardless, for
 * the same reason: PEB_LDR_DATA's own lists are genuinely circular
 * and always populated for any live NT process. */
void *__plat_dlsym(void *__restrict handle, const char *__restrict name)
{
	if (handle == MAIN_IMAGE_HANDLE && __peb->Ldr) {
		LIST_ENTRY *head = &__peb->Ldr->InLoadOrderModuleList;
		LIST_ENTRY *link;
		ANSI_STRING symbol;
		size_t len = strlen(name);

		/* ANSI_STRING counts bytes in a USHORT.  Let the ordinary
		 * ntlibc_rpath_sym() path below record STATUS_NAME_TOO_LONG
		 * instead of wrapping a long name into a different symbol. */
		if (len > 0xffffu)
			return ntlibc_rpath_sym((ntlibc_dll_t *)handle, name);
		symbol.Buffer = (char *)name;
		symbol.Length = (USHORT)len;
		symbol.MaximumLength = symbol.Length;
		for (link = head->Flink; link != head; link = link->Flink) {
			LDR_DATA_TABLE_ENTRY *entry =
				(LDR_DATA_TABLE_ENTRY *)((char *)link -
				 offsetof(LDR_DATA_TABLE_ENTRY, InLoadOrderLinks));
			PVOID address = 0;
			if (NT_SUCCESS(LdrGetProcedureAddress(entry->DllBase,
					&symbol, 0, &address)))
				return address;
		}
		/* Record the ordinary diagnosable symbol error after the global
		 * search, without recording every module miss along the way. */
	}
	return ntlibc_rpath_sym((ntlibc_dll_t *)handle, name);
}

int __plat_dlclose(void *handle)
{
	/* Nothing on NT can sensibly unload the running program's own
	 * image out from under itself; POSIX only ever says an
	 * implementation "may" unload on dlclose(), never must, so
	 * treating this handle as always still open and returning success
	 * is conforming, not a shortcut. */
	if (handle == MAIN_IMAGE_HANDLE)
		return 0;

	return ntlibc_rpath_unload((ntlibc_dll_t *)handle) == 0 ? 0 : -1;
}

/* Thin forwards -- see plat_dlfcn.h's own banner for why sticky-backend/
 * single-shot-front-door reconciliation lives in src/dlfcn/dlfcn.c and
 * not here: ntlibc_rpath_error()/_error_seq() already ARE exactly that
 * sticky-message/monotonic-sequence pair, unchanged since before this
 * split, and other callers of rpath.c (not just dlfcn) already depend
 * on their stickiness. */
const char *__plat_dlerror(void) { return ntlibc_rpath_error(); }
unsigned long __plat_dlerror_seq(void) { return ntlibc_rpath_error_seq(); }
