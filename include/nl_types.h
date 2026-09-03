/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <nl_types.h>: message catalogue access.  See src/misc/catgets.c for the
 * catalogue reader and the file format it understands.
 *
 * nl_catd and nl_item are declared in bits/alltypes.h rather than here,
 * since <langinfo.h> also must define nl_item and two independent
 * typedefs of the same name would collide.
 *
 * NL_SETD is 1, matching the default set number gencat assigns to
 * messages before any $set directive. NL_CAT_LOCALE makes catalogue
 * selection follow LC_MESSAGES rather than LANG; catopen() still reads
 * the two separately even though setlocale() only accepts "C"/"POSIX",
 * so the distinction stays meaningful.
 *
 * There is no gencat in this tree; catopen() resolves NLSPATH and reads a
 * catalogue in the byte format musl's gencat emits, so a catalogue built
 * elsewhere works. With no catalogue file, catopen() fails ENOENT (POSIX
 * permits this) and catgets() never runs.
 */
#ifndef _NL_TYPES_H
#define _NL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <allocation_tokens.h>
#include <string_tokens.h>
#include <ownership.h>

#define __NEED_nl_catd
#define __NEED_nl_item

#include <bits/alltypes.h>

#define NL_SETD 1
#define NL_CAT_LOCALE 1

tokdef catalog_opened
	dynamic_storage
	implemented_by(heap_allocated)
	sentinel_exclude(-1);

int      catclose(nl_catd consume(catalog_opened));
char    *catgets(nl_catd withtok(catalog_opened), int, int, const char *);
withtok(catalog_opened)
nl_catd  catopen(const char * withtok(null_terminated), int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
