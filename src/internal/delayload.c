/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ntlibc_delayLoadHelper2(): the one routine every NTLIBC_DELAY_STUB
 * calls the first time its IAT slot is still unresolved. See
 * include/ntlibc/delayload.h for the descriptor/IAT/INT shapes and why
 * they are not byte-for-byte MSVC's ImgDelayDescr, and
 * include/ntlibc/rpath.h for the $ORIGIN search this uses to find the
 * DLL.
 *
 * NT-only, for the same reason and by the same (ASan-gated, not a bare
 * _WIN32 check -- see src/internal/rpath.c's longer comment for why) as
 * src/internal/rpath.c: this calls into that file, which is itself
 * excluded from a native ASan/UBSan build's compile, so this file has
 * to fail the same way rather than leave a dangling reference to
 * ntlibc_rpath_load() et al. in that build's unconditional link. A
 * plain native -fsyntax-only pass is unaffected, same as there.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#ifndef __has_feature
#define __has_feature(x) 0 /* not clang: never claim a clang-only feature */
#endif
#if !defined(_WIN32) && (defined(_NTLIBC_NATIVE_BUILD) || \
                        defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer))
#error "delayload.c is NT-only (calls into rpath.c, which is NT-only); see the comment above and src/internal/rpath.c"
#endif
#include "libc.h"
#include "ntlibc/delayload.h"

void *ntlibc_delayLoadHelper2(ntlibc_delay_descr_t *descr, ntlibc_delay_thunk_t *piat)
{
	unsigned long index;
	ntlibc_dll_t *dll;
	void *proc;
	const char *name;

	/* Same trick __delayLoadHelper2 uses: which import this call is for
	 * is recovered from *where in the IAT* the caller's slot sits,
	 * rather than being passed separately -- so the descriptor alone
	 * (plus this one slot pointer) is enough, matching the real ABI's
	 * two-argument helper shape. */
	index = (unsigned long)(piat - descr->iat);

	/* *descr->modhandle below is a disclosed, deliberately unmarked
	 * residual, surfaced only after descr's own nonnull mark let this
	 * checker explore further into this function than before (the
	 * "deeper exploration unlocked" effect prior sweeps in this tree
	 * already measured, not a regression): descr->modhandle is a
	 * struct FIELD's own value, distinct from descr itself (already
	 * required, see include/ntlibc/delayload.h), and `nonnull`
	 * cannot describe a field reached through a parameter, only the
	 * parameter itself. Verified sound by hand regardless: every real
	 * descr this function is ever called with is NTLIBC_DELAY_DLL's
	 * own macro-built static (include/ntlibc/delayload.h), whose own
	 * modhandle field is always `&ntlibc_delay_mod_##dllvar` -- the
	 * address of a real, file-static variable, never NULL. */
	dll = *descr->modhandle;
	if (!dll) {
		dll = ntlibc_rpath_load(descr->dllname);
		if (!dll) ntlibc_rpath_fail(descr->dllname, "<module>");
		*descr->modhandle = dll;
	}

	name = descr->nametable[index].name;
	proc = ntlibc_rpath_sym(dll, name);
	if (!proc) ntlibc_rpath_fail(descr->dllname, name);

	piat->function = proc;
	return proc;
}

// NOLINTEND(misc-include-cleaner)
