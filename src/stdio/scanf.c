/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __vfscanf: the one parser every scanf/fscanf/sscanf variant calls
 * into.  sscanf/vsscanf hand it a throwaway read-only memory FILE (see
 * mem.c/fmemopen) instead of duplicating the character-at-a-time logic
 * against a plain string.
 *
 * C99 7.19.6.2p12 and POSIX fscanf make an input item "the longest
 * sequence of input bytes (up to any specified maximum field width)
 * which is an initial subsequence of a matching sequence", and then:
 * "If the input item is not a matching sequence, the execution of the
 * conversion specification fails; this condition is a matching
 * failure."  Note *initial subsequence*, not *matching sequence*: a
 * half-spelled "infi", a "0x" with no hex digit behind it and a "1e"
 * with no exponent behind it are all input items, all consumed in full,
 * and all matching failures.  Only "the offending input" -- the single
 * byte that could not extend the item -- is left unread.
 *
 * A matching sequence for %f has no length limit worth naming: leading
 * zeros, fraction digits and an exponent can run on forever, and a
 * correctly rounded result needs every one of them.  So the float
 * conversions walk the strtod grammar a character at a time, staging
 * the text of the field in a buffer that starts inside this frame and
 * moves to the heap when a field outgrows it.  A field that ends on a
 * matching sequence is handed to strtof/strtod/strtold, which round it
 * exactly; one that ends mid-spelling is a matching failure and is
 * simply dropped, its bytes already spent.  The integer conversions
 * need no buffer at all: they accumulate as they read, and saturate
 * rather than wrap when the digits run past the widest type.
 *
 * Because the item is never given back, the look-ahead is one byte
 * everywhere.  That one byte normally goes to the stream's own ungetc,
 * which C99 only promises for a single character and which can still
 * refuse it, so struct sc keeps a small stack behind it (see unrd
 * below) and seeks back whatever is left over at the end.
 *
 * %[...] scansets and the usual conversions are implemented; positional
 * arguments and vector-of-float %a/%A input conversions are not, since
 * nothing in this tree needs them.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <wchar.h>
#include "stdio_impl.h"

enum { LM_NONE, LM_hh, LM_h, LM_l, LM_ll, LM_j, LM_z, LM_t, LM_L };

/* Input cursor: every character actually taken from the stream bumps
 * nread, and every look-ahead character pushed back takes it off again,
 * so nread is exactly what %n has to report.
 *
 * A pushed-back character normally goes to the stream, but ungetc is
 * only guaranteed for one character and may refuse even that, so unrd
 * falls back to a stack of its own that rd drains before touching the
 * stream again.  Anything still on it when the whole scanf is over is
 * returned to the stream by seeking, the only way left to return it. */
struct sc {
	FILE *f;
	int nread;
	unsigned char *pb;      /* pb[npb-1] is the next character to hand back */
	int npb, pbcap;
	unsigned char pbinit[32];
};

/* The text of one numeric field.  Starts in the caller's frame and
 * grows on the heap; oom is sticky, and the caller turns it into a
 * matching failure, which is the only failure scanf can report. */
struct nbuf {
	char *p;
	int len, cap, oom;
	char init[128];
};

/* A width-limited view of the cursor: %20lf may take twenty characters
 * and no more, and a character given back is available to the field
 * again.  left < 0 means no width was given. */
struct fld {
	struct sc *sc;
	int left;
};

static void sc_init(struct sc *sc, FILE *f)
{
	sc->f = f;
	sc->nread = 0;
	sc->pb = sc->pbinit;
	sc->npb = 0;
	sc->pbcap = (int)sizeof sc->pbinit;
}

static int rd(struct sc *sc)
{
	int c;
	if (sc->npb) { sc->nread++; return sc->pb[--sc->npb]; }
	c = __fgetc(sc->f);
	if (c != EOF) sc->nread++;
	return c;
}

