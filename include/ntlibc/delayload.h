/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ---- Superseded as the documented interface -----------------------------
 *
 * A tcc built from the -Wl,--delay-all fork this project now targets
 * *can* emit a real, linker-built delay import (see crt/delayload2.c's
 * __delayLoadHelper2 and its header comment) -- which the "Can tcc emit
 * a real (linker-built) delay import? No." heading right below answered
 * as of when this file was the only mechanism there was. That answer is
 * now file-scoped history, not current guidance: a *new* program that
 * wants $ORIGIN-relative delay loading should declare an ordinary
 * `extern` function, call it normally, and build with -Wl,--delay-all
 * -- nothing below is needed at the call site at all. See test/delayall.c
 * for that path end to end, and include/ntlibc/rpath.h for the $ORIGIN
 * search both mechanisms share.
 *
 * The macros below still work (NTLIBC_DELAY_DLL/_STUB/_NAME,
 * ntlibc_delayLoadHelper2, the whole hand-rolled descriptor/IAT/INT
 * shape this file's header comment describes) and remain here for a tcc
 * without --delay-all, or an existing caller already written against
 * them -- keeping them working cost nothing once the underlying
 * $ORIGIN search (rpath.c) already had to exist for the linker-driven
 * path too. New code should not reach for them.
 *
 * A real delay-load mechanism, hand-authored: a delay-import descriptor,
 * a delay IAT, a delay import-name-table (INT), per-function thunks that
 * read through the IAT, and a helper (ntlibc_delayLoadHelper2, matching
 * the role and calling shape of MSVC's __delayLoadHelper2) that loads
 * the DLL, resolves the symbol and patches the IAT slot in place on the
 * first call. include/ntlibc/rpath.h supplies the $ORIGIN-relative
 * search this helper uses to find the DLL -- that is the "RPATH
 * equivalent" this whole facility exists for.
 *
 * ---- Can tcc emit a real (linker-built) delay import? -----------------
 *
 * No. tinycc's PE backend (tccpe.c) only ever *reads*
 * IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT as the #define naming data
 * directory slot 13 (tccpe.c:206); nothing in tccpe.c ever populates
 * that slot or builds an IMAGE_DELAYLOAD_DESCRIPTOR. Compare
 * pe_build_imports() (tccpe.c, ~line 875 on), which *does* construct
 * ordinary IMAGE_IMPORT_DESCRIPTOR entries for a normal ".idata" import
 * -- there is no equivalent pe_build_delay_imports(). So the compiler
 * having no /DELAYLOAD switch is not the blocker (delay-load descriptors
 * are just data + a helper, both authorable in C); the actual blocker
 * tcc raises is that its linker cannot auto-generate the per-function
 * jump thunks or populate directory entry 13 for us. This file works
 * around exactly that: the "thunks" are macro-generated C functions
 * instead of linker-synthesized asm stubs, and directory entry 13 is
 * left unset (see below).
 *
 * ---- Why this is NOT byte-for-byte MSVC's ImgDelayDescr ---------------
 *
 * MSVC's classic (VC6-era) delay-load format sets Attributes.RvaBased=0
 * and stores plain absolute VAs in what would otherwise be RVA fields --
 * exactly the trick that lets a C static initializer express them
 * without linker relocation support beyond ordinary "address of a
 * global", which is all tcc needs to do already for any `&foo`
 * initializer. That format's fields are all 32-bit DWORDs, though, and
 * that is fatal on x86_64: tcc's own default EXE image base for
 * x86_64-win32 is 0x140000000 (tccpe.c: `#define IMAGE_BASE_EXE
 * 0x140000000ULL`), which is already past 4GB before a single global's
 * address is added to it, so a real global's VA cannot be represented
 * in a 32-bit field at all. Since nothing outside this file ever
 * interprets these structs -- there is no external delayimp.lib,
 * dbghelp walk, or MSVC-compatible tool reading them here -- there is
 * no reason to reuse the 32-bit-DWORD layout. ntlibc_delay_thunk_t and
 * ntlibc_delay_descr_t below use pointer-sized fields instead, keeping
 * the real mechanism's *shape* (a descriptor naming a DLL and a module
 * handle slot, a delay IAT of resolved-or-not function pointers, a
 * delay INT of import names, a shared helper that patches the IAT in
 * place on first call, indexing the INT by pointer arithmetic on the
 * IAT the same way __delayLoadHelper2 does) while being an ntlibc-
 * private convention rather than the literal Windows SDK layout.
 * Attributes accordingly is not the real RvaBased bit -- it is reserved
 * (always 0) here, since every field in ntlibc_delay_descr_t is always
 * a plain native pointer.
 *
 * ---- Does directory entry 13 need to be set for this to work? ---------
 *
 * No. The NT loader does not walk delay-import descriptors at image
 * load time at all -- that is the entire point of delay loading: no
 * work happens (no LdrLoadDll, no LdrGetProcedureAddress) until a
 * generated thunk is actually called, and by then the thunk already has
 * everything it needs (a pointer to its own descriptor, compiled in) --
 * it does not need the loader to have found the descriptor through the
 * data directory first. What leaving IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT
 * unset (as this build does -- tcc's linker has no feature to populate
 * it, and nothing here needs it to be) actually costs is entirely
 * tooling and optional-feature visibility, not correctness:
 *   - `dumpbin /imports` and similar tools locate delay imports through
 *     that directory entry; without it they will not show ours (they
 *     are, however, still visible as ordinary data in the binary to
 *     anyone who looks for the symbols by name).
 *   - MSVC's `/DELAY:UNLOAD` (FreeLibrary-and-reset-the-IAT support) and
 *     bound-IAT snap optimisation are both driven off fields this
 *     format does not even carry (BoundIAT/UnloadIAT) -- unload is not
 *     implemented here at all, and there is no bound-import
 *     optimisation to lose since this is the unbound (VA, not
 *     pre-resolved) form regardless.
 * A post-link tool that patches the data directory afterward would be
 * possible, but is not written here: it buys only the dumpbin-visibility
 * item above, at the cost of another moving part the kaem bootstrap
 * (tcc + mkdir + cp + catm, nothing else) would have to avoid depending
 * on -- not worth it for a purely cosmetic gain.
 *
 * ---- What this costs the caller, compared to a real /DELAYLOAD import -
 *
 * A real delay import looks exactly like a normal `extern` call at the
 * source level; the linker rewrites the call site to go through the
 * generated thunk. Here the program must instead declare its delay
 * imports with NTLIBC_DELAY_DLL/NTLIBC_DELAY_STUB below and call
 * through the resulting stub function -- there is no way to make an
 * ordinary-looking `extern int add(int, int);` prototype secretly
 * resolve lazily without linker cooperation tcc does not have. The
 * first call through any one stub pays the LdrLoadDll/
 * LdrGetProcedureAddress cost (shared per DLL: only the first stub for
 * a given DLL pays LdrLoadDll, since ntlibc_delayLoadHelper2 caches the
 * module handle); every call after that reads the same delay IAT slot
 * a real delay-loaded call would.
 */
#ifndef NTLIBC_DELAYLOAD_H
#define NTLIBC_DELAYLOAD_H

#include "rpath.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One delay IAT slot, and (in the INT array) one delay-import-name-
 * table slot -- the same union covers both because the real ABI does:
 * before resolution an IAT slot is logically "not yet a function", and
 * the INT never holds anything but names in this implementation (no
 * import-by-ordinal support -- nothing here needs it). */
typedef union ntlibc_delay_thunk {
	void *function;        /* IAT: 0 until resolved, then the resolved address */
	const char *name;      /* INT: the symbol name to resolve at this index */
} ntlibc_delay_thunk_t;

/* One DLL's delay-load descriptor. attrs is reserved (always 0 --- see
 * the header comment on why this is not MSVC's RvaBased bit). modhandle
 * points at a private static slot that caches the loaded module (0
 * until the first successful load); iat/nametable are same-length
 * arrays, indexed the same way, of length `count`. */
typedef struct ntlibc_delay_descr {
	unsigned long attrs;
	const char *dllname;
	void **modhandle;
	ntlibc_delay_thunk_t *iat;
	const ntlibc_delay_thunk_t *nametable;
	unsigned long count;
} ntlibc_delay_descr_t;

/* Resolves the function behind IAT slot *piat (found by pointer
 * arithmetic against descr->iat, the same way __delayLoadHelper2 finds
 * its index), loading descr->dllname through ntlibc_rpath_load() first
 * if no module handle is cached yet. Patches *piat with the resolved
 * address and returns it. Never returns NULL: a failure at either step
 * is fatal, reported through ntlibc_rpath_fail(). Called by
 * NTLIBC_DELAY_STUB; exposed directly only for a caller that wants to
 * drive the mechanism without going through the stub macros.
 *
 * Both required: descr->iat/descr->modhandle/descr->dllname/
 * descr->nametable and piat->function are all dereferenced
 * unconditionally in src/internal/delayload.c's own body, with no
 * NULL check of descr or piat themselves anywhere in it. Every real
 * call site is NTLIBC_DELAY_STUB's own macro expansion (this header,
 * below), which always passes `&ntlibc_delay_descr_##dllvar` (a
 * file-static struct's address) and `&ntlibc_delay_iat_##dllvar[index]`
 * (a file-static array element's address) -- neither can ever be NULL,
 * and no other call site exists anywhere in this tree. */
void *ntlibc_delayLoadHelper2(ntlibc_delay_descr_t *descr, ntlibc_delay_thunk_t *piat)
    __attribute__((nonnull(1, 2)));

/* NTLIBC_DELAY_NAME(s) -- one INT entry naming import `s`; use inside
 * NTLIBC_DELAY_DLL's trailing argument list, one per import, in the same
 * order as `count` and as the indices passed to NTLIBC_DELAY_STUB. */
#define NTLIBC_DELAY_NAME(s) { (s) }

/* NTLIBC_DELAY_DLL(dllvar, dllnamestr, n, ...) -- declares the
 * descriptor, delay IAT and delay INT for one DLL as file-scope statics
 * named after `dllvar`. `n` is the number of imports (must match the
 * number of NTLIBC_DELAY_NAME entries in `...` and the index range
 * given to NTLIBC_DELAY_STUB below) -- there is no linker-side count to
 * infer this from, so it is given explicitly rather than guessed by a
 * preprocessor argument-counting trick. */
#define NTLIBC_DELAY_DLL(dllvar, dllnamestr, n, ...) \
	static void *ntlibc_delay_mod_##dllvar; \
	static ntlibc_delay_thunk_t ntlibc_delay_iat_##dllvar[n]; \
	static const ntlibc_delay_thunk_t ntlibc_delay_int_##dllvar[n] = { __VA_ARGS__ }; \
	static ntlibc_delay_descr_t ntlibc_delay_descr_##dllvar = \
		{ 0, dllnamestr, &ntlibc_delay_mod_##dllvar, \
		  ntlibc_delay_iat_##dllvar, ntlibc_delay_int_##dllvar, (unsigned long)(n) }

/* NTLIBC_DELAY_STUB(ret, dllvar, index, name, params, args) -- defines
 * `name` as a function with parameter list `params` (parenthesised,
 * e.g. "(int a, int b)") and matching call argument list `args` (e.g.
 * "(a, b)") that reads ntlibc_delay_iat_<dllvar>[index] -- resolving it
 * through ntlibc_delayLoadHelper2() the first time it is 0 -- and calls
 * through it. `index` must match the position of this import's
 * NTLIBC_DELAY_NAME in the NTLIBC_DELAY_DLL that declared `dllvar`. Use
 * NTLIBC_DELAY_STUB_VOID for a void-returning function. */
#define NTLIBC_DELAY_STUB(ret, dllvar, index, name, params, args) \
	ret name params \
	{ \
		void *__ntlibc_fp = ntlibc_delay_iat_##dllvar[index].function; \
		if (!__ntlibc_fp) \
			__ntlibc_fp = ntlibc_delayLoadHelper2(&ntlibc_delay_descr_##dllvar, &ntlibc_delay_iat_##dllvar[index]); \
		return ((ret (*) params)__ntlibc_fp) args; /* NOLINT(bugprone-macro-parentheses) -- ret and params form a function-pointer type, while args is already the caller-supplied parenthesized argument list */ \
	}

#define NTLIBC_DELAY_STUB_VOID(dllvar, index, name, params, args) \
	void name params \
	{ \
		void *__ntlibc_fp = ntlibc_delay_iat_##dllvar[index].function; \
		if (!__ntlibc_fp) \
			__ntlibc_fp = ntlibc_delayLoadHelper2(&ntlibc_delay_descr_##dllvar, &ntlibc_delay_iat_##dllvar[index]); \
		((void (*) params)__ntlibc_fp) args; /* NOLINT(bugprone-macro-parentheses) -- params forms a function-pointer type and args is already the caller-supplied parenthesized argument list */ \
	}

#ifdef __cplusplus
}
#endif

#endif
