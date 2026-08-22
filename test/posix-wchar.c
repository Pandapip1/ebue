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
 * wchar.h in this library declares no wcsnlen, no wcpcpy/wcpncpy, and
 * there is no wctype.h at all (no isw*()/towupper() etc): grep confirms
 * they are simply absent from include/, so they are N/A here as "not
 * implemented" rather than tested.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>

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

	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("ok\n");
	return 0;
}