static void unrd(struct sc *sc, int c)
{
	if (c == EOF) return;
	/* The stream first: while its own pushback can hold the
	 * look-ahead, the cursor stays a plain wrapper around it. */
	if (sc->npb == 0 && ungetc(c, sc->f) != EOF) { sc->nread--; return; }
	if (sc->npb >= sc->pbcap) {
		unsigned char *q;
		int cap;
		if (sc->pbcap > INT_MAX / 2) return;
		cap = sc->pbcap * 2;
		q = sc->pb == sc->pbinit ? malloc((size_t)cap)
		                         : realloc(sc->pb, (size_t)cap);
		/* Nowhere to put it and nowhere to report it: the character
		 * is lost, exactly as an over-read look-ahead always was. */
		if (!q) return;
		if (sc->pb == sc->pbinit) memcpy(q, sc->pbinit, (size_t)sc->npb);
		sc->pb = q;
		sc->pbcap = cap;
	}
	sc->pb[sc->npb++] = (unsigned char)c;
	sc->nread--;
}

/* Hand back whatever look-ahead the stream's own pushback could not
 * take, and drop the stack.  A stream that cannot seek cannot be given
 * it back at all, which is the pre-existing cost of over-reading. */
static void sc_done(struct sc *sc)
{
	if (sc->npb) {
		/* A seek that cannot be done is not this call's failure to
		 * report, so it does not get to leave errno behind either. */
		int e = errno;
		if (fseek(sc->f, -(long)sc->npb, SEEK_CUR) == 0) sc->npb = 0;
		errno = e;
	}
	if (sc->pb != sc->pbinit) free(sc->pb);
	sc->pb = sc->pbinit;
	sc->npb = 0;
	sc->pbcap = (int)sizeof sc->pbinit;
}

static int skipspace(struct sc *sc)
{
	int c;
	while ((c = rd(sc)) != EOF && isspace(c)) ;
	return c;
}

static void nb_init(struct nbuf *b)
{
	b->p = b->init;
	b->len = 0;
	b->cap = (int)sizeof b->init;
	b->oom = 0;
}

static void nb_done(struct nbuf *b)
{
	if (b->p != b->init) free(b->p);
	b->p = b->init;
	b->cap = (int)sizeof b->init;
}

/* Append one character, keeping room for the terminator.  0 (and a
 * sticky oom) if the field cannot be staged. */
static int nb_put(struct nbuf *b, int c)
{
	if (b->len + 1 >= b->cap) {
		char *q;
		int cap;
		if (b->cap > INT_MAX / 2) { b->oom = 1; return 0; }
		cap = b->cap * 2;
		q = b->p == b->init ? malloc((size_t)cap)
		                    : realloc(b->p, (size_t)cap);
		if (!q) { b->oom = 1; return 0; }
		if (b->p == b->init) memcpy(q, b->init, (size_t)b->len);
		b->p = q;
		b->cap = cap;
	}
	b->p[b->len++] = (char)c;
	return 1;
}

static int fld_get(struct fld *fl)
{
	int c;
	if (fl->left == 0) return EOF;
	c = rd(fl->sc);
	if (c != EOF && fl->left > 0) fl->left--;
	return c;
}

