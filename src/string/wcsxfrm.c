/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcsxfrm()/wcsxfrm_l(): the wchar_t mirrors of strxfrm()/strxfrm_l()
 * (src/string/strxfrm.c), per
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcsxfrm.html
 * DESCRIPTION, RETURN VALUE -- transform ws2 so that "the result of
 * wcscmp() on two transformed wide strings shall be the same as the
 * result of wcscoll() on the two original strings", returning "the
 * length of the transformed wide-character string, not including the
 * terminating null".
 *
 * ntlibc's single C/POSIX locale collates in code-unit order (see
 * src/string/wcscoll.c), so the transformation that satisfies the
 * clause is the identity: wcscmp(xfrm(a), xfrm(b)) == wcscmp(a, b) ==
 * wcscoll(a, b) trivially.  This is a deliberate answer, not an
 * unwritten one.
 *
 * The return value is the FULL transformed length even when it does not
 * fit, which is what makes the two-call size-query idiom work; only
 * then is ws1 permitted to be a null pointer, and only when n is 0.
 * Truncation still null-terminates, matching strxfrm.c.  Note the count
 * is in wide characters throughout -- wcslen()/wmemcpy(), never bytes.
 *
 * No ERRORS are produced: wcsxfrm.html's [EINVAL] is for a string
 * containing characters outside the locale's domain, and no wchar_t
 * value is outside a code-unit-ordered domain, so errno is left alone
 * (test/posix-string.c pins the same property for strxfrm()).
 */
#include <wchar.h>
#include <locale.h>

size_t wcsxfrm(wchar_t *__restrict dest, const wchar_t *__restrict src, size_t n)
{
	size_t l = wcslen(src);
	if (n > l) wmemcpy(dest, src, l + 1);
	else if (n) { wmemcpy(dest, src, n - 1); dest[n - 1] = 0; }
	return l;
}

size_t wcsxfrm_l(wchar_t *__restrict dest, const wchar_t *__restrict src, size_t n, locale_t loc)
{
	(void)loc;
	return wcsxfrm(dest, src, n);
}
