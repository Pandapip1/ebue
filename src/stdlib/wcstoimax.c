/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcstoimax()/wcstoumax(): C99 7.20.1.4/7.24.4.1.2, the wchar_t mirror
 * of strtoimax()/strtoumax() (src/stdlib/strtol.c). wchar_t on this
 * target is an unsigned short UTF-16 code unit (each arch's
 * bits/alltypes.h.gen); every character this parser cares about -- digits,
 * '+'/'-', the ASCII letters used as base-36 digits, and the C locale's
 * whitespace set -- lives below U+0080, so comparing wchar_t values
 * directly against L'0'..L'9' etc. needs no wctype.h machinery (which
 * this library does not have) any more than strtol.c needs <ctype.h> to
 * do more than ASCII.  Kept as its own two functions rather than
 * generalising strtol.c's parse()/strtox() to take a wchar_t* -- there
 * is no other wide integer conversion function to share it with (no
 * wcstol()/wcstoul() is declared in wchar.h), so a second, parallel
 * implementation is simpler than templating one for a single caller.
 */
#include <inttypes.h>
#include <wchar.h>
#include <limits.h>
#include <errno.h>
#include <features.h>

static int isws(wchar_t c)
{
	return c == L' ' || c == L'\t' || c == L'\n' || c == L'\v' || c == L'\f' || c == L'\r';
}

/* Same contract as strtol.c's parse(): returns 1 on overflow (out =
 * UINTMAX_MAX), *neg from the sign, *end the first unparsed wchar_t (or
 * nptr if nothing numeric was found). */
static int wparse(const wchar_t *nptr, const wchar_t **end, int base, int *neg, uintmax_t *out)
{
	const wchar_t *s = nptr;
	uintmax_t v = 0, cutoff;
	int any = 0, ovf = 0, cutlim, d;
	wchar_t c;

	*neg = 0;
	while (isws(*s)) s++;
	if (*s == L'+') s++;
	else if (*s == L'-') { *neg = 1; s++; }
	if (base == 0) {
		if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')
		 && ((s[2] >= L'0' && s[2] <= L'9') || (s[2] >= L'a' && s[2] <= L'f') || (s[2] >= L'A' && s[2] <= L'F')))
			{ base = 16; s += 2; }
		else if (s[0] == L'0') base = 8;
		else base = 10;
	} else if (base == 16) {
		if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')
		 && ((s[2] >= L'0' && s[2] <= L'9') || (s[2] >= L'a' && s[2] <= L'f') || (s[2] >= L'A' && s[2] <= L'F')))
			s += 2;
	}
	if (base < 2 || base > 36) { errno = EINVAL; *end = nptr; *out = 0; return 0; }
	cutoff = UINTMAX_MAX / (unsigned)base;
	cutlim = (int)(UINTMAX_MAX % (unsigned)base);
	for (;; s++) {
		c = *s;
		if (c >= L'0' && c <= L'9') d = c - L'0';
		else if (c >= L'a' && c <= L'z') d = c - L'a' + 10;
		else if (c >= L'A' && c <= L'Z') d = c - L'A' + 10;
		else break;
		if (d >= base) break;
		any = 1;
		if (ovf) continue;
		if (v > cutoff || (v == cutoff && d > cutlim)) { ovf = 1; continue; }
		v = v * (unsigned)base + (unsigned)d;
	}
	*end = any ? s : nptr;
	*out = ovf ? UINTMAX_MAX : v;
	return ovf;
}

/* Same "0 - x" two's-complement reasoning as strtol.c's strtox(): see
 * the comment there for why it is deliberate and not a stray sign bug. */
__wraps static uintmax_t wcstox(const wchar_t *nptr, wchar_t **endptr, int base, uintmax_t lim)
{
	const wchar_t *end;
	uintmax_t v;
	int neg, ovf;

	ovf = wparse(nptr, &end, base, &neg, &v);
	if (endptr) *endptr = (wchar_t *)end;
	if (lim == UINTMAX_MAX) {
		if (ovf || v > lim) { errno = ERANGE; return lim; }
		return neg ? (0 - v) & lim : v;
	}
	if (neg) {
		if (ovf || v > lim + 1) { errno = ERANGE; return 0 - (lim + 1); }
		return 0 - v;
	}
	if (ovf || v > lim) { errno = ERANGE; return lim; }
	return v;
}

intmax_t wcstoimax(const wchar_t *__restrict s, wchar_t **__restrict e, int b)
{ return (intmax_t)wcstox(s, e, b, INTMAX_MAX); }
uintmax_t wcstoumax(const wchar_t *__restrict s, wchar_t **__restrict e, int b)
{ return wcstox(s, e, b, UINTMAX_MAX); }
