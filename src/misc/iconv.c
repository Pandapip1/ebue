/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * iconv_open(), iconv(), iconv_close() -- iconv_open.html, iconv.html,
 * iconv_close.html.
 *
 * THE THREE STOP CONDITIONS, verbatim from iconv.html, because they are
 * the whole specification of this file and each one is a separate exit:
 *
 *   "If a sequence of input bytes does not form a valid character in
 *    the specified codeset, conversion shall stop after the previous
 *    successfully converted character."            -> [EILSEQ]
 *   "If the input buffer ends with an incomplete character or shift
 *    sequence, conversion shall stop after the previous successfully
 *    converted bytes."                             -> [EINVAL]
 *   "If the output buffer is not large enough to hold the entire
 *    converted input, conversion shall stop just prior to the input
 *    bytes that would cause the output buffer to overflow."
 *                                                  -> [E2BIG]
 *
 * "The variable pointed to by inbuf shall be updated to point to the
 * byte following the last byte successfully used in the conversion."
 * All three exits therefore leave inbuf/inbytesleft AT the character
 * that could not be converted, never past it -- which is what makes a
 * resumed call with a bigger output buffer, or with the rest of the
 * input appended, do the right thing.  Every conversion below is
 * committed as a unit: the output space is checked before any input is
 * consumed, so there is no state in which half a character has been
 * written.
 *
 * RETURN VALUE: "The iconv() function shall update the variables
 * pointed to by the arguments to reflect the extent of the conversion
 * and return the number of non-identical conversions performed."  Zero
 * is returned on every successful call here, and that is a claim rather
 * than a shortcut: UTF-8 and UTF-16LE encode exactly the same set of
 * Unicode scalar values, so every character this converter accepts
 * survives the round trip unchanged and none of them is a substitution.
 * There is no character it can convert non-identically.
 *
 * WHAT IT IS BUILT ON, and what it deliberately is not.
 * src/internal/utf.c is the wrong foundation and is not used: it
 * converts whole strings through ntdll's RtlUTF8ToUnicodeN /
 * RtlUnicodeToUTF8N and mallocs its result, so it has no conversion
 * descriptor, no incremental pointer advance, no resumable state and no
 * partial-output case -- none of the four things the clauses above are
 * about.  The decoder and encoder here are written out in pure C, for
 * the same reason src/stdlib/mbrtowc.c is: no ntdll call, so this works
 * identically on the PE legs and on the native asan build.
 *
 * They are written out rather than layered on mbrtowc()/wcrtomb(),
 * which was the other candidate.  Those carry a pending surrogate in
 * the mbstate_t and hand back one UTF-16 code unit at a time, so a
 * supplementary character would be half-consumed across two calls --
 * exactly the state the "stop just prior to the input bytes" clause
 * forbids being visible to a caller.  Reconstructing the commit-as-a-
 * unit property on top of that machinery costs more than decoding UTF-8
 * directly.
 *
 * CODESETS.  "Settings of fromcode and tocode and their permitted
 * combinations are implementation-defined."  UTF-8 and UTF-16LE, in all
 * four combinations.  Each to itself is a validating copy, not a memcpy:
 * an identity conversion still has to reject a byte that does not form
 * a valid character, or the EILSEQ clause would mean nothing for it.
 *
 * A supplementary character (U+10000..U+10FFFF) is a surrogate pair in
 * UTF-16LE and four bytes in UTF-8, and both directions handle it.
 * Unpaired surrogates are rejected with EILSEQ in both directions:
 * D800-DFFF are not Unicode scalar values, a UTF-8 encoding of one is
 * ill-formed, and a lone surrogate code unit in UTF-16 does not form a
 * character.
 */
#include <iconv.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

enum { CS_UTF8, CS_UTF16LE };

struct __iconv_state { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- libc-internal name is intentionally reserved against application collision
	int from;
	int to;
};

/* Names are compared case-insensitively with '-' and '_' ignored, so
 * "UTF-8", "utf8" and "UTF_8" are one name.  Deliberately NOT accepting
 * "UCS-2LE" as a spelling of UTF-16LE: UCS-2 cannot represent a
 * supplementary character and this converter emits surrogate pairs, so
 * the name would misdescribe the output. */
