/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SUPERSEDED as the documented interface: a tcc built from the
 * -Wl,--delay-all fork this project now targets can emit a real,
 * linker-built delay import (crt/delayload2.c's __delayLoadHelper2). A
 * *new* program wanting $ORIGIN-relative delay loading should declare
 * an ordinary `extern` function, call it normally, and build with
 * -Wl,--delay-all -- nothing below is needed. See test/delayall.c for
 * that path, and include/ntlibc/rpath.h for the $ORIGIN search both
 * mechanisms share.
 *
 * The macros below (NTLIBC_DELAY_DLL/_STUB/_NAME, ntlibc_delayLoadHelper2)
 * still work and remain for a tcc without --delay-all, or an existing
 * caller already written against them. New code should not reach for
 * them.
 *
 * A hand-authored delay-load mechanism: a delay-import descriptor, a
 * delay IAT, a delay import-name-table (INT), per-function thunks that
 * read through the IAT, and a helper (ntlibc_delayLoadHelper2, matching
 * MSVC's __delayLoadHelper2's role) that loads the DLL, resolves the
 * symbol, and patches the IAT slot in place on the first call. Needed
 * because tinycc's PE backend can only *read*
 * IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, never populate it or generate
 * per-function jump thunks -- so the "thunks" here are macro-generated
 * C functions instead of linker-synthesized asm stubs.
 *
 * NOT byte-for-byte MSVC's ImgDelayDescr: MSVC's classic format uses
 * 32-bit DWORD fields, which cannot represent a real address once
 * tcc's x86_64-win32 image base (0x140000000) is already past 4GB.
 * ntlibc_delay_thunk_t/ntlibc_delay_descr_t below use pointer-sized
 * fields instead -- an ntlibc-private convention, not the Windows SDK
 * layout, since nothing outside this file ever interprets these
 * structs. Attributes is therefore reserved (always 0), not the real
 * RvaBased bit.
 *
 * Directory entry 13 (IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT) does NOT need
 * to be set for this to work: the NT loader never walks delay-import
 * descriptors at load time regardless, so leaving it unset only costs
 * tooling visibility (`dumpbin /imports` won't show these) and MSVC's
 * /DELAY:UNLOAD/bound-IAT features, neither implemented here anyway.
 *
 * Cost to the caller vs. a real /DELAYLOAD import: the program must
 * declare imports with NTLIBC_DELAY_DLL/NTLIBC_DELAY_STUB and call
 * through the resulting stub, rather than an ordinary-looking `extern`
 * prototype -- there is no way to make that resolve lazily without
 * linker cooperation tcc doesn't have. The first call through any stub
 * pays LdrLoadDll/LdrGetProcedureAddress (shared per DLL, since the
 * module handle is cached); every call after reads the same IAT slot a
 * real delay-loaded call would.
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

/* Resolves the function behind IAT slot *piat, loading descr->dllname
 * through ntlibc_rpath_load() first if no module handle is cached yet.
 * Patches *piat with the resolved address and returns it. Never
 * returns NULL: a failure at either step is fatal, reported through
 * ntlibc_rpath_fail(). Called by NTLIBC_DELAY_STUB; exposed directly
 * only for a caller that wants to drive the mechanism without going
 * through the stub macros. */
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
