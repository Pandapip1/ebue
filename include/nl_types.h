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
 * nl_types.h.html DESCRIPTION requires, verbatim: "The <nl_types.h>
 * header shall define at least the following types:" -- nl_catd, "Used
 * by the message catalog functions catopen(), catgets(), and catclose()
 * to identify a catalog descriptor.", and nl_item, "Used by
 * nl_langinfo() to identify items of langinfo data. Values of objects of
 * type nl_item are defined in <langinfo.h>." -- then "The <nl_types.h>
 * header shall define at least the following symbolic constants:",
 * NL_SETD and NL_CAT_LOCALE, and then the three prototypes.
 *
 * Both types are declared in bits/alltypes.h rather than here, because
 * <langinfo.h> "shall define the nl_item type as described in
 * <nl_types.h>" and two independent typedefs of the same name would be a
 * redefinition the moment a program included both.
 *
 * NL_SETD's value is implementation-defined ("The value of NL_SETD is
 * implementation-defined."); 1 is chosen to match the default set number
 * gencat assigns to messages that appear before any $set directive.
 *
 * NL_CAT_LOCALE is the catopen() oflag that makes catalogue selection
 * follow LC_MESSAGES rather than LANG.  On this platform that
 * distinction is nearly vacuous -- setlocale() accepts only "C" and
 * "POSIX" -- but catopen() still reads the two sources separately rather
 * than collapsing them, so a program that sets one and not the other
 * gets the substitution it asked for.
 *
 * Scope, stated plainly so a caller is not misled: there is no gencat in
 * this tree, so nothing here produces a catalogue.  What catopen() does
 * is resolve NLSPATH exactly as catopen.html specifies and read a
 * catalogue in the byte format musl's gencat emits, so a catalogue built
 * elsewhere works.  With no NLSPATH and no catalogue file, catopen()
 * fails with ENOENT -- which catopen.html permits outright, every one of
 * its ERRORS entries being under "The catopen() function may fail if:"
 * -- and catgets() then never runs, its own contract being undefined for
 * a descriptor catopen() did not return.
 */
#ifndef _NL_TYPES_H
#define _NL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <ownership.h>

#define __NEED_nl_catd
#define __NEED_nl_item

#include <bits/alltypes.h>

#define NL_SETD 1
#define NL_CAT_LOCALE 1

tokdef catalog_opened
	dynamic_storage
	sentinel_exclude(-1);

int      catclose(nl_catd consume(catalog_opened));
char    *catgets(nl_catd withtok(catalog_opened), int, int, const char *);
withtok(catalog_opened)
nl_catd  catopen(const char * __NTLIBC_STRING, int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