static int codeset(const char *name, int *out)
{
	char norm[16];
	size_t n = 0;
	const char *p;

	for (p = name; *p; p++) {
		char c = *p;
		if (c == '-' || c == '_') continue;
		if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		if (n >= sizeof norm - 1) return 0;
		norm[n++] = c;
	}
	norm[n] = 0;

	if (!strcmp(norm, "UTF8")) { *out = CS_UTF8; return 1; }
	if (!strcmp(norm, "UTF16LE")) { *out = CS_UTF16LE; return 1; }
	return 0;
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
	struct __iconv_state *st;
	int f, t;

	if (!tocode || !fromcode || !codeset(fromcode, &f) || !codeset(tocode, &t)) {
		/* "[EINVAL] The conversion specified by fromcode and
		 * tocode is not supported by the implementation." */
		errno = EINVAL;
		return (iconv_t)-1;
	}
	st = malloc(sizeof *st);
	if (!st) { errno = ENOMEM; return (iconv_t)-1; }
	st->from = f;
	st->to = t;
	return (iconv_t)st;
}

int iconv_close(iconv_t cd)
{
	if (!cd || cd == (iconv_t)-1) { errno = EBADF; return -1; }
	free(cd);
	return 0;
}

/* Decode one character.  Returns the number of input bytes it occupies
 * and stores the scalar value, or 0 with *err set to EILSEQ or EINVAL.
 * Never consumes anything on failure -- the caller has not advanced. */
static size_t decode_utf8(const unsigned char *p, size_t n,
                          uint32_t *cp, int *err)
{
	unsigned char b = p[0];
	size_t len, i;
	uint32_t v;

	if (b < 0x80) { *cp = b; return 1; }
	if (b >= 0xc2 && b <= 0xdf) { len = 2; v = b & 0x1fu; }
	else if (b >= 0xe0 && b <= 0xef) { len = 3; v = b & 0x0fu; }
	else if (b >= 0xf0 && b <= 0xf4) { len = 4; v = b & 0x07u; }
	else { *err = EILSEQ; return 0; }   /* 0x80-0xc1, 0xf5-0xff */

	for (i = 1; i < len; i++) {
		unsigned char c;
		if (i >= n) {
			/* Everything present so far was a valid prefix, so
			 * this is "the input buffer ends with an incomplete
			 * character", not a bad byte. */
			*err = EINVAL;
			return 0;
		}
		c = p[i];
		if (c < 0x80 || c > 0xbf) { *err = EILSEQ; return 0; }
		v = (v << 6) | (uint32_t)(c & 0x3fu);
	}

	/* Overlongs are excluded by the lead-byte ranges above for the
	 * 2-byte form; the 3- and 4-byte forms need the value checked.
	 * Surrogates and anything past U+10FFFF are not scalar values. */
	if (len == 3 && v < 0x800) { *err = EILSEQ; return 0; }
	if (len == 4 && v < 0x10000) { *err = EILSEQ; return 0; }
	if (v >= 0xd800 && v <= 0xdfff) { *err = EILSEQ; return 0; }
	if (v > 0x10ffff) { *err = EILSEQ; return 0; }

	*cp = v;
	return len;
}

static size_t decode_utf16le(const unsigned char *p, size_t n,
                             uint32_t *cp, int *err)
{
	uint32_t hi, lo;

	if (n < 2) { *err = EINVAL; return 0; }
	hi = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
	if (hi < 0xd800 || hi > 0xdfff) { *cp = hi; return 2; }
	/* A low surrogate first is not a character at all. */
	if (hi >= 0xdc00) { *err = EILSEQ; return 0; }
	if (n < 4) { *err = EINVAL; return 0; }
	lo = (uint32_t)p[2] | ((uint32_t)p[3] << 8);
	if (lo < 0xdc00 || lo > 0xdfff) { *err = EILSEQ; return 0; }
	*cp = 0x10000u + ((hi - 0xd800u) << 10) + (lo - 0xdc00u);
	return 4;
}

/* Encode one character.  Returns the number of bytes it needs; writes
 * only if that many are available, and writes nothing otherwise, so the
 * caller can treat a short return as "stop just prior to" this input. */