static void fld_unget(struct fld *fl, int c)
{
	if (c == EOF) return;
	unrd(fl->sc, c);
	if (fl->left >= 0) fl->left++;
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Match one of the spellings of a named value, case-insensitively:
 * least characters make the short spelling ("inf"), the whole word the
 * long one ("infinity").  Every character that keeps the item an
 * initial subsequence of one of them is consumed, so a spelling that
 * stops in between ("infi") is consumed in full and is a matching
 * failure.  1 if a spelling matched, 0 if not, -1 out of memory.  c is
 * the first character, already read. */
static int scanword(struct fld *fl, struct nbuf *b, const char *word, int least, int c)
{
	int i = 0, ok = 0;
	for (;;) {
		if (c == EOF || tolower(c) != word[i]) break;
		if (!nb_put(b, c)) return -1;
		i++;
		ok = i == least || !word[i];
		if (!word[i]) { c = EOF; break; }
		c = fld_get(fl);
	}
	fld_unget(fl, c);
	return ok;
}

/* "nan", optionally followed by a parenthesised character sequence.
 * "nan(" and everything after it is an initial subsequence of a
 * "nan(n-char-sequence)", so an unterminated one is consumed in full
 * and is a matching failure rather than a bare "nan". */
static int scannan(struct fld *fl, struct nbuf *b, int c)
{
	int r = scanword(fl, b, "nan", 3, c);
	if (r <= 0) return r;
	c = fld_get(fl);
	if (c != '(') { fld_unget(fl, c); return 1; }
	if (!nb_put(b, c)) return -1;
	for (;;) {
		c = fld_get(fl);
		if (c == ')') return nb_put(b, c) ? 1 : -1;
		if (c == EOF || !(isalnum(c) || c == '_')) break;
		if (!nb_put(b, c)) return -1;
	}
	fld_unget(fl, c);
	return 0;
}

/* The digits of a mantissa, decimal or hexadecimal, with at most one
 * radix point.  Leading zeros are consumed but not staged: they say
 * nothing about the value, and a field of a hundred of them should not
 * cost a hundred bytes.  Returns the terminating character in *cp, 0 if
 * there was no digit at all, -1 out of memory. */
static int scandigits(struct fld *fl, struct nbuf *b, int base, int *cp)
{
	int c = *cp, any = 0, dot = 0, lead = 1, nd = 0;
	for (; ; c = fld_get(fl)) {
		if (c != EOF && (base == 16 ? hexval(c) >= 0 : isdigit(c))) {
			any = 1;
			if (c == '0' && lead && !dot) continue;
			if (c != '0') lead = 0;
			if (!nb_put(b, c)) return -1;
			nd++;
		} else if (c == '.' && !dot) {
			dot = 1;
			if (!nb_put(b, c)) return -1;
		} else {
			break;
		}
	}
	*cp = c;
	/* Every digit was a dropped leading zero: the value is zero, and
	 * the staged text still has to say so. */
	if (any && !nd && !nb_put(b, '0')) return -1;
	return any;
}

/* An exponent, if one is there in full: "e" or "p", an optional sign,
 * and at least one decimal digit.  A half-written one ("1.5e+") is
 * still an initial subsequence of a matching sequence, so it stays
 * consumed and makes the item as a whole a matching failure.  1 for a
 * complete exponent, 0 for a half-written one, -1 out of memory; the
 * terminating character comes back in *cp. */
static int scanexp(struct fld *fl, struct nbuf *b, int *cp)
{
	int c = *cp, ok = 0;
	if (!nb_put(b, c)) return -1;
	c = fld_get(fl);
	if (c == '+' || c == '-') {
		if (!nb_put(b, c)) return -1;
		c = fld_get(fl);
	}
	while (c != EOF && isdigit(c)) {
		ok = 1;
		if (!nb_put(b, c)) return -1;
		c = fld_get(fl);
	}
	*cp = c;
	return ok;
}

/* One floating-point field.  Consumes the whole input item -- the
 * longest prefix of the input that is an initial subsequence of a
 * strtod subject sequence, capped by the field width -- and returns 1
 * if that item is itself a subject sequence, with its text staged in b
 * ready to convert; 0 if it is not, which is a matching failure with
 * the item's bytes spent; -1 out of memory.  The one character handed
 * back is the offending input that ended the item, which POSIX leaves
 * unread. */
static int scanfloat(struct fld *fl, struct nbuf *b)
{
	int c, ok, any;

	c = fld_get(fl);
	if (c == '+' || c == '-') {
		if (!nb_put(b, c)) return -1;
		c = fld_get(fl);
	}
	if (c == 'i' || c == 'I') return scanword(fl, b, "infinity", 3, c);
	if (c == 'n' || c == 'N') return scannan(fl, b, c);

	if (c == '0') {
		int c2 = fld_get(fl);
		if (c2 == 'x' || c2 == 'X') {
			/* Past the prefix there must be a hex digit.  A "0x"
			 * with none behind it is still an initial subsequence
			 * of "0x1", so it is the item and a matching failure;
			 * it is not a "0" with the "x" handed back. */
			if (!nb_put(b, c)) return -1;
			if (!nb_put(b, c2)) return -1;
			c = fld_get(fl);
			any = scandigits(fl, b, 16, &c);
			if (any < 0) return -1;
			ok = any != 0;
			if (ok && (c == 'p' || c == 'P')) {
				ok = scanexp(fl, b, &c);
				if (ok < 0) return -1;
			}
			fld_unget(fl, c);
			return ok;
		}
		fld_unget(fl, c2);
	}

	any = scandigits(fl, b, 10, &c);
	if (any < 0) return -1;
	/* Not even a digit: a lone sign or radix point is an initial
	 * subsequence of a matching sequence and stays consumed, and if
	 * nothing at all was staged then nothing was consumed either. */
	if (!any) { fld_unget(fl, c); return 0; }
	ok = 1;
	if (c == 'e' || c == 'E') {
		ok = scanexp(fl, b, &c);
		if (ok < 0) return -1;
	}
	fld_unget(fl, c);
	return ok;
}

/* Out of memory staging a field.  scanf has no channel for ENOMEM, so
 * this becomes a matching failure -- but the field is drained first, so
 * the stream stops where it would have stopped and the directives that
 * would have followed see the same input either way. */
static void scandrain(struct fld *fl)
{
	int c;
	while ((c = fld_get(fl)) != EOF &&
	       (isalnum(c) || c == '.' || c == '+' || c == '-' ||
	        c == '(' || c == ')' || c == '_')) ;
	fld_unget(fl, c);
}

/* One input byte through mbrtowc(), for the l-modified %s, %c and %[.
 *
 * fscanf.html says the same sentence under all three: "If an l (ell)
 * qualifier is present, the input is a sequence of characters that
 * begins in the initial shift state.  Each character shall be converted
 * to a wide character as if by a call to the mbrtowc() function."
 *
 * Fed one byte at a time because that is how this scanner reads: a
 * partial sequence lives in the mbstate_t between calls, which is
 * exactly what mbrtowc's (size_t)-2 return is for.
 *
 * SURROGATE PAIRS ARE THE SUBTLE PART.  wchar_t is a 16-bit UTF-16 code
 * unit on this target, so a character above the BMP is TWO wchar_t from
 * ONE multibyte character: src/stdlib/mbrtowc.c hands back the high
 * surrogate and keeps the low one in the state, to be returned by a
 * later call that consumes nothing, with (size_t)-3.  A loop that
 * assumes one wide character per mbrtowc() call silently drops the low
 * surrogate.  It is drained here with n == 0, which cannot consume input
 * and which returns -2 harmlessly when nothing is pending -- note
 * mbrtowc checks its pending-surrogate state BEFORE it checks n, which
 * is what makes the zero-length call work.
 *
 * Returns 0, or -1 for an encoding error ([EILSEQ]). */
static int wide_put(int c, wchar_t *ws, int *nn, mbstate_t *st, int assign)
{
	char ch = (char)c;
	wchar_t wc;
	size_t r = mbrtowc(&wc, &ch, 1, st);

	if (r == (size_t)-1) return -1;
	if (r == (size_t)-2) return 0;          /* incomplete; more bytes needed */
	if (assign) ws[*nn] = wc;
	(*nn)++;
	while (mbrtowc(&wc, &ch, 0, st) == (size_t)-3) {
		if (assign) ws[*nn] = wc;
		(*nn)++;
	}
	return 0;
}

/* ------------------------------------------------------------------
 * FORMAT CURSOR
 *
 * The directive scanner below reads its format through gf() and steps
 * by `st` bytes rather than dereferencing a char*, so that one scanner
 * serves both fscanf() (st == 1, a byte format) and, once the wide
 * entry points exist, fwscanf() (st == sizeof(wchar_t)).  Every
 * character a conversion specification can contain is ASCII, and the
 * <ctype.h> functions used here are range tests that are false above
 * 0x7f (src/ctype/isspace.c and friends), so a wide format unit of
 * 0x1234 behaves exactly as a non-directive byte does -- no
 * wide-specific classification is needed or wanted.
 *
 * The cursor was renamed from `p` to `fp` in the same change, and
 * deliberately: renaming it makes any site that still dereferences the
 * old pointer directly fail to COMPILE rather than silently read one
 * byte of a wide format unit.  A stride refactor whose misses are
 * invisible at st == 1 is exactly the kind that ships a latent bug.
 *
 * `st` is a size_t, not an int, for the same reason src/stdlib/strtod.c
 * gives at its own cursor: it is a byte stride and every use of it is
 * pointer arithmetic.  As an int, the %[ range scanner's `fp += 2 * st`
 * computed the step in int and widened the product to ptrdiff_t
 * afterwards (clang-tidy
 * bugprone-implicit-widening-of-multiplication-result, on 64-bit
 * targets only, where ptrdiff_t is wider than int).  Nothing truncated
 * -- st is 1 or sizeof(wchar_t) -- but the type, not a cast at the one
 * site the analyzer happened to reach, is what makes the arithmetic
 * right.  gf() being a macro means the declared type of `st` is the
 * only thing that decides that width.
 * ------------------------------------------------------------------ */
/* A MACRO, not a static function, and measured rather than assumed.
 * The shipped compiler for this target is tcc, which does no inlining
 * at all, so a fetch helper written as a function is a real call per
 * format character.  Benchmarked over 300000 iterations of eight
 * sscanf() calls: 0.79-0.82s with the pre-refactor scanner, 0.92-0.99s
 * with a function-call fetch -- about 17% -- and back to the
 * pre-refactor time with the macro below.  Nothing about the
 * abstraction changes; only whether the compiler is given the chance to
 * fold `st` away at each site. */
#define gf(q, s) ((s) == 1 ? (unsigned)(unsigned char)*(q) \
                           : (unsigned)*(const wchar_t *)(const void *)(q))

