/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcsspn()/wcscspn()/wcspbrk(): the wchar_t mirrors of strspn()/
 * strcspn()/strpbrk() (src/string/strspn.c, strcspn.c, strpbrk.c), per
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcsspn.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcscspn.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcspbrk.html
 * DESCRIPTION/RETURN VALUE.  Same three-way relationship as the byte
 * family: wcspbrk() is wcscspn() plus a null-or-pointer decision.
 *
 * The one deliberate divergence from the byte versions is the set
 * representation.  strspn.c/strcspn.c build a 256-bit bitset on the
 * stack (32 bytes) and test membership in O(1).  The same trick for
 * wchar_t would need 65536 bits -- 8KiB of stack per call -- which is
 * out of proportion to the separator sets these are ever handed, and
 * would be a stack-overflow hazard in the deeply-nested call chains
 * this libc supports.  A linear wcschr() scan over the set is used
 * instead: O(|set|) per input unit rather than O(1), but the sets are
 * short and the code is the same shape as the already-implemented
 * wcschr().
 *
 * Matching is over wchar_t units.  For this target's UTF-16 wchar_t a
 * surrogate half in the separator set therefore matches that half
 * alone, not the composed supplementary character -- consistent with
 * wcschr()/wcsrchr(), which have exactly the same unit granularity,
 * and harmless in practice because separator sets are punctuation.
 */
#include <wchar.h>

/* Membership test.  Never called with c == 0: wcschr(set, 0) would
 * report a hit on the set's own terminator, which is why every loop
 * below guards on *s before asking. */
static int inset(const wchar_t *set, wchar_t c)
{
	for (; *set; set++) if (*set == c) return 1;
	return 0;
}

size_t wcsspn(const wchar_t *s, const wchar_t *set)
{
	const wchar_t *a = s;
	for (; *s && inset(set, *s); s++);
	return (size_t)(s - a);
}

size_t wcscspn(const wchar_t *s, const wchar_t *set)
{
	const wchar_t *a = s;
	for (; *s && !inset(set, *s); s++);
	return (size_t)(s - a);
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *set)
{
	s += wcscspn(s, set);
	return *s ? (wchar_t *)s : 0;
}
