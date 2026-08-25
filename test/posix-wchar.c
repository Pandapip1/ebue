/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <wchar.h> and the multibyte
 * conversion functions.  Each block cites the page it was checked
 * against under https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 *
 * The decisive fact that shapes almost every test below: ntlibc's
 * wchar_t is a 16-bit UTF-16 code unit (WCHAR_MAX == 0xffff, see
 * include/wchar.h), not the 32-bit-holds-one-codepoint wchar_t POSIX's
 * text implicitly assumes elsewhere (glibc, most Issue 7 systems).  A
 * code point above U+FFFF is therefore two wchar_t (a surrogate pair),
 * and several clauses -- mbrtowc's return-value contract chief among
 * them -- cannot be met to the letter for such a character.  Every
 * place that happens is called out in a comment beside the assertion
 * and mirrored in test/posix-coverage/wchar.md; nothing is silently
 * skipped.
 *
 * Below the implemented-function tests, every wchar.h-synopsis function
 * (per https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/wchar.h.html)
 * that grep confirms is absent from include/wchar.h and unimplemented in
 * src/string or src/stdlib gets its own #if 0-fenced test carrying real
 * assertions, per the project's "every specified behaviour needs a test,
 * even one that cannot pass" standard.  Three fence tags are used:
 *   BUG:     a real spec violation in an *implemented* function.
 *   N/A:     genuinely impossible under a 16-bit wchar_t -- reserved for
 *            clauses that structurally require a single wchar_t to carry
 *            one whole character.  None of the missing functions below
 *            are unconditionally impossible; wcwidth()/wcswidth() are
 *            the one case where a clause is impossible for non-BMP input
 *            specifically while remaining fully testable/implementable
 *            for U+0000-U+FFFF, so that block splits UNIMPL (BMP) from
 *            N/A (the true column width of a character split across a
 *            surrogate pair, since wcwidth() is only ever handed one
 *            code unit at a time and can never see its partner).
 *   UNIMPL:  absent, but nothing about UTF-16 prevents implementing it.
 * Confirmed absent (grep of include/wchar.h + src/string + src/stdlib,
 * 2026-08-24): fgetwc, fgetws, fputwc, fputws, fwide, getwc, getwchar,
 * putwc, putwchar, ungetwc, fwprintf, fwscanf, swprintf, swscanf,
 * vfwprintf, vfwscanf, vswprintf, vswscanf, vwprintf, vwscanf, wprintf,
 * wscanf, open_wmemstream, wcwidth, wcswidth,
 * wcstod, wcstof, wcstold.  Confirmed *present*
 * (implemented, tested above): wcscpy, wcsncpy, wcscat, wcsncat, wcscmp,
 * wcsncmp, wcschr, wcsrchr, wcslen, wmemcpy, wmemmove, wmemset, wmemcmp,
 * wmemchr, btowc, wctob, mbsinit, mbrtowc, wcrtomb, mbrlen, mbsrtowcs,
 * wcsrtombs, plus the stdlib.h mbtowc/wctomb/mblen/mbstowcs/wcstombs and
 * inttypes.h wcstoimax/wcstoumax already covered.  Newly present as of
 * 2026-08-24 (src/string/wcsstr.c, wcsspn.c, wcstok.c, wcsdup.c,
 * wcsnlen.c, wcpcpy.c, wcscasecmp.c, and src/stdlib/wcstol.c), tested
 * below rather than fenced: wcsstr, wcspbrk, wcscspn, wcsspn, wcstok,
 * wcsdup, wcsnlen, wcpcpy, wcpncpy, wcscasecmp, wcscasecmp_l,
 * wcsncasecmp, wcsncasecmp_l, wcstol, wcstoll, wcstoul, wcstoull, and
 * (src/string/wcscoll.c, wcsxfrm.c) wcscoll, wcscoll_l, wcsxfrm,
 * wcsxfrm_l, plus (src/stdlib/mbrtowc.c) mbsnrtowcs and wcsnrtombs and
 * (src/time/wcsftime.c) wcsftime.  Also now present, as
 * of the new include/wctype.h (2026-08-23): iswalnum/iswalpha/iswblank/
 * iswcntrl/iswdigit/iswgraph/iswlower/iswprint/iswpunct/iswspace/
 * iswupper/iswxdigit, iswctype, wctype, towlower, towupper, wctrans,
 * towctrans.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <locale.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* A wide-character literal (L"...") is a compiler intrinsic whose element
 * type is the *compiler's own* wchar_t, not this header's typedef -- on
 * the native host that make asan builds against, that is a 4-byte int,
 * while include/wchar.h's wchar_t is `unsigned short` (see WCHAR_MAX
 * above).  Passing an L"..." string to a wcs*() function compiled
 * against this header is therefore a silent type mismatch: the target
 * build (tcc, whose own wchar_t matches the header) gets it right by
 * accident, but the native asan build reads every other unit as the
 * high half of a 4-byte int (0 for any ASCII content), corrupting
 * anything past the first character.  All test content below is
 * plain 7-bit ASCII, so W() rebuilds it a `wchar_t` (the header's,
 * whichever width that is) at a time instead of ever writing L"...". */
static const wchar_t *W(const char *s)
{
	static wchar_t pool[8][32];
	static int slot;
	wchar_t *b = pool[slot++ % 8];
	size_t i;
	for (i = 0; s[i] && i < 31; i++) b[i] = (wchar_t)(unsigned char)s[i];
	b[i] = 0;
	return b;
}

/* ---------------------------------------------------------------------
 * wcslen / wcscpy / wcsncpy / wcscat / wcsncat / wcscmp / wcsncmp /
 * wcschr / wcsrchr -- wcslen.html, wcscpy.html, wcsncpy.html,
 * wcscat.html, wcsncat.html, wcscmp.html, wcsncmp.html, wcschr.html
 * ------------------------------------------------------------------- */

static void test_wcslen(void)
{
	/* "shall compute the number of wide-character codes ... not
	 * including the terminating null wide-character code." */
	CHECK(wcslen(W("")) == 0);
	CHECK(wcslen(W("abc")) == 3);
}

static void test_wcscpy(void)
{
	wchar_t buf[8];
	/* "copy ... (including the terminating null ...) ... return ws1." */
	CHECK(wcscpy(buf, W("abc")) == buf);
	CHECK(!wcscmp(buf, W("abc")));
}

static void test_wcsncpy(void)
{
	wchar_t buf[8];
	/* "If the array pointed to by ws2 is a wide-character string that
	 * is shorter than n wide-character codes, null wide-character
	 * codes shall be appended ... until n wide-character codes in all
	 * are written." + "wcsncpy() function shall return ws1." */
	wmemset(buf, L'z', 8);
	CHECK(wcsncpy(buf, W("ab"), 5) == buf);
	CHECK(buf[0] == L'a' && buf[1] == L'b');
	CHECK(buf[2] == 0 && buf[3] == 0 && buf[4] == 0);	/* padded to n */
	CHECK(buf[5] == L'z');					/* untouched past n */

	/* source at least as long as n: no NUL appended, only n copied */
	wmemset(buf, L'z', 8);
	wcsncpy(buf, W("abcdef"), 3);
	CHECK(buf[0] == L'a' && buf[1] == L'b' && buf[2] == L'c' && buf[3] == L'z');
}

static void test_wcscat(void)
{
	wchar_t buf[8];
	/* "append ... (including the terminating null ...) ... The initial
	 * wide-character code of ws2 shall overwrite the null ... at the
	 * end of ws1." + "shall return ws1." */
	wcscpy(buf, W("ab"));
	CHECK(wcscat(buf, W("cd")) == buf);
	CHECK(!wcscmp(buf, W("abcd")));
}

static void test_wcsncat(void)
{
	wchar_t buf[8];
	/* "append not more than n ... A terminating null wide-character
	 * code shall always be appended to the result." + "return ws1." */
	wcscpy(buf, W("ab"));
	CHECK(wcsncat(buf, W("cdef"), 2) == buf);
	CHECK(!wcscmp(buf, W("abcd")));		/* only 2 chars appended */

	wcscpy(buf, W("ab"));
	wcsncat(buf, W("c"), 5);			/* source shorter than n: stops at NUL */
	CHECK(!wcscmp(buf, W("abc")));
}

static void test_wcscmp(void)
{
	/* "sign ... determined by the sign of the difference between the
	 * values of the first pair of wide-character codes that differ." */
	CHECK(wcscmp(W("abc"), W("abc")) == 0);
	CHECK(wcscmp(W("abc"), W("abd")) < 0);
	CHECK(wcscmp(W("abd"), W("abc")) > 0);
	CHECK(wcscmp(W("ab"), W("abc")) < 0);
	/* first-differing-code magnitude, not lexical distance */
	{
		wchar_t a[] = { 'a', 0xffff, 0 };
		CHECK(wcscmp(a, W("ab")) > 0);
	}
}

static void test_wcsncmp(void)
{
	/* "compare not more than n ... codes that follow a null ... are
	 * not compared." */
	CHECK(wcsncmp(W("abcX"), W("abcY"), 3) == 0);
	CHECK(wcsncmp(W("abcX"), W("abcY"), 4) < 0);
	{
		wchar_t a[] = { 'a', 'b', 0, 'c', 'd' };
		wchar_t b[] = { 'a', 'b', 0, 'z', 'z' };
		CHECK(wcsncmp(a, b, 5) == 0);	/* stops at NUL though n=5 */
	}
}

static void test_wcschr(void)
{
	wchar_t s[] = { 'a', 'b', 'c', 0 };
	/* "terminating null wide-character code is considered to be part
	 * of the wide-character string." */
	CHECK(wcschr(s, L'b') == s + 1);
	CHECK(wcschr(s, L'z') == 0);
	CHECK(wcschr(s, 0) == s + 3);
}

static void test_wcsrchr(void)
{
	wchar_t s[] = { 'a', 'b', 'c', 'a', 'b', 'c', 0 };
	/* last occurrence; NUL considered part of the string too. */
	CHECK(wcsrchr(s, L'a') == s + 3);
	CHECK(wcsrchr(s, L'z') == 0);
	CHECK(wcsrchr(s, 0) == s + 6);
}

/* ---------------------------------------------------------------------
 * wmemchr / wmemcmp / wmemcpy / wmemmove / wmemset -- wmemchr.html,
 * wmemcmp.html, wmemcpy.html, wmemmove.html, wmemset.html
 * ------------------------------------------------------------------- */