static int vfscanf_st(FILE *f, const char *fmt, va_list ap, size_t st)
{
	int nmatched = 0, gotEOF = 0, ilseq = 0;
	const char *fp = fmt;
	int c = 0;
	struct sc sc;

	sc_init(&sc, f);
	for (; gf(fp, st); fp += st) {
		if (isspace((int)gf(fp, st))) {
			c = skipspace(&sc);
			unrd(&sc, c);
			continue;
		}
		if (gf(fp, st) != '%') {
			c = rd(&sc);
			if (c == EOF) { gotEOF = 1; goto done; }
			if ((unsigned)c != gf(fp, st)) { unrd(&sc, c); goto done; }
			continue;
		}
		fp += st;
		if (gf(fp, st) == '%') {
			c = rd(&sc);
			if (c == EOF) { gotEOF = 1; goto done; }
			if (c != '%') { unrd(&sc, c); goto done; }
			continue;
		}

		{
			int assign = 1, width = -1, lm = LM_NONE;
			if (gf(fp, st) == '*') { assign = 0; fp += st; }
			while (gf(fp, st) >= '0' && gf(fp, st) <= '9') { if (width < 0) width = 0; width = width * 10 + (int)(gf(fp, st) - '0'); fp += st; }
			for (;;) {
				if (gf(fp, st) == 'h') { lm = lm == LM_h ? LM_hh : LM_h; fp += st; }
				else if (gf(fp, st) == 'l') { lm = lm == LM_l ? LM_ll : LM_l; fp += st; }
				else if (gf(fp, st) == 'j') { lm = LM_j; fp += st; }
				else if (gf(fp, st) == 'z') { lm = LM_z; fp += st; }
				else if (gf(fp, st) == 't') { lm = LM_t; fp += st; }
				else if (gf(fp, st) == 'L') { lm = LM_L; fp += st; }
				else break;
			}

			switch ((int)gf(fp, st)) {
			case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
				int base = (gf(fp, st) == 'o') ? 8 : (gf(fp, st) == 'x' || gf(fp, st) == 'X') ? 16 : 10;
				int autodetect = gf(fp, st) == 'i';
				int issigned = gf(fp, st) == 'd' || gf(fp, st) == 'i';
				int neg = 0, any = 0, ovf = 0;
				unsigned long long uv = 0;
				struct fld fl;

				c = skipspace(&sc);
				if (c == EOF) { gotEOF = 1; goto done; }
				unrd(&sc, c);
				/* The field width counts every character of the
				 * item, the sign and any "0x" included. */
				fl.sc = &sc;
				fl.left = width;
				c = fld_get(&fl);
				if (c == '+' || c == '-') { neg = c == '-'; c = fld_get(&fl); }
				if ((autodetect || base == 16) && c == '0') {
					int c2 = fld_get(&fl);
					if (c2 == 'x' || c2 == 'X') {
						int c3 = fld_get(&fl);
						/* "0x" with no hex digit behind it is an
						 * initial subsequence of "0x1" and nothing
						 * shorter, so the item is the whole "0x" and
						 * it is a matching failure -- not a "0" with
						 * the "x" handed back. */
						if (hexval(c3) < 0) { fld_unget(&fl, c3); goto done; }
						base = 16;
						c = c3;
					} else {
						any = 1;   /* the "0" is already a complete item */
						if (autodetect) base = 8;
						c = c2;
					}
				}
				if (autodetect && base != 16 && base != 8) base = 10;
				for (; c != EOF; c = fld_get(&fl)) {
					unsigned d;
					if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
					else if (c >= 'a' && c <= 'z') d = (unsigned)(c - 'a' + 10);
					else if (c >= 'A' && c <= 'Z') d = (unsigned)(c - 'A' + 10);
					else break;
					if (d >= (unsigned)base) break;
					/* Every digit of the item is consumed, however
					 * many there are; a value too wide for the
					 * widest type saturates the way strtoull would
					 * rather than quietly wrapping. */
					if (uv > (ULLONG_MAX - d) / (unsigned)base) ovf = 1;
					else uv = uv * (unsigned)base + d;
					any = 1;
				}
				fld_unget(&fl, c);
				if (!any) goto done;
				if (ovf)
					uv = issigned ? (neg ? (unsigned long long)LLONG_MIN
					                     : (unsigned long long)LLONG_MAX)
					              : ULLONG_MAX;
				/* The negation is done on the unsigned value, never
				 * on a signed one: -(long long)uv is undefined for
				 * LLONG_MIN, whose magnitude a long long cannot hold,
				 * while unsigned negation wraps modulo 2**64 (C99
				 * 6.2.5p9) to exactly the bits wanted. */
				else if (neg) uv = __neg_mag(uv);
				if (assign) {
					switch (lm) {
					case LM_hh: *(unsigned char *)va_arg(ap, void *) = (unsigned char)uv; break;
					case LM_h:  *(unsigned short *)va_arg(ap, void *) = (unsigned short)uv; break;
					case LM_l:  *(unsigned long *)va_arg(ap, void *) = (unsigned long)uv; break;
					case LM_ll: case LM_j: *(unsigned long long *)va_arg(ap, void *) = uv; break;
					case LM_z: *(size_t *)va_arg(ap, void *) = (size_t)uv; break;
					case LM_t: *(ptrdiff_t *)va_arg(ap, void *) = (ptrdiff_t)uv; break;
					default: *(unsigned int *)va_arg(ap, void *) = (unsigned int)uv; break;
					}
					nmatched++;
				}
				break;
			}
			case 'f': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
				struct fld fl;
				struct nbuf nb;
				int r;

				c = skipspace(&sc);
				if (c == EOF) { gotEOF = 1; goto done; }
				unrd(&sc, c);
				fl.sc = &sc;
				fl.left = width;
				nb_init(&nb);
				r = scanfloat(&fl, &nb);
				if (r <= 0) {
					if (r < 0) scandrain(&fl);
					nb_done(&nb);
					goto done;
				}
				nb.p[nb.len] = 0;
				if (assign) {
					/* The staged text is a complete subject
					 * sequence, so each of these converts all of
					 * it, in the destination's own precision. */
					if (lm == LM_L) *(long double *)va_arg(ap, void *) = strtold(nb.p, 0);
					else if (lm == LM_l) *(double *)va_arg(ap, void *) = strtod(nb.p, 0);
					else *(float *)va_arg(ap, void *) = strtof(nb.p, 0);
					nmatched++;
				}
				nb_done(&nb);
				break;
			}
			case 's': {
				if (lm == LM_l) {
					wchar_t *ws = assign ? va_arg(ap, wchar_t *) : 0;
					mbstate_t mbs;
					int nn = 0, nb = 0;
					memset(&mbs, 0, sizeof mbs);
					c = skipspace(&sc);
					if (c == EOF) { gotEOF = 1; goto done; }
					for (; c != EOF && !isspace(c) && (width < 0 || nb < width); c = rd(&sc)) {
						if (wide_put(c, ws, &nn, &mbs, assign) < 0) { ilseq = 1; goto done; }
						nb++;
					}
					unrd(&sc, c);
					/* a sequence left half-finished is an encoding error
					 * too: there are no more bytes that could complete it */
					if (!mbsinit(&mbs)) { ilseq = 1; goto done; }
					if (nb == 0) goto done;
					if (assign) { ws[nn] = 0; nmatched++; }
					break;
				}
				{
				char *s = assign ? va_arg(ap, char *) : 0;
				int nn = 0;
				c = skipspace(&sc);
				if (c == EOF) { gotEOF = 1; goto done; }
				for (; c != EOF && !isspace(c) && (width < 0 || nn < width); c = rd(&sc)) {
					if (assign) s[nn] = (char)c;
					nn++;
				}
				unrd(&sc, c);
				if (nn == 0) goto done;
				if (assign) { s[nn] = 0; nmatched++; }
				}
				break;
			}
			case 'c': {
				if (lm == LM_l) {
					wchar_t *ws = assign ? va_arg(ap, wchar_t *) : 0;
					mbstate_t mbs;
					/* fscanf.html's c entry: "Matches a sequence of bytes
					 * of the number specified by the field width (1 if no
					 * field width is present)" -- the width is a BYTE
					 * count, and the l qualifier converts those bytes
					 * rather than changing what the width counts.  (C99
					 * is arguably read the other way for %lc; POSIX is
					 * the spec this suite audits against and it says
					 * bytes.)  No null byte is added, here or below. */
					int w = width < 0 ? 1 : width, nb, nn = 0;
					memset(&mbs, 0, sizeof mbs);
					for (nb = 0; nb < w; nb++) {
						c = rd(&sc);
						if (c == EOF) break;
						if (wide_put(c, ws, &nn, &mbs, assign) < 0) { ilseq = 1; goto done; }
					}
					if (nb == 0) { gotEOF = 1; goto done; }
					if (!mbsinit(&mbs)) { ilseq = 1; goto done; }
					if (assign) nmatched++;
					break;
				}
				{
				char *s = assign ? va_arg(ap, char *) : 0;
				int w = width < 0 ? 1 : width, nn;
				for (nn = 0; nn < w; nn++) {
					c = rd(&sc);
					if (c == EOF) break;
					if (assign) s[nn] = (char)c;
				}
				if (nn == 0) { gotEOF = 1; goto done; }
				if (assign) nmatched++;
				}
				break;
			}
			case '[': {
				unsigned char set[256] = {0};
				/* One va_arg for both widths: the caller's pointer is
				 * char * without the l qualifier and wchar_t * with it,
				 * and both are object pointers of the same
				 * representation on this target, so it is fetched once
				 * and cast at the point of use. */
				char *s = assign ? va_arg(ap, char *) : 0;
				int neg = 0, nn = 0;
				fp += st;
				if (gf(fp, st) == '^') { neg = 1; fp += st; }
				{
					const char *start = fp;
					do {
						if (gf(fp, st) == '-' && gf(fp + st, st) && gf(fp + st, st) != ']' && fp != start) {
							unsigned a = gf(fp - st, st), b = gf(fp + st, st), k;
							if (a < 256 && b < 256) for (k = a; k <= b; k++) set[k] = 1;
							fp += 2 * st;
						} else {
							if (gf(fp, st) < 256) set[gf(fp, st)] = 1;
							fp += st;
						}
					} while (gf(fp, st) && gf(fp, st) != ']');
					/* fp now at the closing ']'; the outer loop steps past it */
				}
				if (lm == LM_l) {
					mbstate_t mbs;
					int nb = 0;
					nn = 0;
					memset(&mbs, 0, sizeof mbs);
					c = rd(&sc);
					while (c != EOF && (set[(unsigned char)c] != 0) != neg && (width < 0 || nb < width)) {
						if (wide_put(c, (wchar_t *)s, &nn, &mbs, assign) < 0) { ilseq = 1; goto done; }
						nb++;
						c = rd(&sc);
					}
					unrd(&sc, c);
					if (!mbsinit(&mbs)) { ilseq = 1; goto done; }
					if (nb == 0) { if (c == EOF) gotEOF = 1; goto done; }
					if (assign) { ((wchar_t *)s)[nn] = 0; nmatched++; }
					break;
				}
				c = rd(&sc);
				while (c != EOF && (set[(unsigned char)c] != 0) != neg && (width < 0 || nn < width)) {
					if (assign) s[nn] = (char)c;
					nn++;
					c = rd(&sc);
				}
				unrd(&sc, c);
				if (nn == 0) { if (c == EOF) gotEOF = 1; goto done; }
				if (assign) { s[nn] = 0; nmatched++; }
				break;
			}
			case 'p': {
				void **pp = assign ? va_arg(ap, void **) : 0;
				unsigned long long uv = 0; int any = 0;
				c = skipspace(&sc);
				if (c == EOF) { gotEOF = 1; goto done; }
				if (c == '0') { int c2 = rd(&sc); if (c2 == 'x' || c2 == 'X') c = rd(&sc); else unrd(&sc, c2); }
				for (; c != EOF; c = rd(&sc)) {
					int d;
					if (c >= '0' && c <= '9') d = c - '0';
					else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
					else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
					else break;
					uv = uv * 16 + (unsigned)d; any = 1;
				}
				unrd(&sc, c);
				if (!any) goto done;
				if (assign) { *pp = (void *)(uintptr_t)uv; nmatched++; }
				break;
			}
			case 'n':
				if (assign) {
					/* %n never counts toward the return value */
					switch (lm) {
					case LM_hh: *(signed char *)va_arg(ap, void *) = (signed char)sc.nread; break;
					case LM_h: *(short *)va_arg(ap, void *) = (short)sc.nread; break;
					case LM_l: *(long *)va_arg(ap, void *) = sc.nread; break;
					case LM_ll: case LM_j: *(long long *)va_arg(ap, void *) = sc.nread; break;
					case LM_z: *(size_t *)va_arg(ap, void *) = (size_t)sc.nread; break;
					case LM_t: *(ptrdiff_t *)va_arg(ap, void *) = sc.nread; break;
					default: *(int *)va_arg(ap, void *) = sc.nread; break;
					}
				}
				break;
			default:
				break;
			}
		}
	}
