/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <dlfcn.h>: dlopen()/dlsym()/dlclose()/dlerror(), layered directly on
 * top of the loading primitives include/ntlibc/rpath.h already exposes
 * (ntlibc_rpath_load()/_sym()/_unload()/_error()/_error_seq(), all
 * implemented in src/internal/rpath.c over LdrLoadDll()/
 * LdrGetProcedureAddress()/LdrUnloadDll()). Nothing in this file talks
 * to ntdll directly; every NT-specific decision -- what counts as an
 * absolute path, where $ORIGIN is, the sticky-error bookkeeping -- is
 * already made there. What is left here is entirely POSIX-shape
 * plumbing: turning three loosely-typed rpath.c calls into the four
 * functions and refcounting/error contract dlfcn.h promises.
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
 * ---- dlopen(NULL, ...): the main program's own exports ---------------
 *
 * dlopen.html DESCRIPTION: "If file is a null pointer, dlopen() shall
 * return a global symbol table handle for the currently running
 * process image." __peb->ImageBaseAddress (src/internal/nt.h's PEB,
 * populated by the OS before crt1.c ever runs) is exactly that: the
 * base address of the main executable's own already-mapped PE image,
 * the same kind of value ntlibc_rpath_load() returns for a DLL.
 * LdrGetProcedureAddress() -- what ntlibc_rpath_sym() already wraps --
 * does not care whether the base it is handed is a DLL or the main
 * EXE; it walks that image's own IMAGE_EXPORT_DIRECTORY exactly the
 * same way either time (src/internal/pe.c's hand-rolled walker does
 * this identically, for the same reason). So dlsym() on this handle is
 * not a special case below -- it is routed through ntlibc_rpath_sym()
 * like any other handle.
 *
 * What IS a real, worth-recording limitation: an ordinary ntlibc
 * program is a `-nostdlib`-linked, tcc-built PE executable, and tcc
 * does not emit an export directory for an EXE unless something asks
 * it to (there is no ordinary reason to export symbols from a program
 * that is not itself a DLL) -- so DataDirectory[IMAGE_DIRECTORY_ENTRY_
 * EXPORT] is empty for a normal ntlibc-linked program, and every
 * dlsym() call against a dlopen(NULL, ...) handle fails with "symbol
 * not found" (STATUS_ENTRYPOINT_NOT_FOUND, via LdrGetProcedureAddress)
 * even for a symbol that is plainly defined in that very program. This
 * is not a bug in this file: dlopen(NULL, ...) itself succeeds and
 * returns a real, usable-for-dlclose() handle, exactly as specified;
 * it is dlsym() against it that can only ever see what the linker
 * chose to export, and by default that set is empty. A program that
 * wants dlopen(NULL, ...)+dlsym() to find its own symbols would need
 * to link with an explicit export directive (tcc's -bt/-rdynamic-
 * equivalent, if any, or a hand-written .def) -- out of scope for this
 * change, which only had to say why the handle alone is not enough.
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
 * reason. */
#ifndef __has_feature
#define __has_feature(x) 0 /* not clang: never claim a clang-only feature */
#endif
#if !defined(_WIN32) && (defined(_NTLIBC_NATIVE_BUILD) || \
                        defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer))
#error "dlfcn.c is NT-only (built on ntlibc_rpath_*(), which is NT-only itself); see src/internal/rpath.c's comment for why this guard exists"
#endif
#include <stddef.h>
#include <dlfcn.h>
#include "libc.h"
#include "ntlibc/rpath.h"

/* dlclose()'s no-op special case below compares against this rather
 * than hardcoding NULL, so it reads the same way dlopen(NULL, ...)'s
 * own special case does. */
#define MAIN_IMAGE_HANDLE (__peb->ImageBaseAddress)

void *dlopen(const char *file, int mode)
{
	(void)mode; /* see this file's header comment: every bit is either
	             * already what LdrLoadDll() does, or N/A on NT */

	if (!file)
		return MAIN_IMAGE_HANDLE;

	return ntlibc_rpath_load(file);
}

void *dlsym(void *__restrict handle, const char *__restrict name)
{
	return ntlibc_rpath_sym((ntlibc_dll_t *)handle, name);
}

int dlclose(void *handle)
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

/* dlerror()'s single-shot contract (dlerror.html DESCRIPTION: "invoking
 * dlerror() a second time, immediately following a prior invocation,
 * shall result in NULL being returned") layered over
 * ntlibc_rpath_error(), which is deliberately NOT single-shot -- it
 * stays sticky across repeated calls so callers that already depend on
 * that (test/posix-dl.c's test_dl_underlying_mechanism(), calling
 * ntlibc_rpath_error() twice in a row and expecting the same string
 * both times) keep working unmodified. The two are reconciled with one
 * extra piece of state entirely local to this file: the seq value
 * (ntlibc_rpath_error_seq(), bumped once per failure recorded in
 * rpath.c) that was already reported through dlerror(). A successful
 * dlopen()/dlsym()/dlclose() call never bumps that counter, so it
 * cannot un-consume an already-reported error -- exactly the
 * "unless an intervening call ... returned NULL and set the error
 * condition" exception dlerror.html itself carves out; nothing else in
 * this file needs to special-case a successful call at all. */
char *dlerror(void)
{
	static unsigned long reported;
	unsigned long cur = ntlibc_rpath_error_seq();

	if (cur == 0 || cur == reported)
		return NULL;

	reported = cur;
	/* ntlibc_rpath_error() owns this buffer (a function-local static in
	 * rpath.c) and promises it stays valid until the next rpath.c
	 * call, the same lifetime dlerror.html itself specifies for its
	 * own return value ("may be overwritten by a subsequent call").
	 * The const is dropped only because dlerror()'s prototype is
	 * `char *`, not `const char *` -- POSIX's own signature, not
	 * something this file chose -- and nothing here writes through it. */
	return (char *)ntlibc_rpath_error();
}
