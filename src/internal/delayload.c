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
 * NT-only, for the same reason and by the same mechanism as
 * src/internal/rpath.c (see the comment there): this calls into that
 * file, which is itself excluded from a native ASan/UBSan build by
 * failing to compile outside _WIN32, so this file has to fail the same
 * way rather than leave a dangling reference to ntlibc_rpath_load() et
 * al. in that build's unconditional link.
 */
#ifndef _WIN32
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
