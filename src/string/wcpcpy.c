/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcpcpy()/wcpncpy(): the wchar_t mirrors of stpcpy()/stpncpy()
 * (src/string/stpcpy.c, stpncpy.c), with those pages' contracts read
 * for wide characters:
 *   stpcpy.html RETURN VALUE  -- "a pointer to the terminating null
 *     byte" [null wide character] "copied into the s1 buffer".
 *   stpncpy.html RETURN VALUE -- "a pointer to the terminating null
 *     byte in s1, or, if s1 is not null-terminated, s1 + n".
 * These differ from wcscpy()/wcsncpy() (already implemented) only in
 * what they return; the copying is identical, which is why they are a
 * literal transliteration rather than a re-derivation.
 *
 * wcpncpy() pads the remainder with null wide characters, exactly as
 * wcsncpy() does -- wmemset() rather than memset(), since the padding
 * count is in wchar_t units.  In the padded case the returned pointer
 * is the FIRST pad unit (the terminating null), which is what
 * stpncpy.c's `return d` before the memset-advance likewise gives.
 */
#include <wchar.h>

wchar_t *wcpcpy(wchar_t *__restrict d, const wchar_t *__restrict s)
{
	for (; (*d = *s); s++, d++);
	return d;
}

wchar_t *wcpncpy(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	for (; n && (*d = *s); n--, s++, d++);
	wmemset(d, 0, n);
	return d;
}
