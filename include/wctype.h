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

int iswalnum(wint_t);
int iswalpha(wint_t);
int iswblank(wint_t);
int iswcntrl(wint_t);
int iswdigit(wint_t);
int iswgraph(wint_t);
int iswlower(wint_t);
int iswprint(wint_t);
int iswpunct(wint_t);
int iswspace(wint_t);
int iswupper(wint_t);
int iswxdigit(wint_t);

int iswctype(wint_t, wctype_t);
wctype_t wctype(const char *);

wint_t towlower(wint_t);
wint_t towupper(wint_t);
wint_t towctrans(wint_t, wctrans_t);
wctrans_t wctrans(const char *);

#ifdef __cplusplus
}
#endif

#endif