done:
	sc_done(&sc);
	/* scanf.html ERRORS, shall fail: "[EILSEQ] Input byte sequence does
	 * not form a valid character."  This is a READ error, not a matching
	 * failure: RETURN VALUE says EOF "if an input failure occurs before
	 * any conversion", and the stream's error indicator has to be set so
	 * ferror() can tell it apart from end-of-file. */
	if (ilseq) { f->err = 1; errno = EILSEQ; return EOF; }
	return (nmatched == 0 && gotEOF) ? EOF : nmatched;
}

int __vfscanf(FILE *f, const char *fmt, va_list ap)
{
	return vfscanf_st(f, fmt, ap, 1);
}

int vfscanf(FILE *__restrict f, const char *__restrict fmt, __isoc_va_list ap)
{
	return __vfscanf(f, fmt, ap);
}
int vscanf(const char *__restrict fmt, __isoc_va_list ap)
{
	return __vfscanf(stdin, fmt, ap);
}

static int vsscanf_impl(const char *s, const char *fmt, va_list ap)
{
	FILE mf;
	int r;
	memset(&mf, 0, sizeof mf);
	mf.fd = -1;
	mf.pid = -1;
	mf.is_mem = 1;
	mf.readable = 1;
	mf.mem_buf = (unsigned char *)s;
	mf.mem_len = strlen(s);
	mf.mem_size = mf.mem_len;
	r = __vfscanf(&mf, fmt, ap);
	/* __fill gives even a memory-backed FILE a read buffer, and this
	 * one is a local that never sees fclose, so releasing it is ours
	 * to do -- otherwise every sscanf would leak a BUFSIZ block. */
	free(mf.buf);
	return r;
}

int vsscanf(const char *__restrict s, const char *__restrict fmt, __isoc_va_list ap)
{
	return vsscanf_impl(s, fmt, ap);
}

int fscanf(FILE *__restrict f, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfscanf(f, fmt, ap);
	va_end(ap);
	return r;
}
int scanf(const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfscanf(stdin, fmt, ap);
	va_end(ap);
	return r;
}
int sscanf(const char *__restrict s, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vsscanf_impl(s, fmt, ap);
	va_end(ap);
	return r;
}