static void test_wmemchr(void)
{
	wchar_t s[] = { L'a', L'b', 0, L'c' };
	/* "not affected by locale ... null wide character ... not treated
	 * specially." -- a NUL in the middle is just another value. */
	CHECK(wmemchr(s, L'c', 4) == s + 3);
	CHECK(wmemchr(s, 0, 4) == s + 2);
	CHECK(wmemchr(s, L'z', 4) == 0);
	CHECK(wmemchr(s, L'a', 0) == 0);	/* n==0: never found */
}

static void test_wmemcmp(void)
{
	wchar_t a[] = { 1, 2, 3 }, b[] = { 1, 2, 4 };
	CHECK(wmemcmp(a, a, 3) == 0);
	CHECK(wmemcmp(a, b, 2) == 0);
	CHECK(wmemcmp(a, b, 3) < 0);
	CHECK(wmemcmp(b, a, 3) > 0);
	CHECK(wmemcmp(a, b, 0) == 0);
	/* unsigned wchar_t: 0xffff compares greater than 1, not as -1 */
	{
		wchar_t big[] = { 0xffff }, small[] = { 1 };
		CHECK(wmemcmp(big, small, 1) > 0);
	}
}

static void test_wmemcpy(void)
{
	wchar_t src[4] = { L'a', L'b', L'c', L'd' }, dst[4];
	CHECK(wmemcpy(dst, src, 4) == dst);
	CHECK(!wmemcmp(dst, src, 4));
	CHECK(wmemcpy(dst, src, 0) == dst);	/* n==0: valid pointers, no copy */
}

static void test_wmemmove(void)
{
	wchar_t buf[6] = { L'a', L'b', L'c', L'd', L'e', L'f' };
	/* overlap defined: as if via a temporary array */
	CHECK(wmemmove(buf + 1, buf, 4) == buf + 1);
	CHECK(buf[1] == L'a' && buf[2] == L'b' && buf[3] == L'c' && buf[4] == L'd');

	{
		wchar_t buf2[6] = { L'a', L'b', L'c', L'd', L'e', L'f' };
		wmemmove(buf2, buf2 + 1, 4);
		CHECK(buf2[0] == L'b' && buf2[1] == L'c' && buf2[2] == L'd' && buf2[3] == L'e');
	}
}

static void test_wmemset(void)
{
	wchar_t buf[5] = { 1, 1, 1, 1, 1 };
	CHECK(wmemset(buf, L'x', 3) == buf);
	CHECK(buf[0] == L'x' && buf[1] == L'x' && buf[2] == L'x' && buf[3] == 1 && buf[4] == 1);
	CHECK(wmemset(buf, L'y', 0) == buf);
}

/* ---------------------------------------------------------------------
 * mbsinit -- mbsinit.html
 * ------------------------------------------------------------------- */

static void test_mbsinit(void)
{
	mbstate_t st;
	/* "return non-zero if ps is a null pointer, or if the pointed-to
	 * object describes an initial conversion state; otherwise ... 0." */
	CHECK(mbsinit(0) != 0);
	memset(&st, 0, sizeof st);
	CHECK(mbsinit(&st) != 0);
	st.__opaque1 = 1;		/* mid-sequence: not initial */
	CHECK(mbsinit(&st) == 0);
	memset(&st, 0, sizeof st);
	st.__opaque2 = 0xdc00;		/* pending low surrogate: not initial */
	CHECK(mbsinit(&st) == 0);
}

/* ---------------------------------------------------------------------
 * mbrtowc -- mbrtowc.html
 * ------------------------------------------------------------------- */