static size_t encode_utf8(uint32_t cp, unsigned char *p, size_t n)
{
	size_t len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;

	if (n < len) return len;
	switch (len) {
	case 1: p[0] = (unsigned char)cp; break;
	case 2: p[0] = (unsigned char)(0xc0 | (cp >> 6));
	        p[1] = (unsigned char)(0x80 | (cp & 0x3f)); break;
	case 3: p[0] = (unsigned char)(0xe0 | (cp >> 12));
	        p[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
	        p[2] = (unsigned char)(0x80 | (cp & 0x3f)); break;
	default: p[0] = (unsigned char)(0xf0 | (cp >> 18));
	        p[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
	        p[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
	        p[3] = (unsigned char)(0x80 | (cp & 0x3f)); break;
	}
	return len;
}

static size_t encode_utf16le(uint32_t cp, unsigned char *p, size_t n)
{
	if (cp < 0x10000) {
		if (n < 2) return 2;
		p[0] = (unsigned char)(cp & 0xff);
		p[1] = (unsigned char)(cp >> 8);
		return 2;
	}
	if (n < 4) return 4;
	cp -= 0x10000u;
	{
		uint32_t hi = 0xd800u + (cp >> 10), lo = 0xdc00u + (cp & 0x3ffu);
		p[0] = (unsigned char)(hi & 0xff);
		p[1] = (unsigned char)(hi >> 8);
		p[2] = (unsigned char)(lo & 0xff);
		p[3] = (unsigned char)(lo >> 8);
	}
	return 4;
}

size_t iconv(iconv_t cd, char **restrict inbuf, size_t *restrict inbytesleft,
             char **restrict outbuf, size_t *restrict outbytesleft)
{
	struct __iconv_state *st = (struct __iconv_state *)cd;
	unsigned char *ip, *op;
	size_t il, ol;

	if (!st || cd == (iconv_t)-1) { errno = EBADF; return (size_t)-1; }

	/* "the conversion descriptor cd is placed into its initial shift
	 * state by a call for which inbuf is a null pointer".  Neither
	 * codeset here is state-dependent, so there is no state to reset
	 * and nothing to emit -- but the call must still succeed, and a
	 * caller relies on it doing so. */
	if (!inbuf || !*inbuf) return 0;

	ip = (unsigned char *)*inbuf;
	il = inbytesleft ? *inbytesleft : 0;
	op = outbuf && *outbuf ? (unsigned char *)*outbuf : 0;
	/* ol must be 0 whenever op is null: encode_utf8()/encode_utf16le()
	 * only dereference p once n (their length budget, passed ol here)
	 * is large enough to hold what they write, and nothing else ties
	 * "large enough" to "non-null" -- outbuf and outbytesleft are two
	 * independent caller-supplied pointers, so without this a caller
	 * that passes *outbuf == NULL alongside a positive *outbytesleft
	 * would reach a null `p[0] = ...` inside those encoders. Making the
	 * pairing explicit here, rather than only in the encoders, is what
	 * lets a caller with real output space (op non-null) actually use
	 * all of ol -- deriving ol from op instead would truncate that. */
	ol = op && outbytesleft ? *outbytesleft : 0;

	while (il) {
		uint32_t cp;
		size_t used, need;
		int err = 0;

		used = st->from == CS_UTF8 ? decode_utf8(ip, il, &cp, &err)
		                           : decode_utf16le(ip, il, &cp, &err);
		if (!used) goto stop_err;

		need = st->to == CS_UTF8 ? encode_utf8(cp, op, ol)
		                         : encode_utf16le(cp, op, ol);
		if (need > ol) { err = E2BIG; goto stop_err; }

		ip += used; il -= used;
		op += need; ol -= need;
		continue;

	stop_err:
		/* Commit what was converted and leave the pointers at the
		 * character that was not.  The counts written back are the
		 * whole of what a resuming caller has to go on. */
		*inbuf = (char *)ip;
		if (inbytesleft) *inbytesleft = il;
		if (outbuf) *outbuf = (char *)op;
		if (outbytesleft) *outbytesleft = ol;
		errno = err;
		return (size_t)-1;
	}

	*inbuf = (char *)ip;
	if (inbytesleft) *inbytesleft = il;
	if (outbuf) *outbuf = (char *)op;
	if (outbytesleft) *outbytesleft = ol;
	return 0;
}
