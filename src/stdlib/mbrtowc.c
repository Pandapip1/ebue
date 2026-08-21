/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* UTF-8 <-> UTF-16 conversion with state.  wchar_t is a 16-bit UTF-16
 * code unit, so a 4-byte UTF-8 sequence yields two wchar_t: mbrtowc
 * returns the high surrogate and consumes the bytes, remembering the low
 * surrogate in the state (__opaque2) to hand back on the next call for
 * zero bytes consumed... except POSIX requires a nonzero return for a
 * nonzero character, so that call consumes nothing and returns (size_t)-3,
 * the same convention glibc/Windows use for "output from state alone".
 *
 * Partial sequences are kept in __opaque1 as (pending code point << 8)
 * | (bytes still needed << 4) | (bytes seen) -- musl-style.
 *
 * wcrtomb likewise remembers a high surrogate in __opaque1 and writes
 * the 4-byte sequence when the low surrogate arrives. */
#include <wchar.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define SURR_HI(c) ((c) >= 0xd800 && (c) < 0xdc00)
#define SURR_LO(c) ((c) >= 0xdc00 && (c) < 0xe000)

size_t __ctype_get_mb_cur_max(void) { return 4; }

int mbsinit(const mbstate_t *st) { return !st || (!st->__opaque1 && !st->__opaque2); }

size_t mbrtowc(wchar_t *__restrict wc, const char *__restrict s, size_t n, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	const unsigned char *p = (const unsigned char *)s;
	unsigned c, cp, need, seen;
	size_t used = 0;
	wchar_t dummy;

	if (!st) st = &internal;
	if (!wc) wc = &dummy;
	if (!s) { s = ""; n = 1; wc = &dummy; p = (const unsigned char *)s; }

	if (st->__opaque2) {       /* a pending low surrogate */
		*wc = (wchar_t)st->__opaque2;
		st->__opaque2 = 0;
		return (size_t)-3;
	}
	if (!n) return (size_t)-2;

	if (st->__opaque1) {
		cp = st->__opaque1 >> 8;
		need = (st->__opaque1 >> 4) & 0xf;
		seen = st->__opaque1 & 0xf;
	} else {
		c = *p++; used = 1;
		if (c < 0x80) { *wc = (wchar_t)c; return c ? 1 : 0; }
		if (c < 0xc2 || c > 0xf4) goto ilseq;
		if (c < 0xe0) { cp = c & 0x1f; need = 1; }
		else if (c < 0xf0) { cp = c & 0x0f; need = 2; }
		else { cp = c & 0x07; need = 3; }
		seen = 1;
	}
	while (need) {
		if (used >= n) {
			st->__opaque1 = (cp << 8) | (need << 4) | seen;
			return (size_t)-2;
		}
		c = *p++; used++;
		if ((c & 0xc0) != 0x80) goto ilseq;
		cp = (cp << 6) | (c & 0x3f);
		need--; seen++;
		/* reject overlongs / surrogates / out of range as early as the
		 * second byte so an invalid prefix is not kept as state. */
		if (seen == 2) {
			unsigned total = need + 2;
			if ((total == 3 && cp < 0x20) || (total == 4 && (cp < 0x10 || cp > 0x10f))) goto ilseq;
		}
	}
	st->__opaque1 = 0;
	if (cp >= 0xd800 && cp < 0xe000) goto ilseq;
	if (cp >= 0x10000) {
		cp -= 0x10000;
		*wc = (wchar_t)(0xd800 + (cp >> 10));
		st->__opaque2 = 0xdc00 + (cp & 0x3ff);
	} else {
		*wc = (wchar_t)cp;
	}
	return used;
ilseq:
	st->__opaque1 = 0;
	errno = EILSEQ;
	return (size_t)-1;
}

size_t mbrlen(const char *__restrict s, size_t n, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	return mbrtowc(0, s, n, st ? st : &internal);
}

size_t wcrtomb(char *__restrict s, wchar_t wc, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	unsigned c = wc, cp;
	if (!st) st = &internal;
	if (!s) { st->__opaque1 = 0; return 1; }
	if (st->__opaque1) {
		unsigned hi = st->__opaque1;
		st->__opaque1 = 0;
		if (!SURR_LO(c)) { errno = EILSEQ; return (size_t)-1; }
		cp = 0x10000 + ((hi - 0xd800) << 10) + (c - 0xdc00);
		s[0] = (char)(0xf0 | (cp >> 18));
		s[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
		s[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
		s[3] = (char)(0x80 | (cp & 0x3f));
		return 4;
	}
	if (SURR_HI(c)) { st->__opaque1 = c; return 0; }
	if (SURR_LO(c)) { errno = EILSEQ; return (size_t)-1; }
	if (c < 0x80) { s[0] = (char)c; return 1; }
	if (c < 0x800) { s[0] = (char)(0xc0 | (c >> 6)); s[1] = (char)(0x80 | (c & 0x3f)); return 2; }
	s[0] = (char)(0xe0 | (c >> 12)); s[1] = (char)(0x80 | ((c >> 6) & 0x3f)); s[2] = (char)(0x80 | (c & 0x3f));
	return 3;
}

size_t mbsrtowcs(wchar_t *__restrict ws, const char **__restrict src, size_t n, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	const char *s = *src;
	size_t out = 0, r;
	wchar_t wc;

	if (!st) st = &internal;
	for (;;) {
		if (ws && out >= n) break;
		r = mbrtowc(&wc, s, 4, st);
		if (r == (size_t)-1) { if (ws) *src = s; return (size_t)-1; }
		if (r == (size_t)-2) { /* NUL inside a sequence: mbrtowc stops at it as ilseq */
			errno = EILSEQ; if (ws) *src = s; return (size_t)-1;
		}
		if (r == (size_t)-3) r = 0;
		if (ws) ws[out] = wc;
		if (r && !wc) { if (ws) *src = 0; return out; }
		if (!r && !wc) { if (ws) *src = 0; return out; }
		s += r;
		out++;
	}
	if (ws) *src = s;
	return out;
}

size_t wcsrtombs(char *__restrict s, const wchar_t **__restrict src, size_t n, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	const wchar_t *ws = *src;
	char buf[4];
	size_t out = 0, r;

	if (!st) st = &internal;
	for (;;) {
		/* a high surrogate followed by its low one is 4 bytes; peek so
		 * that we never write a partial character. */
		if (SURR_HI(ws[0]) && SURR_LO(ws[1])) {
			if (s && out + 4 > n) break;
			wcrtomb(buf, ws[0], st);
			wcrtomb(buf, ws[1], st);
			if (s) memcpy(s + out, buf, 4);
			out += 4; ws += 2;
			continue;
		}
		r = wcrtomb(buf, ws[0], st);
		if (r == (size_t)-1 || r == 0) { st->__opaque1 = 0; if (s) *src = ws; errno = EILSEQ; return (size_t)-1; }
		if (s && out + r > n) break;
		if (s) memcpy(s + out, buf, r);
		if (!ws[0]) { if (s) *src = 0; return out; }
		out += r; ws++;
	}
	if (s) *src = ws;
	return out;
}

wint_t btowc(int c)
{
	unsigned char b = (unsigned char)c;
	if (c == -1) return WEOF;
	return b < 0x80 ? b : WEOF;
}

int wctob(wint_t c)
{
	return c < 0x80 ? (int)c : -1;
}