static void test_mbrtowc_basic(void)
{
	mbstate_t st;
	wchar_t wc;

	/* 0: "next n or fewer bytes complete a null wide character" */
	memset(&st, 0, sizeof st);
	CHECK(mbrtowc(&wc, "", 1, &st) == 0);
	CHECK(wc == 0);

	/* 1..n: valid character, byte count returned */
	memset(&st, 0, sizeof st);
	CHECK(mbrtowc(&wc, "A", 1, &st) == 1 && wc == L'A');
	memset(&st, 0, sizeof st);
	CHECK(mbrtowc(&wc, "\xc3\xa9", 2, &st) == 2 && wc == 0xe9);	/* U+00E9 e-acute */
	memset(&st, 0, sizeof st);
	CHECK(mbrtowc(&wc, "\xe4\xb8\xad", 3, &st) == 3 && wc == 0x4e2d);	/* U+4E2D BMP */

	/* -2: incomplete but potentially valid sequence */
	memset(&st, 0, sizeof st);
	CHECK(mbrtowc(&wc, "\xe4\xb8", 2, &st) == (size_t)-2);
	CHECK(mbsinit(&st) == 0);	/* state remembers the partial sequence */

	/* -1 / EILSEQ: invalid sequence */
	memset(&st, 0, sizeof st);
	errno = 0;
	CHECK(mbrtowc(&wc, "\xff", 1, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);

	/* s == NULL behaves as mbrtowc(NULL, "", 1, ps) */
	memset(&st, 0, sizeof st);
	CHECK(mbrtowc(&wc, 0, 1, &st) == 0);
	CHECK(mbsinit(&st) != 0);

	/* "shall not modify errno upon successful execution" */
	memset(&st, 0, sizeof st);
	errno = 0x1234;
	CHECK(mbrtowc(&wc, "A", 1, &st) == 1);
	CHECK(errno == 0x1234);
}

static void test_mbrtowc_overlong_and_range(void)
{
	mbstate_t st;
	wchar_t wc;

	/* overlong 3-byte encoding of U+0000 (E0 80 80): must be rejected,
	 * not accepted as a "shortest form" violation is silently allowed. */
	memset(&st, 0, sizeof st);
	errno = 0;
	CHECK(mbrtowc(&wc, "\xe0\x80\x80", 3, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);

	/* overlong 4-byte encoding (F0 80 80 80) */
	memset(&st, 0, sizeof st);
	errno = 0;
	CHECK(mbrtowc(&wc, "\xf0\x80\x80\x80", 4, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);

	/* UTF-8 encoding of a surrogate code point (U+D800, ED A0 80):
	 * surrogates are not scalar values and must be rejected. */
	memset(&st, 0, sizeof st);
	errno = 0;
	CHECK(mbrtowc(&wc, "\xed\xa0\x80", 3, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);

	/* codepoint above U+10FFFF (F4 90 80 80 = U+110000): out of range. */
	memset(&st, 0, sizeof st);
	errno = 0;
	CHECK(mbrtowc(&wc, "\xf4\x90\x80\x80", 4, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);

	/* the largest valid codepoint, U+10FFFF (F4 8F BF BF), is accepted
	 * -- exercised below together with the surrogate-pair divergence. */
}

/* This is the central 16-bit-wchar_t divergence.  POSIX's mbrtowc
 * return-value contract is exactly four outcomes: 0, a byte count in
 * [1,n], (size_t)-1 (EILSEQ), or (size_t)-2 (incomplete).  A code point
 * above U+FFFF -- e.g. U+10FFFF, encoded as 4 UTF-8 bytes -- cannot be
 * represented in one 16-bit wchar_t, so ntlibc returns the high
 * surrogate from the call that consumes the 4 bytes, and on the very
 * next call (even though the caller supplies no new bytes and *n* may
 * be 0) hands back the low surrogate through a FIFTH return value,
 * (size_t)-3, consuming 0 bytes.  (size_t)-3 is not one of the values
 * POSIX allows mbrtowc() to return; a strictly conforming caller that
 * switches on {0, -1, -2} and treats "positive" as the only other case
 * will misinterpret -3 as an enormous byte count.  This is a deliberate
 * accommodation documented in src/stdlib/mbrtowc.c's header comment,
 * not an oversight, but it is a real non-conformance and is recorded
 * here rather than asserted away. */
static void test_mbrtowc_surrogate_pair_divergence(void)
{
	mbstate_t st;
	wchar_t wc;
	size_t r;

	memset(&st, 0, sizeof st);
	wc = 0;
	r = mbrtowc(&wc, "\xf4\x8f\xbf\xbf", 4, &st);	/* U+10FFFF */
	CHECK(r == 4);					/* bytes consumed: in-contract */
	CHECK(wc == 0xdbff);				/* high surrogate: NOT a POSIX outcome */
	CHECK(mbsinit(&st) == 0);			/* low surrogate still pending */

	r = mbrtowc(&wc, "", 0, &st);			/* n==0, no input bytes at all */
	CHECK(r == (size_t)-3);			/* divergent 5th return value */
	CHECK(wc == 0xdfff);				/* low surrogate delivered here */
	CHECK(mbsinit(&st) != 0);			/* state now initial again */
}

/* ---------------------------------------------------------------------
 * wcrtomb -- wcrtomb.html
 * ------------------------------------------------------------------- */

static void test_wcrtomb_basic(void)
{
	mbstate_t st;
	char buf[4];

	/* "number of bytes stored ... including any shift sequences." */
	memset(&st, 0, sizeof st);
	CHECK(wcrtomb(buf, L'A', &st) == 1 && buf[0] == 'A');
	memset(&st, 0, sizeof st);
	CHECK(wcrtomb(buf, 0xe9, &st) == 2);
	memset(&st, 0, sizeof st);
	CHECK(wcrtomb(buf, 0x4e2d, &st) == 3);

	/* null wide character: "a null byte shall be stored" */
	memset(&st, 0, sizeof st);
	CHECK(wcrtomb(buf, 0, &st) == 1 && buf[0] == 0);

	/* s == NULL: equivalent to wcrtomb(buf, L'\0', ps) into an internal
	 * buffer -- observable effect is the return value only. */
	memset(&st, 0, sizeof st);
	CHECK(wcrtomb(0, L'A', &st) == 1);

	/* "shall not change errno if successful." */
	memset(&st, 0, sizeof st);
	errno = 0x1234;
	CHECK(wcrtomb(buf, L'A', &st) == 1);
	CHECK(errno == 0x1234);

	/* unpaired low surrogate (0xdc00-0xdfff, not preceded by a high
	 * surrogate): not a valid scalar value on its own -> EILSEQ. */
	memset(&st, 0, sizeof st);
	errno = 0;
	CHECK(wcrtomb(buf, 0xdc00, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);
}

/* Divergence mirroring test_mbrtowc_surrogate_pair_divergence: a lone
 * high surrogate (0xd800-0xdbff) is, by itself, "an invalid
 * wide-character code" per wcrtomb.html's RETURN VALUE clause ("When wc
 * is not a valid wide character, ... errno ... [EILSEQ] and returns
 * (size_t)-1"). ntlibc instead treats it as the first half of a
 * surrogate pair spanning two calls: it stashes the high surrogate in
 * *ps and returns 0 -- a return value POSIX never assigns any meaning
 * to (0 is not documented as "waiting for more input" anywhere in this
 * function's contract, unlike mbrtowc's incomplete-sequence -2). Only
 * if the *next* wcrtomb() call is given the matching low surrogate does
 * a real 4-byte UTF-8 sequence get emitted. */
static void test_wcrtomb_surrogate_pair_divergence(void)
{
	mbstate_t st;
	char buf[4];
	size_t r;

	memset(&st, 0, sizeof st);
	r = wcrtomb(buf, 0xdbff, &st);		/* high surrogate of U+10FFFF */
	CHECK(r == 0);				/* not a POSIX-documented outcome */
	CHECK(mbsinit(&st) == 0);		/* high surrogate remembered */

	r = wcrtomb(buf, 0xdfff, &st);		/* matching low surrogate */
	CHECK(r == 4);
	CHECK((unsigned char)buf[0] == 0xf4 && (unsigned char)buf[1] == 0x8f
	   && (unsigned char)buf[2] == 0xbf && (unsigned char)buf[3] == 0xbf);
	CHECK(mbsinit(&st) != 0);

	/* a high surrogate followed by something other than its low
	 * surrogate: "the conversion state becomes undefined" on error is
	 * the only applicable clause; ntlibc surfaces EILSEQ, which is a
	 * reasonable reading but the pending-high-surrogate state itself is
	 * not something POSIX's wcrtomb ever has, so this whole path is
	 * outside the standard's contract. */
	memset(&st, 0, sizeof st);
	wcrtomb(buf, 0xd800, &st);
	errno = 0;
	CHECK(wcrtomb(buf, L'A', &st) == (size_t)-1);
	CHECK(errno == EILSEQ);
}

/* ---------------------------------------------------------------------
 * mbrlen -- mbrlen is documented on mbrtowc.html's SEE ALSO web as
 * "equivalent to mbrtowc(NULL, s, n, ps ? ps : &internal)" (its own doc
 * page mbrlen.html states the same contract in different words); sanity
 * checked here for the byte-count/0/-1/-2 outcomes.
 * ------------------------------------------------------------------- */

static void test_mbrlen(void)
{
	mbstate_t st;
	memset(&st, 0, sizeof st);
	CHECK(mbrlen("A", 1, &st) == 1);
	memset(&st, 0, sizeof st);
	CHECK(mbrlen("", 1, &st) == 0);
	memset(&st, 0, sizeof st);
	CHECK(mbrlen("\xe4\xb8", 2, &st) == (size_t)-2);
}

/* ---------------------------------------------------------------------
 * mbtowc / wctomb -- mbtowc.html, wctomb.html
 * ------------------------------------------------------------------- */

static void test_mbtowc(void)
{
	wchar_t wc;

	/* s == NULL: "non-zero or 0 ... if character encodings ... do or
	 * do not have state-dependent encodings." UTF-8 is not
	 * state-dependent in the POSIX sense (no shift sequences), so 0. */
	CHECK(mbtowc(&wc, 0, 4) == 0);

	CHECK(mbtowc(&wc, "", 1) == 0 && wc == 0);
	CHECK(mbtowc(&wc, "A", 1) == 1 && wc == L'A');
	CHECK(mbtowc(&wc, "\xc3\xa9", 2) == 2 && wc == 0xe9);

	/* "In no case shall the value returned be greater than n." -- an
	 * incomplete sequence with insufficient n must fail (mbtowc has no
	 * -2 outcome of its own; -1 is all it can report). */
	CHECK(mbtowc(&wc, "\xe4\xb8", 1) == -1);

	errno = 0;
	CHECK(mbtowc(&wc, "\xff", 1) == -1);
	CHECK(errno == EILSEQ);

	/* wc may be NULL: still reports length, no crash */
	CHECK(mbtowc(0, "A", 1) == 1);
}

static void test_wctomb(void)
{
	char buf[4];

	/* s == NULL: same state-dependent-or-not contract as mbtowc */
	CHECK(wctomb(0, L'A') == 0);

	CHECK(wctomb(buf, L'A') == 1 && buf[0] == 'A');
	CHECK(wctomb(buf, 0xe9) == 2);
	CHECK(wctomb(buf, 0x4e2d) == 3);

	/* lone high surrogate: not a directly encodable scalar value in
	 * one call -- wctomb has no cross-call state to defer to, so this
	 * must fail; ntlibc reports -1/EILSEQ (see src/stdlib/mbtowc.c). */
	errno = 0;
	CHECK(wctomb(buf, 0xd800) == -1);
	CHECK(errno == EILSEQ);
}

static void test_mblen(void)
{
	/* "equivalent to a call to mbtowc((wchar_t *)0, s, n)" */
	CHECK(mblen(0, 4) == 0);
	CHECK(mblen("", 1) == 0);
	CHECK(mblen("A", 1) == 1);
	CHECK(mblen("\xc3\xa9", 2) == 2);
	errno = 0;
	CHECK(mblen("\xff", 1) == -1);
}

/* ---------------------------------------------------------------------
 * mbstowcs / wcstombs -- mbstowcs.html, wcstombs.html
 * ------------------------------------------------------------------- */

static void test_mbstowcs(void)
{
	wchar_t buf[8];

	/* "convert ... store not more than n wide-character codes";
	 * "return[s] the count of array elements modified ... excluding
	 * any terminating zero code." */
	CHECK(mbstowcs(buf, "abc", 8) == 3);
	CHECK(buf[0] == L'a' && buf[1] == L'b' && buf[2] == L'c' && buf[3] == 0);

	/* pwcs == NULL: length needed, nothing stored */
	CHECK(mbstowcs(0, "abc", 0) == 3);

	/* n limits storage; "array ... lacks zero-termination when the
	 * returned value equals n." */
	wmemset(buf, L'z', 8);
	CHECK(mbstowcs(buf, "abcdef", 3) == 3);
	CHECK(buf[0] == L'a' && buf[1] == L'b' && buf[2] == L'c' && buf[3] == L'z');

	/* EILSEQ on invalid sequence */
	errno = 0;
	CHECK(mbstowcs(buf, "\xff", 8) == (size_t)-1);
	CHECK(errno == EILSEQ);
}

static void test_wcstombs(void)
{
	char buf[8];

	CHECK(wcstombs(buf, W("abc"), 8) == 3);
	CHECK(!strcmp(buf, "abc"));

	/* s == NULL: length needed */
	CHECK(wcstombs(0, W("abc"), 0) == 3);

	/* "stopping if a character would exceed the limit of n total
	 * bytes" -- a 2-byte character must not be split. */
	memset(buf, 'z', 8);
	{
		wchar_t a[] = { 'a', 0xe9, 0 };
		CHECK(wcstombs(buf, a, 2) == 1);	/* 0xe9 needs 2 bytes, only 1 left */
	}
	CHECK(buf[0] == 'a' && buf[1] == 'z');

	/* invalid wide-character code -> EILSEQ.  0xd800 alone (an
	 * unpaired surrogate) is not a valid scalar value. */
	errno = 0;
	{
		wchar_t a[] = { 0xd800, 0 };
		CHECK(wcstombs(buf, a, 8) == (size_t)-1);
	}
	CHECK(errno == EILSEQ);
}

/* ---------------------------------------------------------------------
 * mbsrtowcs / wcsrtombs -- mbsrtowcs.html, wcsrtombs.html
 * ------------------------------------------------------------------- */

static void test_mbsrtowcs(void)
{
	mbstate_t st;
	wchar_t buf[8];
	char abc[] = "abc", abcdef[] = "abcdef", bad[] = "\xff";
	const char *src;

	memset(&st, 0, sizeof st);
	src = abc;
	CHECK(mbsrtowcs(buf, &src, 8, &st) == 3);
	CHECK(!wcscmp(buf, W("abc")));
	/* "assigned ... a null pointer (if conversion stopped due to
	 * reaching a terminating null character)" */
	CHECK(src == 0);

	/* len limits output; src assigned "the address just past the last
	 * character converted" */
	memset(&st, 0, sizeof st);
	src = abcdef;
	CHECK(mbsrtowcs(buf, &src, 3, &st) == 3);
	CHECK(src == abcdef + 3);

	/* dst == NULL: length needed, src untouched by the function's
	 * "if dst is not a null pointer" clauses -- ntlibc still advances
	 * its local copy but must not write through *src doesn't apply
	 * (src is read-modified only when ws is non-null per the man page;
	 * confirm the count is still right). */
	memset(&st, 0, sizeof st);
	src = abc;
	CHECK(mbsrtowcs(0, &src, 0, &st) == 3);

	/* EILSEQ */
	memset(&st, 0, sizeof st);
	src = bad;
	errno = 0;
	CHECK(mbsrtowcs(buf, &src, 8, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);
}

static void test_wcsrtombs(void)
{
	mbstate_t st;
	char buf[8];
	wchar_t abc[] = { 'a', 'b', 'c', 0 };
	wchar_t abcdef[] = { 'a', 'b', 'c', 'd', 'e', 'f', 0 };
	wchar_t bad[] = { 0xdc00, 0 };
	const wchar_t *src;

	memset(&st, 0, sizeof st);
	src = abc;
	CHECK(wcsrtombs(buf, &src, 8, &st) == 3);
	CHECK(!strcmp(buf, "abc"));
	CHECK(src == 0);

	memset(&st, 0, sizeof st);
	src = abcdef;
	CHECK(wcsrtombs(buf, &src, 3, &st) == 3);
	CHECK(src == abcdef + 3);

	/* dst == NULL: length needed */
	memset(&st, 0, sizeof st);
	src = abc;
	CHECK(wcsrtombs(0, &src, 0, &st) == 3);

	/* a surrogate pair that would need 4 bytes must not be split
	 * across the len boundary -- see wcsrtombs's peek-ahead in
	 * src/stdlib/mbrtowc.c. */
	memset(&st, 0, sizeof st);
	{
		wchar_t pair[3];
		pair[0] = 0xd800; pair[1] = 0xdc00; pair[2] = 0;
		src = pair;
		memset(buf, 'z', 8);
		CHECK(wcsrtombs(buf, &src, 3, &st) == 0);	/* 4 needed, 3 given: nothing stored */
		CHECK(src == pair);				/* no progress made */
	}

	errno = 0;
	memset(&st, 0, sizeof st);
	src = bad;	/* unpaired low surrogate */
	CHECK(wcsrtombs(buf, &src, 8, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);
}

/* ---------------------------------------------------------------------
 * btowc / wctob -- btowc.html, wctob.html
 * ------------------------------------------------------------------- */

/* btowc.html RETURN VALUE: "In the POSIX locale, btowc() shall not
 * return WEOF if c has a value in the range 0 to 255 inclusive." This
 * is the second major 16-bit/encoding divergence: ntlibc's only locale
 * is named "POSIX", but unlike the traditional single-byte-identity
 * POSIX locale, its multibyte encoding is UTF-8, under which bytes
 * 0x80-0xFF are never a complete one-byte character on their own (they
 * are lead or continuation bytes of a multibyte sequence). Honoring the
 * letter of this clause would mean claiming e.g. byte 0x80 is the
 * one-byte character U+0080 -- which contradicts every mbrtowc/EILSEQ
 * test above that correctly rejects a bare 0x80. ntlibc's btowc()
 * returns WEOF for 0x80-0xFF, which is the UTF-8-correct answer and the
 * one asserted below, but it does not satisfy this clause as literally
 * written for the POSIX locale name. Recorded, not fenced: changing it
 * to satisfy the letter of the clause would make it wrong for UTF-8. */
static void test_btowc(void)
{
	CHECK(btowc(-1) == WEOF);		/* c == EOF */
	CHECK(btowc('A') == L'A');
	CHECK(btowc(0) == 0);
	CHECK(btowc(0x80) == (wint_t)WEOF);	/* divergence: see comment above */
}

static void test_wctob(void)
{
	/* "EOF if c does not correspond to a character with length one in
	 * the initial shift state. Otherwise ... the single-byte
	 * representation ... as unsigned char converted to int." */
	CHECK(wctob(L'A') == 'A');
	CHECK(wctob(0) == 0);
	CHECK(wctob(0xe9) == EOF);		/* U+00E9 needs 2 UTF-8 bytes */
	CHECK(wctob(WEOF) == EOF);
}

/* ---------------------------------------------------------------------
 * wcstoimax / wcstoumax -- wcstoimax.html
 * ------------------------------------------------------------------- */

static void test_wcstoimax(void)
{
	wchar_t xyz[] = { 'x', 'y', 'z', 0 };
	wchar_t *end;

	/* "equivalent to wcstol()/wcstoll()/wcstoul()/wcstoull() ...
	 * converted to intmax_t/uintmax_t." */
	CHECK(wcstoimax(W("123"), 0, 10) == 123);
	CHECK(wcstoimax(W("-123"), 0, 10) == -123);
	CHECK(wcstoumax(W("123"), 0, 10) == 123);

	/* "If no conversion could be performed, zero shall be returned." */
	end = xyz + 1;
	CHECK(wcstoimax(xyz, &end, 10) == 0);
	CHECK(end == xyz);	/* endptr reset to nptr: nothing consumed */

	/* out of range -> {INTMAX_MAX,INTMAX_MIN,UINTMAX_MAX} + ERANGE */
	errno = 0;
	CHECK(wcstoimax(W("99999999999999999999999"), 0, 10) == INTMAX_MAX);
	CHECK(errno == ERANGE);

	errno = 0;
	CHECK(wcstoimax(W("-99999999999999999999999"), 0, 10) == INTMAX_MIN);
	CHECK(errno == ERANGE);

	errno = 0;
	CHECK(wcstoumax(W("99999999999999999999999"), 0, 10) == UINTMAX_MAX);
	CHECK(errno == ERANGE);

	/* EINVAL: unsupported base */
	errno = 0;
	CHECK(wcstoimax(W("1"), 0, 1) == 0);
	CHECK(errno == EINVAL);

	/* base 0 auto-detection: 0x prefix -> hex, leading 0 -> octal */
	CHECK(wcstoimax(W("0x1A"), 0, 0) == 26);
	CHECK(wcstoimax(W("017"), 0, 0) == 15);
}

/* =======================================================================
 * Below: every wchar.h-synopsis function absent from include/wchar.h and
 * unimplemented in src/, each with a real #if 0-fenced test.  None of
 * these are called from main(): they cannot compile against the current
 * header, which is the point -- absence itself is what is being tested
 * for by grep in the block comment above, and the fenced body is the
 * transcription of what a real implementation would have to satisfy.
 * ======================================================================= */

/* ---------------------------------------------------------------------
 * fgetwc / getwc / getwchar -- fgetwc.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: fgetwc()/getwc()/getwchar() -- fgetwc.html DESCRIPTION,
       * RETURN VALUE.  Implementable: reads one converted wide character
       * per call, no per-call knowledge of surrogate pairing is needed
       * (a caller assembling a supplementary character just calls twice,
       * exactly like the existing mbrtowc()-based UTF-8 decoder does). */
static void test_fgetwc(void)
{
	FILE *f = fopen("test.tmp", "w+");
	wint_t c;
	fputs("AB", f);
	rewind(f);
	/* "obtain the next character ... convert that to the corresponding
	 * wide-character code" */
	c = fgetwc(f);
	CHECK(c == L'A');
	CHECK(getwc(f) == L'B');
	/* "the end-of-file indicator for the stream shall be set and
	 * fgetwc() shall return WEOF." */
	CHECK(fgetwc(f) == WEOF);
	CHECK(feof(f));
	fclose(f);
	remove("test.tmp");
}
#endif

/* ---------------------------------------------------------------------
 * fputwc / putwc / putwchar -- fputwc.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: fputwc()/putwc()/putwchar() -- fputwc.html DESCRIPTION,
       * RETURN VALUE, ERRORS.  Implementable per code unit; a caller
       * writing a supplementary character just calls it twice with the
       * two surrogate halves, same shape as wcrtomb()'s existing
       * pending-high-surrogate state machine. */
static void test_fputwc(void)
{
	FILE *f = fopen("test.tmp", "w+");
	/* "Upon successful completion, fputwc() shall return wc." */
	CHECK(fputwc(L'A', f) == L'A');
	CHECK(putwc(L'B', f) == L'B');
	rewind(f);
	CHECK(fgetc(f) == 'A');
	CHECK(fgetc(f) == 'B');
	fclose(f);
	remove("test.tmp");
}
#endif

/* ---------------------------------------------------------------------
 * fgetws -- fgetws.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: fgetws() -- fgetws.html DESCRIPTION, RETURN VALUE.
       * Implementable: reads code units (not codepoints) up to n-1,
       * newline, or EOF, and NUL-terminates -- exactly wcsncpy's unit
       * granularity, no surrogate awareness required. */
static void test_fgetws(void)
{
	FILE *f = fopen("test.tmp", "w+");
	wchar_t buf[8];
	fputs("abc\ndef\n", f);
	rewind(f);
	/* "read characters ... until n-1 characters are read, or a
	 * <newline> is read ... The wide-character string ... shall then
	 * be terminated with a null wide-character code." */
	CHECK(fgetws(buf, 8, f) == buf);
	CHECK(!wcscmp(buf, W("abc\n")));
	/* "the end-of-file indicator ... shall be set and fgetws() shall
	 * return a null pointer" once at EOF with nothing read. */
	fseek(f, 0, SEEK_END);
	CHECK(fgetws(buf, 8, f) == 0);
	fclose(f);
	remove("test.tmp");
}
#endif

/* ---------------------------------------------------------------------
 * fputws -- fputws.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: fputws() -- fputws.html DESCRIPTION, RETURN VALUE.
       * Implementable per code unit, terminating NUL not written. */
static void test_fputws(void)
{
	FILE *f = fopen("test.tmp", "w+");
	/* "Upon successful completion, fputws() shall return a
	 * non-negative number." */
	CHECK(fputws(W("abc"), f) >= 0);
	rewind(f);
	CHECK(fgetc(f) == 'a' && fgetc(f) == 'b' && fgetc(f) == 'c');
	CHECK(fgetc(f) == EOF);	/* no terminating NUL byte written */
	fclose(f);
	remove("test.tmp");
}
#endif

/* ---------------------------------------------------------------------
 * ungetwc -- ungetwc.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: ungetwc() -- ungetwc.html DESCRIPTION, RETURN VALUE.
       * Implementable as a one-wchar_t pushback slot per stream, same
       * granularity as fgetwc(); pushing back one half of a surrogate
       * pair is no different from pushing back any other code unit. */
static void test_ungetwc(void)
{
	FILE *f = fopen("test.tmp", "w+");
	fputs("A", f);
	rewind(f);
	CHECK(fgetwc(f) == L'A');
	/* "ungetwc() shall return the wide-character code corresponding
	 * to the pushed-back character." */
	CHECK(ungetwc(L'A', f) == L'A');
	CHECK(fgetwc(f) == L'A');
	/* "If wc is WEOF, the operation shall fail and the input stream
	 * shall be left unchanged." */
	CHECK(ungetwc(WEOF, f) == WEOF);
	fclose(f);
	remove("test.tmp");
}
#endif

/* ---------------------------------------------------------------------
 * fwide -- fwide.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: fwide() -- fwide.html DESCRIPTION, RETURN VALUE.
       * Implementable as one extra int per FILE; no wchar_t-width
       * dependency at all. */
static void test_fwide(void)
{
	FILE *f = fopen("test.tmp", "w+");
	/* mode > 0 requests wide orientation */
	CHECK(fwide(f, 1) > 0);
	/* "If the orientation of the stream has already been determined,
	 * fwide() shall not change it." -- a later negative request must
	 * still report wide. */
	CHECK(fwide(f, -1) > 0);
	fclose(f);
	remove("test.tmp");
}
#endif

/* ---------------------------------------------------------------------
 * fwprintf / wprintf / swprintf (+ v-variants) -- fwprintf.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: fwprintf()/wprintf()/swprintf()/vfwprintf()/vwprintf()/
       * vswprintf() -- fwprintf.html DESCRIPTION, RETURN VALUE.
       * Implementable: format-directive processing is over ASCII
       * conversion characters, argument values go through the same
       * printf core already used by the byte-string family; no
       * surrogate-pair barrier since %lc/%ls just copy wchar_t units. */
static void test_fwprintf(void)
{
	wchar_t buf[8];
	/* "the count of wide characters transmitted (excluding swprintf's
	 * terminating null)." */
	CHECK(swprintf(buf, 8, W("%d"), 12) == 2);
	CHECK(!wcscmp(buf, W("12")));
	/* "If n or more wide characters were requested to be written,
	 * swprintf() shall return a negative value, and set errno." */
	CHECK(swprintf(buf, 2, W("%d"), 12345) < 0);
}
#endif

/* ---------------------------------------------------------------------
 * fwscanf / wscanf / swscanf (+ v-variants) -- fwscanf.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: fwscanf()/wscanf()/swscanf()/vfwscanf()/vwscanf()/
       * vswscanf() -- fwscanf.html DESCRIPTION, RETURN VALUE.
       * Implementable on the same scanf core as the byte-string family;
       * %lc/%ls just move wchar_t units. */
static void test_fwscanf(void)
{
	int n = 0;
	/* "the number of successfully matched and assigned input items." */
	CHECK(swscanf(W("42"), W("%d"), &n) == 1);
	CHECK(n == 42);
	/* EOF before any conversion. */
	CHECK(swscanf(W(""), W("%d"), &n) == WEOF);
}
#endif

/* ---------------------------------------------------------------------
 * open_wmemstream -- open_wmemstream.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: open_wmemstream() -- open_wmemstream.html DESCRIPTION,
       * RETURN VALUE, ERRORS.  Implementable: a growable wchar_t buffer
       * behind a FILE*, no per-codepoint reasoning involved. */
static void test_open_wmemstream(void)
{
	wchar_t *buf;
	size_t len;
	FILE *f = open_wmemstream(&buf, &len);
	/* "Upon successful completion ... a pointer to the object
	 * controlling the stream." */
	CHECK(f != 0);
	fputws(W("hi"), f);
	fflush(f);
	/* "*bufp shall point to a wchar_t array ... and sizep shall
	 * point to the number of wide characters ... at the file
	 * position." */
	CHECK(len == 2);
	CHECK(!wcscmp(buf, W("hi")));
	fclose(f);
	free(buf);
}
#endif

/* ---------------------------------------------------------------------
 * isw*() classification family -- iswalpha.html
 * <wctype.h> now exists (include/wctype.h). Classification is ASCII-only
 * in the C locale, mirroring ctype.h's is*() family exactly (see that
 * header's comment for why): every BMP code unit above 0x7f answers
 * false for every class, with no special-casing needed for a lone
 * surrogate half (0xd800-0xdfff) or WEOF -- both simply fall outside
 * every ASCII range test the same way any other out-of-range value
 * does. iswalpha.html DESCRIPTION restricts the domain to "a valid
 * wide-character code, or ... WEOF" and calls anything else undefined;
 * ntlibc answers false for a lone surrogate rather than leaving it
 * undefined.
 * ------------------------------------------------------------------- */
static void test_iswalpha_family(void)
{
	/* "shall return non-zero if wc is [class]; otherwise ... 0." */
	CHECK(iswalpha(L'a') != 0);
	CHECK(iswalpha(L'1') == 0);
	CHECK(iswdigit(L'1') != 0);
	CHECK(iswdigit(L'a') == 0);
	CHECK(iswspace(L' ') != 0);
	CHECK(iswupper(L'A') != 0);
	CHECK(iswlower(L'a') != 0);
	CHECK(iswpunct(L'.') != 0);
	CHECK(iswcntrl(L'\n') != 0);
	CHECK(iswxdigit(L'F') != 0);
	CHECK(iswgraph(L'a') != 0);
	CHECK(iswprint(L' ') != 0);
	/* "the application shall ensure ... WEOF" is an accepted value */
	CHECK(iswalpha(WEOF) == 0);
}

/* A lone surrogate half is not a valid character (it can never appear
 * alone in well-formed UTF-16), so it is outside every class in the C
 * locale -- same defined-but-false answer WEOF gets above, for the
 * same reason: it fails every ASCII range check. 0xd800 is the first
 * high surrogate, 0xdfff the last low surrogate. */
static void test_iswalpha_family_surrogate(void)
{
	CHECK(iswalpha(0xd800) == 0);
	CHECK(iswalnum(0xd800) == 0);
	CHECK(iswspace(0xd800) == 0);
	CHECK(iswcntrl(0xd800) == 0);
	CHECK(iswprint(0xd800) == 0);
	CHECK(iswalpha(0xdfff) == 0);
}

static void test_iswctype(void)
{
	wctype_t digit = wctype("digit");
	/* "returning true or false" per charclass */
	CHECK(iswctype(L'5', digit) != 0);
	CHECK(iswctype(L'x', digit) == 0);
	/* "An invalid character class name" -> wctype() returns
	 * (wctype_t)0, and iswctype() with that class returns 0. */
	CHECK(wctype("not-a-real-class") == (wctype_t)0);
	CHECK(iswctype(L'5', (wctype_t)0) == 0);
	/* every required class name (iswctype.html) is accepted */
	CHECK(wctype("alnum") != (wctype_t)0);
	CHECK(wctype("alpha") != (wctype_t)0);
	CHECK(wctype("blank") != (wctype_t)0);
	CHECK(wctype("cntrl") != (wctype_t)0);
	CHECK(wctype("graph") != (wctype_t)0);
	CHECK(wctype("lower") != (wctype_t)0);
	CHECK(wctype("print") != (wctype_t)0);
	CHECK(wctype("punct") != (wctype_t)0);
	CHECK(wctype("space") != (wctype_t)0);
	CHECK(wctype("upper") != (wctype_t)0);
	CHECK(wctype("xdigit") != (wctype_t)0);
	/* a lone surrogate is outside every class here too */
	CHECK(iswctype(0xd800, digit) == 0);
}

/* ---------------------------------------------------------------------
 * towlower / towupper -- towlower.html
 * Same domain-restriction reasoning as isw*(): "All other arguments in
 * the domain are returned unchanged" -- a lone surrogate is simply not
 * an uppercase/lowercase letter, so it is returned unchanged like any
 * other non-cased BMP code unit.
 * ------------------------------------------------------------------- */
static void test_towlower(void)
{
	CHECK(towlower(L'A') == L'a');
	CHECK(towupper(L'a') == L'A');
	/* "All other arguments in the domain are returned unchanged." */
	CHECK(towlower(L'1') == L'1');
	CHECK(towupper(L'.') == L'.');
	/* WEOF and a lone surrogate are both outside the cased domain */
	CHECK(towlower(WEOF) == (wint_t)WEOF);
	CHECK(towupper(0xd800) == 0xd800);
}

/* ---------------------------------------------------------------------
 * wctrans / towctrans -- wctrans.html, towctrans.html
 * "tolower" and "toupper" are "defined in all locales" (wctrans.html);
 * ntlibc's C locale defines no others.  towctrans() with the resulting
 * wctrans_t is just towlower()/towupper() by another name.
 * ------------------------------------------------------------------- */
static void test_wctrans(void)
{
	wctrans_t lower = wctrans("tolower");
	wctrans_t upper = wctrans("toupper");

	CHECK(lower != (wctrans_t)0);
	CHECK(upper != (wctrans_t)0);
	CHECK(wctrans("not-a-real-mapping") == (wctrans_t)0);

	CHECK(towctrans(L'A', lower) == L'a');
	CHECK(towctrans(L'a', upper) == L'A');
	/* outside the mapping's domain: returned unchanged, same as
	 * towlower()/towupper() */
	CHECK(towctrans(L'1', lower) == L'1');
	CHECK(towctrans(0xd800, upper) == 0xd800);
}

/* ---------------------------------------------------------------------
 * wcwidth / wcswidth -- wcwidth.html
 * wcwidth(wchar_t) takes exactly one code unit, so for a BMP character
 * (one wchar_t == one codepoint) column width is computable in
 * principle, and the fence below is a DECLINED implementation rather
 * than an unattempted one.  For a supplementary-plane character
 * represented as a surrogate pair, wcwidth() is only ever handed one
 * half at a time and structurally cannot see its partner, so it can
 * never report the *composed* character's true display width (e.g. 2
 * columns for most emoji) -- that clause is N/A, not merely unwritten.
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL (declined, 2026-08-24): wcwidth()/wcswidth() --
       * wcwidth.html DESCRIPTION, RETURN VALUE.  UNIMPL is the right
       * tag because this project's rule counts "I chose not to" as
       * UNIMPL; the assertions below are all satisfiable and are kept
       * so the clause stays written down.  What was declined and why:
       *
       * The only wcwidth() this tree could honestly ship today is one
       * built on iswprint(), and include/wctype.h states a deliberate,
       * standing decision that classification here is ASCII-only --
       * "no BMP code point past 0x7f is ever classified true", so that
       * iswalpha() can never disagree with isalpha().  A wcwidth()
       * over that iswprint() returns -1 ("not printable") for every
       * code point from U+0080 to U+FFFF: every accented Latin letter,
       * all of CJK, everything.  That answer is conforming, given the
       * locale position the tree has already taken, and useless for
       * the one job wcwidth() exists to do -- terminal column
       * accounting -- since callers branch on -1 to reject or error.
       *
       * The decisive consideration is downstream, not local.  ntlibc
       * is a bootstrap target: packages that need a working wcwidth()
       * ship gnulib's replacement and use it *when the libc does not
       * provide one*.  Providing a broken one is worse than providing
       * none, because autoconf finds it, gnulib stands down, and the
       * package silently gets our useless answer instead of gnulib's
       * correct one.  Absent is the state the ecosystem already knows
       * how to handle; present-and-wrong is not.
       *
       * Re-enable this when the tree gains a real Unicode width table
       * (east-asian-width plus combining marks), which it has
       * deliberately not taken on -- not before.  Implementing it over
       * the existing ASCII-only iswprint() would close the fence and
       * make the library worse. */
static void test_wcwidth_bmp(void)
{
	/* "0 for the null wide-character code" */
	CHECK(wcwidth(0) == 0);
	/* printable ASCII -> 1 column */
	CHECK(wcwidth(L'A') == 1);
	/* "-1 ... does not correspond to a printable wide-character code" */
	CHECK(wcwidth(0x01) == -1);
	CHECK(wcswidth(W("AB"), 2) == 2);
}
#endif

/* C99 build, so the negative-array idiom rather than static_assert. */
typedef char wcwidth_fence_needs_16bit_wchar_t[sizeof(wchar_t) == 2 ? 1 : -1];

#if 0 /* N/A: wcwidth() -- wcwidth.html DESCRIPTION -- cannot report the
       * true column width of a non-BMP character.  Such a character is
       * two wchar_t (a UTF-16 surrogate pair, e.g. U+1F600 GRINNING
       * FACE = 0xd83d 0xde00); wcwidth() takes a single wchar_t, so it
       * is handed one surrogate half at a time and has no way to see
       * that a partner unit exists, let alone which codepoint the pair
       * encodes.  The best any implementation can do is a fixed,
       * partner-blind answer for the whole 0xd800-0xdfff surrogate
       * range (commonly -1, treating the lone unit as unprintable),
       * which is wrong for a real 2-column composed character and
       * right by accident for nothing -- the per-half return value can
       * never equal the composed character's actual width.
       *
       * The premise is a type width, so it is pinned rather than only
       * described: the assertion below fails the build if wchar_t ever
       * stops being 16-bit.  It is 16-bit because that is the Windows
       * ABI (arch/x86_64/bits/alltypes.h.in: "TYPEDEF unsigned short
       * wchar_t;") and because every string this library hands NT is a
       * UNICODE_STRING of UTF-16 code units, so it is not a free
       * choice.  If a port ever widened it to 32 bits, non-BMP
       * characters would become representable in one wchar_t and this
       * clause would become live -- at which point this fence is wrong
       * and the compiler says so. */
static void test_wcwidth_non_bmp(void)
{
	/* U+1F600 (a genuinely 2-column glyph in terminals) split into
	 * surrogates. */
	CHECK(wcwidth(0xd83d) == 2);	/* cannot be satisfied: high half
					 * alone carries no width info */
	CHECK(wcwidth(0xde00) == 2);	/* likewise for the low half */
}
#endif

/* ---------------------------------------------------------------------
 * wcsstr / wcspbrk / wcscspn / wcsspn -- wcsstr.html and family
 * Pure code-unit sequence search, exactly like the already-implemented
 * wcschr()/wcsrchr(): a surrogate pair is just two opaque units to
 * match.  Implemented in src/string/wcsstr.c and src/string/wcsspn.c.
 * ------------------------------------------------------------------- */
static void test_wcsstr(void)
{
	const wchar_t *hay = W("abcdef");
	CHECK(wcsstr(hay, W("cd")) == hay + 2);
	CHECK(wcsstr(hay, W("zz")) == 0);
	/* "If ws2 points to a wide-character string with zero length,
	 * the function shall return ws1." */
	CHECK(wcsstr(hay, W("")) == hay);
}

static void test_wcspbrk_family(void)
{
	const wchar_t *s = W("abc123");
	/* wcspbrk: pointer to first char in s that is also in the
	 * breakset, or null if none. */
	CHECK(wcspbrk(s, W("31")) == s + 3);
	CHECK(wcspbrk(s, W("xyz")) == 0);
	/* wcscspn: length of initial segment with NO chars from reject. */
	CHECK(wcscspn(s, W("321")) == 3);
	/* wcsspn: length of initial segment consisting ONLY of chars
	 * from accept. */
	CHECK(wcsspn(s, W("abc")) == 3);
}

/* ---------------------------------------------------------------------
 * wcstok -- wcstok.html
 * ------------------------------------------------------------------- */
static void test_wcstok(void)
{
	wchar_t buf[16];
	wchar_t *save;
	wchar_t *tok;
	wcscpy(buf, W("ab,cd"));
	/* "a pointer to the first wide-character code of a token." */
	tok = wcstok(buf, W(","), &save);
	CHECK(tok == buf && !wcscmp(tok, W("ab")));
	tok = wcstok(0, W(","), &save);
	CHECK(!wcscmp(tok, W("cd")));
	/* "If there are no non-separator characters remaining ... a null
	 * pointer shall be returned." */
	CHECK(wcstok(0, W(","), &save) == 0);

	/* "The first call ... shall search ... for the first wide-character
	 * code that is not contained in the current separator string" --
	 * leading and repeated separators are skipped, not returned as
	 * empty tokens. */
	wcscpy(buf, W(",,ab,,cd,,"));
	tok = wcstok(buf, W(","), &save);
	CHECK(tok == buf + 2 && !wcscmp(tok, W("ab")));
	tok = wcstok(0, W(","), &save);
	CHECK(tok != 0 && !wcscmp(tok, W("cd")));
	CHECK(wcstok(0, W(","), &save) == 0);

	/* A string of nothing but separators yields no token at all. */
	wcscpy(buf, W(",,,"));
	CHECK(wcstok(buf, W(","), &save) == 0);

	/* The separator set may change between calls: "the separator
	 * string pointed to by ws2 may be different from call to call." */
	wcscpy(buf, W("ab:cd,ef"));
	tok = wcstok(buf, W(":"), &save);
	CHECK(tok != 0 && !wcscmp(tok, W("ab")));
	tok = wcstok(0, W(","), &save);
	CHECK(tok != 0 && !wcscmp(tok, W("cd")));
	tok = wcstok(0, W(","), &save);
	CHECK(tok != 0 && !wcscmp(tok, W("ef")));
}

/* ---------------------------------------------------------------------
 * wcsdup / wcsnlen / wcpcpy / wcpncpy
 * ------------------------------------------------------------------- */
static void test_wcsdup(void)
{
	wchar_t *d = wcsdup(W("abc"));
	/* "a pointer to the newly allocated wide-character string." */
	CHECK(d != 0 && !wcscmp(d, W("abc")));
	free(d);
}

static void test_wcsnlen(void)
{
	CHECK(wcsnlen(W("abc"), 10) == 3);
	CHECK(wcsnlen(W("abcdef"), 3) == 3);
}

static void test_wcpcpy(void)
{
	wchar_t buf[8];
	wchar_t *end = wcpcpy(buf, W("abc"));
	CHECK(end == buf + 3 && *end == 0);
	CHECK(!wcscmp(buf, W("abc")));

	/* wcpncpy: stpncpy.html RETURN VALUE read for wide characters --
	 * "a pointer to the terminating null byte in s1", i.e. the first
	 * pad unit when the source is shorter than n. */
	wmemset(buf, L'Z', 8);
	end = wcpncpy(buf, W("ab"), 5);
	CHECK(end == buf + 2 && *end == 0);
	/* "the remainder ... shall be filled with null bytes" */
	CHECK(buf[2] == 0 && buf[3] == 0 && buf[4] == 0);
	/* untouched past n */
	CHECK(buf[5] == L'Z');
	/* "or, if s1 is not null-terminated, s1 + n": source at least n
	 * long, so nothing is written past n and no terminator exists. */
	wmemset(buf, L'Z', 8);
	end = wcpncpy(buf, W("abcdef"), 3);
	CHECK(end == buf + 3);
	CHECK(buf[0] == L'a' && buf[1] == L'b' && buf[2] == L'c');
	CHECK(buf[3] == L'Z');
}

/* ---------------------------------------------------------------------
 * wcscasecmp / wcsncasecmp (+ _l) -- wcscasecmp.html
 * Case-folding a lone surrogate half is identity (it is not a cased
 * letter), the same domain restriction as towlower() above, so this is
 * well defined over the whole wchar_t domain.  Implemented in
 * src/string/wcscasecmp.c.
 * ------------------------------------------------------------------- */
static void test_wcscasecmp(void)
{
	/* "an integer greater than, equal to, or less than 0" ignoring
	 * case. */
	CHECK(wcscasecmp(W("ABC"), W("abc")) == 0);
	CHECK(wcscasecmp(W("abd"), W("abc")) > 0);
	CHECK(wcsncasecmp(W("ABCxyz"), W("abcqqq"), 3) == 0);
	CHECK(wcsncasecmp(W("ABD"), W("abc"), 3) > 0);
	/* n == 0: nothing compared, so equal. */
	CHECK(wcsncasecmp(W("x"), W("y"), 0) == 0);
	/* wcscasecmp.html: the _l forms "shall be equivalent to" the
	 * plain forms "except that the locale ... is the locale
	 * represented by locale".  This library has only C/POSIX, so
	 * both must agree with the plain forms (same shape as
	 * test/posix-strings.c's strcasecmp_l checks). */
	CHECK(wcscasecmp_l(W("ABC"), W("abc"), LC_GLOBAL_LOCALE) == 0);
	CHECK(wcscasecmp_l(W("abd"), W("abc"), LC_GLOBAL_LOCALE) > 0);
	CHECK(wcsncasecmp_l(W("ABCxyz"), W("abcqqq"), 3, LC_GLOBAL_LOCALE) == 0);
	CHECK(wcsncasecmp_l(W("ABD"), W("abc"), 3, LC_GLOBAL_LOCALE) > 0);
}

/* ---------------------------------------------------------------------
 * wcstol / wcstoll / wcstoul / wcstoull -- wcstol.html
 * Pure ASCII-digit parsing (the same subject-sequence grammar
 * wcstoimax() implements above, and now literally the same parser --
 * src/stdlib/wcstol.c, which is what src/stdlib/wcstoimax.c was
 * renamed to when these four joined it); no surrogate involvement.
 * ------------------------------------------------------------------- */
static void test_wcstol_family(void)
{
	wchar_t *end;
	CHECK(wcstol(W("123"), &end, 10) == 123);
	CHECK(*end == 0);
	/* out of range -> LONG_MAX/LONG_MIN + ERANGE */
	errno = 0;
	CHECK(wcstol(W("99999999999999999999"), 0, 10) == LONG_MAX);
	CHECK(errno == ERANGE);
	CHECK(wcstoul(W("42"), 0, 10) == 42UL);
	CHECK(wcstoll(W("42"), 0, 10) == 42LL);
	CHECK(wcstoull(W("42"), 0, 10) == 42ULL);

	/* LLP64: long is 32 bits, long long is 64.  wcstoll() must reach
	 * past LONG_MAX where wcstol() saturates, or the two are not
	 * distinguishable at all on this target. */
	CHECK(wcstoll(W("2147483648"), 0, 10) == 2147483648LL);
	errno = 0;
	CHECK(wcstol(W("2147483648"), 0, 10) == LONG_MAX);
	CHECK(errno == ERANGE);

	/* "If the value of base is 0, the expected form of the subject
	 * sequence is that of a decimal, octal, or hexadecimal constant."
	 */
	CHECK(wcstol(W("0x1f"), &end, 0) == 31);
	CHECK(*end == 0);
	CHECK(wcstol(W("017"), 0, 0) == 15);
	/* base 16 with an optional 0x prefix */
	CHECK(wcstol(W("0x1f"), 0, 16) == 31);

	/* "the subject sequence shall be interpreted as an integer ...
	 * and if the subject sequence begins with a <hyphen-minus>, the
	 * value resulting from the conversion shall be negated" -- for
	 * the unsigned forms that negation wraps, it is not an error. */
	CHECK(wcstoul(W("-1"), 0, 10) == ULONG_MAX);
	CHECK(wcstoull(W("-1"), 0, 10) == ULLONG_MAX);

	/* "If the correct value is outside the range ... {ULONG_MAX} ...
	 * shall be returned and errno set to [ERANGE]." */
	errno = 0;
	CHECK(wcstoul(W("99999999999999999999"), 0, 10) == ULONG_MAX);
	CHECK(errno == ERANGE);
	errno = 0;
	CHECK(wcstoull(W("99999999999999999999"), 0, 10) == ULLONG_MAX);
	CHECK(errno == ERANGE);
	/* A magnitude that overflows even uintmax_t, with a minus sign,
	 * is still "outside the range of representable values" for the
	 * unsigned forms: {ULONG_MAX}/{ULLONG_MAX} and [ERANGE], not a
	 * wrapped negation.  (This is the case that distinguishes the
	 * unsigned arm of the shared worker from the signed one; without
	 * it a wcstoul() routed through the signed arm returns 0 here.) */
	errno = 0;
	CHECK(wcstoul(W("-99999999999999999999"), 0, 10) == ULONG_MAX);
	CHECK(errno == ERANGE);
	errno = 0;
	CHECK(wcstoull(W("-99999999999999999999"), 0, 10) == ULLONG_MAX);
	CHECK(errno == ERANGE);

	/* the negative end of the signed range clamps to LONG_MIN */
	errno = 0;
	CHECK(wcstol(W("-99999999999999999999"), 0, 10) == LONG_MIN);
	CHECK(errno == ERANGE);
	errno = 0;
	CHECK(wcstoll(W("-99999999999999999999"), 0, 10) == LLONG_MIN);
	CHECK(errno == ERANGE);

	/* "If no conversion could be performed, 0 shall be returned and
	 * errno may be set to [EINVAL]" -- and endptr, if not null, gets
	 * the original nptr back. */
	{
		const wchar_t *nptr = W("zz");
		end = 0;
		CHECK(wcstol(nptr, &end, 10) == 0);
		CHECK(end == nptr);
	}

	/* leading whitespace is skipped, and endptr stops at the first
	 * unconvertible unit */
	CHECK(wcstol(W("  -12abc"), &end, 10) == -12);
	CHECK(*end == L'a');
}

/* ---------------------------------------------------------------------
 * wcstod / wcstof / wcstold -- wcstod.html
 * ------------------------------------------------------------------- */
#if 0 /* UNIMPL: wcstod()/wcstof()/wcstold() -- wcstod.html DESCRIPTION,
       * RETURN VALUE, ERRORS. */
static void test_wcstod_family(void)
{
	wchar_t *end;
	CHECK(wcstod(W("1.5"), &end) == 1.5);
	CHECK(*end == 0);
	/* "If no conversion could be performed, 0 shall be returned." */
	end = 0;
	CHECK(wcstod(W("xyz"), &end) == 0.0);
}
#endif

/* ---------------------------------------------------------------------
 * wcscoll / wcscoll_l / wcsxfrm / wcsxfrm_l -- wcscoll.html,
 * wcsxfrm.html.  This library only meaningfully supports the C/POSIX
 * (UTF-8) locale (see the btowc()/LC_CTYPE divergence note above), so
 * both are a code-unit-order collation -- which is exactly what
 * src/string/strcoll.c and strxfrm.c do for bytes, confirmed rather
 * than presumed.  Implemented in src/string/wcscoll.c and wcsxfrm.c.
 * ------------------------------------------------------------------- */
static void test_wcscoll(void)
{
	locale_t loc;

	/* C/POSIX locale collation == code-unit order, like wcscmp(). */
	CHECK(wcscoll(W("abc"), W("abc")) == 0);
	CHECK(wcscoll(W("abd"), W("abc")) > 0);
	CHECK(wcscoll(W("abc"), W("abd")) < 0);
	/* the empty string collates before any non-empty one */
	CHECK(wcscoll(W(""), W("a")) < 0);
	CHECK(wcscoll(W(""), W("")) == 0);
	/* a prefix collates before the longer string */
	CHECK(wcscoll(W("ab"), W("abc")) < 0);
	/* POSIX-locale collation is code-unit order, so it agrees with
	 * wcscmp() in sign on every pair */
	CHECK((wcscoll(W("A"), W("a")) < 0) == (wcscmp(W("A"), W("a")) < 0));

	/* wcscoll.html: the caller may "set errno to 0, then check errno
	 * after the call" to detect [EINVAL]; nothing here can produce it,
	 * so errno must be left alone on a successful comparison. */
	errno = 0;
	CHECK(wcscoll(W("abc"), W("abd")) < 0);
	CHECK(errno == 0);

	/* wcscoll.html: wcscoll_l() "shall be equivalent to wcscoll(),
	 * except that the locale data used is from the locale represented
	 * by locale."  Same shape as test/posix-string.c's strcoll_l
	 * checks: through LC_GLOBAL_LOCALE and through a freshly created
	 * "C" locale, both must agree with the plain form. */
	CHECK(wcscoll_l(W("abc"), W("abc"), LC_GLOBAL_LOCALE) == 0);
	CHECK(wcscoll_l(W("abc"), W("abd"), LC_GLOBAL_LOCALE) < 0);
	CHECK(wcscoll_l(W("abd"), W("abc"), LC_GLOBAL_LOCALE) > 0);
	CHECK((wcscoll_l(W("abc"), W("abd"), LC_GLOBAL_LOCALE) < 0)
	      == (wcscoll(W("abc"), W("abd")) < 0));

	loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	CHECK(loc != (locale_t)0);
	if (loc) {
		CHECK(wcscoll_l(W("abc"), W("abd"), loc) < 0);
		CHECK(wcscoll_l(W("abc"), W("abc"), loc) == 0);
		freelocale(loc);
	}
}

static void test_wcsxfrm(void)
{
	wchar_t buf[8], b2[8];
	size_t n = wcsxfrm(buf, W("abc"), 8);
	/* "the length of the transformed wide-character string (not
	 * including the terminating null)." In the C locale the
	 * transformed form is the string itself. */
	CHECK(n == 3);
	CHECK(!wcscmp(buf, W("abc")));
	/* querying required length with n == 0 must not touch ws1 */
	CHECK(wcsxfrm(0, W("abc"), 0) == 3);

	/* "If the value returned is n or more, the contents of the array
	 * pointed to by ws1 are unspecified" -- but the RETURN VALUE is
	 * still the full transformed length, which is what makes the
	 * two-call size query work.  Truncation must still terminate. */
	wmemset(buf, L'Z', 8);
	CHECK(wcsxfrm(buf, W("abcdef"), 3) == 6);
	CHECK(wcslen(buf) < 3);
	CHECK(buf[3] == L'Z');	/* nothing written at or past n */

	/* The defining clause: "the result of wcscmp() on two transformed
	 * wide-character strings shall be the same as the result of
	 * wcscoll() on the two original strings." */
	wcsxfrm(buf, W("abc"), 8);
	wcsxfrm(b2, W("abd"), 8);
	CHECK((wcscmp(buf, b2) < 0) == (wcscoll(W("abc"), W("abd")) < 0));
	wcsxfrm(buf, W("abd"), 8);
	wcsxfrm(b2, W("abc"), 8);
	CHECK((wcscmp(buf, b2) > 0) == (wcscoll(W("abd"), W("abc")) > 0));

	/* No error is possible here, so errno must survive a successful
	 * call (the same property test/posix-string.c pins for strxfrm). */
	errno = 0;
	CHECK(wcsxfrm(buf, W("abc"), 8) == 3);
	CHECK(errno == 0);

	/* wcsxfrm.html: wcsxfrm_l() "shall be equivalent to wcsxfrm(),
	 * except that the locale data used is from the locale represented
	 * by locale." */
	CHECK(wcsxfrm_l(0, W("abcd"), 0, LC_GLOBAL_LOCALE) == 4);
	CHECK(wcsxfrm_l(buf, W("abcd"), 8, LC_GLOBAL_LOCALE) == 4);
	CHECK(!wcscmp(buf, W("abcd")));
}

/* ---------------------------------------------------------------------
 * wcsftime -- wcsftime.html
 * Implemented in src/time/wcsftime.c, by walking the wide format and
 * calling strftime() once per conversion specifier -- not by formatting
 * the whole thing into bytes and widening, which cannot honour a
 * maxsize counted in wide characters.  See that file's header.
 * ------------------------------------------------------------------- */
static void test_wcsftime(void)
{
	wchar_t buf[32];
	struct tm tm;
	size_t n;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 100; tm.tm_mon = 0; tm.tm_mday = 2;
	/* "the number of wide-character codes placed into the array
	 * ... not including the terminating null." */
	n = wcsftime(buf, 32, W("%Y-%m-%d"), &tm);
	CHECK(n == 10);
	CHECK(!wcscmp(buf, W("2000-01-02")));
	/* "If the total number of resulting characters ... is not more
	 * than maxsize, [...] Otherwise, zero is returned." */
	CHECK(wcsftime(buf, 4, W("%Y-%m-%d"), &tm) == 0);
	/* the boundary: the count INCLUDING the terminating null must fit */
	CHECK(wcsftime(buf, 11, W("%Y-%m-%d"), &tm) == 10);
	CHECK(wcsftime(buf, 10, W("%Y-%m-%d"), &tm) == 0);
	/* maxsize 0 cannot even hold the terminating null */
	CHECK(wcsftime(buf, 0, W(""), &tm) == 0);

	/* Ordinary characters "shall be copied unchanged into the array",
	 * and %% produces a single '%'. */
	CHECK(wcsftime(buf, 32, W("a%%b"), &tm) == 3);
	CHECK(!wcscmp(buf, W("a%b")));

	/* strftime()'s own grammar, mirrored: an unrecognised specifier is
	 * passed through literally, and a trailing '%' is dropped.  Pinned
	 * here so wcsftime() and strftime() cannot drift apart. */
	CHECK(wcsftime(buf, 32, W("[%q]"), &tm) == 4);
	CHECK(!wcscmp(buf, W("[%q]")));
	CHECK(wcsftime(buf, 32, W("ab%"), &tm) == 2);
	CHECK(!wcscmp(buf, W("ab")));

	/* %n and %t are a <newline> and a <tab>. */
	CHECK(wcsftime(buf, 32, W("%n%t"), &tm) == 2);
	CHECK(buf[0] == L'\n' && buf[1] == L'\t');

	/* THE COUNT IS IN WIDE CHARACTERS, NOT BYTES.  Three U+00E9
	 * literals are three wide characters but six UTF-8 bytes, so an
	 * implementation that budgeted maxsize in bytes would fail this
	 * where POSIX requires it to succeed. */
	{
		static const wchar_t acc[4] = { 0xe9, 0xe9, 0xe9, 0 };
		CHECK(wcsftime(buf, 4, acc, &tm) == 3);
		CHECK(buf[0] == 0xe9 && buf[2] == 0xe9 && buf[3] == 0);
		CHECK(wcsftime(buf, 3, acc, &tm) == 0);
	}

	/* A conversion specifier whose letter is not a single-byte
	 * character cannot be spelled in strftime()'s byte format at all.
	 * strftime()'s answer for an unrecognised specifier is to emit the
	 * '%' and the character literally, and wcsftime() must agree. */
	{
		static const wchar_t oddspec[3] = { L'%', 0xe9, 0 };
		CHECK(wcsftime(buf, 32, oddspec, &tm) == 2);
		CHECK(buf[0] == L'%' && buf[1] == 0xe9 && buf[2] == 0);
	}

	/* A specifier whose EXPANSION is non-ASCII: %Z copies tm's zone
	 * name (src/time/strftime.c), so a zone name holding U+1F600 --
	 * four UTF-8 bytes -- must arrive as exactly two wchar_t, the
	 * UTF-16 surrogate pair, and be counted as two.  This is the case
	 * that separates a correct wide-character count from a byte count:
	 * an implementation formatting into bytes and widening afterwards
	 * has to get this conversion right, and one budgeting maxsize in
	 * bytes gets the count wrong by two. */
	tm.__tm_zone = "\xf0\x9f\x98\x80";
	CHECK(wcsftime(buf, 32, W("%Z"), &tm) == 2);
	CHECK(buf[0] == 0xd83d && buf[1] == 0xde00 && buf[2] == 0);
	/* and the maxsize test counts those two, not the four bytes */
	CHECK(wcsftime(buf, 3, W("%Z"), &tm) == 2);
	CHECK(wcsftime(buf, 2, W("%Z"), &tm) == 0);
	tm.__tm_zone = 0;

	/* A supplementary character in the format is two wchar_t (a UTF-16
	 * surrogate pair) and must be copied through as two, unmangled --
	 * it is a literal, so it never goes near a conversion. */
	{
		static const wchar_t emoji[3] = { 0xd83d, 0xde00, 0 };
		CHECK(wcsftime(buf, 32, emoji, &tm) == 2);
		CHECK(buf[0] == 0xd83d && buf[1] == 0xde00 && buf[2] == 0);
	}
}

/* ---------------------------------------------------------------------
 * mbsnrtowcs / wcsnrtombs -- mbsnrtowcs.html, wcsnrtombs.html
 * Bounded-input variants of mbsrtowcs()/wcsrtombs(), implemented
 * alongside them in src/stdlib/mbrtowc.c.  The fence used to call the
 * extra nmc/nwc parameter "a plain length cap, no surrogate-specific
 * behavior beyond what those two already have"; that turned out to be
 * wrong in two ways, both now asserted below.  A real input bound makes
 * mbrtowc()'s "incomplete" return the ordinary end-of-buffer case
 * rather than the error mbsrtowcs() can treat it as, and it makes the
 * surrogate-pair lookahead in wcsrtombs() illegal without a guard,
 * because the caller's array need not be null-terminated.  These are
 * Issue 8 pages, cited against
 * https://pubs.opengroup.org/onlinepubs/9799919799/ rather than the
 * Issue 7 base URL the rest of this file uses -- neither function
 * exists in Issue 7.
 * ------------------------------------------------------------------- */
static void test_mbsnrtowcs(void)
{
	/* dst is zeroed rather than left uninitialised: with nmc stopping
	 * the conversion before the source's terminating null, mbsnrtowcs()
	 * writes no null wide character, so comparing dst as a *string*
	 * would read past what the call defines.  The original transcription
	 * of this test did exactly that and passed by luck. */
	wchar_t dst[8] = {0};
	const char *src = "abcdef";
	mbstate_t st;
	memset(&st, 0, sizeof(st));
	/* "the conversion ... is limited to at most nmc bytes": only 3 of
	 * the 6 source bytes may be consumed */
	CHECK(mbsnrtowcs(dst, &src, 3, 8, &st) == 3);
	CHECK(dst[0] == L'a' && dst[1] == L'b' && dst[2] == L'c');
	/* "the address just past the last byte processed" */
	CHECK(src != 0 && !strcmp(src, "def"));

	/* Reaching the terminating null: src is set to a null pointer and
	 * the null is not counted in the return value. */
	memset(&st, 0, sizeof(st));
	src = "ab";
	CHECK(mbsnrtowcs(dst, &src, 10, 8, &st) == 2);
	CHECK(src == 0);
	CHECK(!wcscmp(dst, W("ab")));

	/* The len bound applies as it does for mbsrtowcs(): at most len
	 * wide characters are stored, and src stops there. */
	memset(&st, 0, sizeof(st));
	src = "abcdef";
	CHECK(mbsnrtowcs(dst, &src, 10, 2, &st) == 2);
	CHECK(src != 0 && !strcmp(src, "cdef"));

	/* dst a null pointer: the count is computed and the pointer object
	 * pointed to by src is not modified. */
	memset(&st, 0, sizeof(st));
	src = "abcdef";
	CHECK(mbsnrtowcs(0, &src, 4, 1, &st) == 4);
	CHECK(src != 0 && !strcmp(src, "abcdef"));

	/* "If the input buffer ends with an incomplete character,
	 * conversion shall stop at the end of the input buffer; a
	 * subsequent call ... shall correctly complete the conversion of
	 * that character."  U+00E9 is 0xC3 0xA9 in UTF-8, so a 2-byte
	 * window over "a\xc3\xa9" ends one byte into that character. */
	memset(&st, 0, sizeof(st));
	memset(dst, 0, sizeof(dst));
	src = "a\xc3\xa9";
	CHECK(mbsnrtowcs(dst, &src, 2, 8, &st) == 1);
	CHECK(dst[0] == L'a');
	CHECK(src != 0 && !strcmp(src, "\xa9"));	/* past the byte consumed */
	CHECK(!mbsinit(&st));				/* partial sequence held */
	/* the continuation byte alone now completes it */
	CHECK(mbsnrtowcs(dst, &src, 1, 8, &st) == 1);
	CHECK(dst[0] == 0xe9);
	CHECK(mbsinit(&st));

	/* A supplementary character is one 4-byte UTF-8 sequence and two
	 * wchar_t here.  mbrtowc() hands back the high surrogate having
	 * consumed all four bytes, then the low surrogate from state alone
	 * consuming none -- so the second half must still be delivered
	 * after the byte budget is exhausted.  (This is why mbrtowc() is
	 * called before, not after, testing nmc.) */
	memset(&st, 0, sizeof(st));
	memset(dst, 0, sizeof(dst));
	src = "\xf0\x9f\x98\x80";	/* U+1F600 */
	CHECK(mbsnrtowcs(dst, &src, 4, 8, &st) == 2);
	CHECK(dst[0] == 0xd83d && dst[1] == 0xde00);
	CHECK(src != 0 && *src == 0);
	CHECK(mbsinit(&st));

	/* [EILSEQ]: "An invalid character sequence is detected." */
	memset(&st, 0, sizeof(st));
	src = "a\xff";
	errno = 0;
	CHECK(mbsnrtowcs(dst, &src, 2, 8, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);
}

static void test_wcsnrtombs(void)
{
	char dst[8];
	const wchar_t *src = W("abcdef");
	const wchar_t *start;
	/* U+1F600 as a UTF-16 surrogate pair; W() cannot build one, since
	 * it only widens ASCII. */
	static const wchar_t pair[3] = { 0xd83d, 0xde00, 0 };
	mbstate_t st;

	memset(&st, 0, sizeof(st));
	/* "the conversion is limited to the first nwc wide characters":
	 * only 3 of the 6 source wchar_t may be consumed */
	CHECK(wcsnrtombs(dst, &src, 3, 8, &st) == 3);
	CHECK(!memcmp(dst, "abc", 3));
	/* "the address just past the last wide character converted" */
	CHECK(src != 0 && !wcscmp(src, W("def")));

	/* Reaching the terminating null wide character: src becomes a null
	 * pointer, and the null byte is written but not counted. */
	memset(&st, 0, sizeof(st));
	src = W("ab");
	memset(dst, 'Z', sizeof(dst));
	CHECK(wcsnrtombs(dst, &src, 10, 8, &st) == 2);
	CHECK(src == 0);
	CHECK(!strcmp(dst, "ab"));

	/* dst a null pointer: byte count only, src not modified. */
	memset(&st, 0, sizeof(st));
	src = start = W("abcdef");
	CHECK(wcsnrtombs(0, &src, 4, 1, &st) == 4);
	CHECK(src == start);

	/* A supplementary character is four bytes and must never be split:
	 * with room for only three, nothing is written and the count stops
	 * short of it. */
	memset(&st, 0, sizeof(st));
	src = pair;
	memset(dst, 'Z', sizeof(dst));
	CHECK(wcsnrtombs(dst, &src, 2, 3, &st) == 0);
	CHECK(dst[0] == 'Z');
	CHECK(src == pair);
	/* with room, both halves convert together as one 4-byte sequence */
	memset(&st, 0, sizeof(st));
	src = pair;
	CHECK(wcsnrtombs(dst, &src, 2, 8, &st) == 4);
	CHECK(!memcmp(dst, "\xf0\x9f\x98\x80", 4));
	CHECK(src == pair + 2);

	/* A high surrogate that is the last wide character inside nwc has
	 * no partner this call may read.  ntlibc reports [EILSEQ], the same
	 * answer wcsrtombs() gives for a lone high surrogate; POSIX does
	 * not describe the case because it cannot arise with a 32-bit
	 * wchar_t.  Asserted so the choice is pinned rather than incidental.
	 */
	memset(&st, 0, sizeof(st));
	src = pair;
	errno = 0;
	CHECK(wcsnrtombs(dst, &src, 1, 8, &st) == (size_t)-1);
	CHECK(errno == EILSEQ);
}


int main(void)
{
	test_wcslen();
	test_wcscpy();
	test_wcsncpy();
	test_wcscat();
	test_wcsncat();
	test_wcscmp();
	test_wcsncmp();
	test_wcschr();
	test_wcsrchr();

	test_wmemchr();
	test_wmemcmp();
	test_wmemcpy();
	test_wmemmove();
	test_wmemset();

	test_mbsinit();
	test_mbrtowc_basic();
	test_mbrtowc_overlong_and_range();
	test_mbrtowc_surrogate_pair_divergence();
	test_wcrtomb_basic();
	test_wcrtomb_surrogate_pair_divergence();
	test_mbrlen();

	test_mbtowc();
	test_wctomb();
	test_mblen();

	test_mbstowcs();
	test_wcstombs();
	test_mbsrtowcs();
	test_wcsrtombs();

	test_btowc();
	test_wctob();

	test_wcstoimax();

	test_iswalpha_family();
	test_iswalpha_family_surrogate();
	test_iswctype();
	test_towlower();
	test_wctrans();

	test_wcsstr();
	test_wcspbrk_family();
	test_wcstok();
	test_wcsdup();
	test_wcsnlen();
	test_wcpcpy();
	test_wcscasecmp();
	test_wcstol_family();
	test_wcscoll();
	test_wcsxfrm();
	test_mbsnrtowcs();
	test_wcsnrtombs();
	test_wcsftime();

	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("ok\n");
	return 0;
}
