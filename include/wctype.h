/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <wctype.h> -- https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/wctype.h.html
 *
 * ntlibc is C/POSIX-locale-only (src/misc/locale.c never accepts any
 * other locale name), and wchar_t here is a 16-bit UTF-16 code unit
 * (WCHAR_MAX == 0xffff, see wchar.h), not the 32-bit-holds-one-codepoint
 * type POSIX text elsewhere implicitly assumes.  Two decisions that
 * follow from that, documented once here rather than per function:
 *
 *  - Classification is ASCII-only, exactly mirroring ctype.h's is*()
 *    family (isalpha() et al. there answer false for the whole
 *    0x80-0xff range, not just >0xff -- see src/ctype/isalpha.c and
 *    friends).  The C locale does not require classifying anything
 *    outside the "portable character set", and answering iswalpha()
 *    true for a Latin-1 letter while isalpha() answers false for the
 *    same byte would be the exact inconsistency the design brief warns
 *    against, so no BMP code point past 0x7f is ever classified true.
 *
 *  - A lone surrogate half (0xd800-0xdfff) is not a valid character;
 *    iswalpha.html's DESCRIPTION restricts the domain to "a valid
 *    wide-character code, or ... WEOF" and says behaviour is undefined
 *    for anything else. ntlibc picks a defined answer anyway: every
 *    classification function returns 0 (false) and every conversion
 *    function returns the argument unchanged, because a surrogate half
 *    simply falls outside every ASCII range test below -- exactly like
 *    any other out-of-range wint_t, WEOF included, gets handled by the
 *    same range checks with no special-casing required.
 */

#ifndef _WCTYPE_H
#define _WCTYPE_H

#include <features.h>

#define __NEED_wint_t
#define __NEED_wctype_t

#include <bits/alltypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#undef WEOF
#define WEOF 0xffffffffU

/* wctrans_t: "a scalar type ... values which represent locale-specific
 * character mappings" (wctype.h.html DESCRIPTION).  ntlibc's one locale
 * defines exactly two mappings ("tolower", "toupper" -- wctrans.html
 * "the following character mapping names are defined in all locales"),
 * so a small dense int enum is all the opacity this type needs; there
 * is no shared __NEED_wctrans_t machinery in bits/alltypes.h to hook
 * into (nothing else needs the type), so it is defined right here. */
typedef int wctrans_t;

/* Every iswXXX() here (src/ctype/isw*.c) is a one-line forward into the
 * matching ctype.h is*() function, already __pure__ for the reasons
 * given there; towlower/towupper (src/ctype/tow*.c) are the same ASCII
 * bit trick as tolower/toupper, gated on iswupper()/iswlower(). None of
 * this reads locale state beyond the fixed C/POSIX-only design this
 * whole header's own banner comment documents, touches errno, or does
 * I/O -- each is a total, deterministic function of its argument(s). */
int iswalnum(wint_t) __attribute__((__pure__));
int iswalpha(wint_t) __attribute__((__pure__));
int iswblank(wint_t) __attribute__((__pure__));
int iswcntrl(wint_t) __attribute__((__pure__));
int iswdigit(wint_t) __attribute__((__pure__));
int iswgraph(wint_t) __attribute__((__pure__));
int iswlower(wint_t) __attribute__((__pure__));
int iswprint(wint_t) __attribute__((__pure__));
int iswpunct(wint_t) __attribute__((__pure__));
int iswspace(wint_t) __attribute__((__pure__));
int iswupper(wint_t) __attribute__((__pure__));
int iswxdigit(wint_t) __attribute__((__pure__));

/* iswctype() (src/ctype/iswctype.c) is a closed switch over the twelve
 * iswXXX() functions above, keyed on the wctype_t desc; wctype()
 * (src/ctype/wctype.c) only strcmp()s name against a fixed static
 * array of the twelve class-name literals, reading its own arguments
 * and this tree's fixed constant table only -- no locale variation is
 * possible (this tree has exactly one locale), no errno, no writes. */
int iswctype(wint_t, wctype_t) __attribute__((__pure__));
wctype_t wctype(const char *) __attribute__((__pure__));

/* towctrans()/wctrans() (src/ctype/towctrans.c, wctrans.c) are the same
 * shape as iswctype()/wctype() just above: a closed switch/strcmp
 * dispatch to towlower()/towupper(), with no locale variation, errno,
 * or writes anywhere in either body. */
wint_t towlower(wint_t) __attribute__((__pure__));
wint_t towupper(wint_t) __attribute__((__pure__));
wint_t towctrans(wint_t, wctrans_t) __attribute__((__pure__));
wctrans_t wctrans(const char *) __attribute__((__pure__));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
