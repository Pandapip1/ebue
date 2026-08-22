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

int __vfscanf(FILE *f, const char *fmt, va_list ap)
{
	int nmatched = 0, gotEOF = 0;
	const char *p = fmt;
	int c = 0;
	struct sc sc;

	sc_init(&sc, f);
	for (; *p; p++) {
		if (isspace((unsigned char)*p)) {
			c = skipspace(&sc);
			unrd(&sc, c);
			continue;
		}
		if (*p != '%') {
			c = rd(&sc);
			if (c == EOF) { gotEOF = 1; goto done; }
			if (c != *p) { unrd(&sc, c); goto done; }
			continue;
		}
		p++;
		if (*p == '%') {
			c = rd(&sc);
			if (c == EOF) { gotEOF = 1; goto done; }
			if (c != '%') { unrd(&sc, c); goto done; }
			continue;
		}

		{
			int assign = 1, width = -1, lm = LM_NONE;
			if (*p == '*') { assign = 0; p++; }
			while (*p >= '0' && *p <= '9') { if (width < 0) width = 0; width = width * 10 + (*p++ - '0'); }
			for (;;) {
				if (*p == 'h') { lm = lm == LM_h ? LM_hh : LM_h; p++; }
				else if (*p == 'l') { lm = lm == LM_l ? LM_ll : LM_l; p++; }
				else if (*p == 'j') { lm = LM_j; p++; }
				else if (*p == 'z') { lm = LM_z; p++; }
				else if (*p == 't') { lm = LM_t; p++; }
				else if (*p == 'L') { lm = LM_L; p++; }
				else break;
			}

			switch (*p) {
			case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
				int base = (*p == 'o') ? 8 : (*p == 'x' || *p == 'X') ? 16 : 10;
				int autodetect = *p == 'i';
				int issigned = *p == 'd' || *p == 'i';
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
				break;
			}
			case 'c': {
				char *s = assign ? va_arg(ap, char *) : 0;
				int w = width < 0 ? 1 : width, nn;
				for (nn = 0; nn < w; nn++) {
					c = rd(&sc);
					if (c == EOF) break;
					if (assign) s[nn] = (char)c;
				}
				if (nn == 0) { gotEOF = 1; goto done; }
				if (assign) nmatched++;
				break;
			}
			case '[': {
				unsigned char set[256] = {0};
				char *s = assign ? va_arg(ap, char *) : 0;
				int neg = 0, nn = 0;
				p++;
				if (*p == '^') { neg = 1; p++; }
				{
					const char *start = p;
					do {
						if (*p == '-' && p[1] && p[1] != ']' && p != start) {
							int a = (unsigned char)p[-1], b = (unsigned char)p[1], k;
							for (k = a; k <= b; k++) set[k] = 1;
							p += 2;
						} else {
							set[(unsigned char)*p] = 1;
							p++;
						}
					} while (*p && *p != ']');
					/* p now at the closing ']'; the outer for(;*p;p++) will step past it */
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
	return (nmatched == 0 && gotEOF) ? EOF : nmatched;
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
