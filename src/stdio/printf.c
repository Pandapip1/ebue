/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __vfprintf: the one formatter every printf/fprintf/sprintf/snprintf
 * variant calls into.  sprintf and snprintf are __vfprintf writing into
 * a throwaway FILE that is a fixed-size memory buffer exactly like
 * fmemopen's (see mem.c and __file_write in buf.c) but built on the
 * stack, so truncation and buffer-filling logic is not written twice.
 *
 * Floating-point conversions (%f/%e/%g and their capitals) are exact.
 * A finite double is m * 2^e for integers m < 2^53 and e, so its value
 * is a binary rational and its decimal expansion terminates: dec_exact
 * below computes every digit of it with one big-integer multiply, and
 * dec_round then rounds that expansion to the requested number of
 * places, to nearest with ties to even.  Every digit printed is
 * therefore the digit glibc and musl print.  %a/%A are exact for the
 * same reason and more directly: a double's significand is already 13
 * hex digits, so they are read straight out of the bits.
 * Positional (%n$) arguments are implemented, and an ordinary
 * unnumbered format does not pay for them: see THE ARGUMENT LIST below.
 *
 * No conversion sizes anything from the caller's precision, which C99
 * 7.19.6.1 leaves unbounded -- see PREC_MAX below.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <wchar.h>
#include <errno.h>
#include "stdio_impl.h"

enum { LM_NONE, LM_hh, LM_h, LM_l, LM_ll, LM_j, LM_z, LM_t, LM_L };

/* A precision is an int and C99 7.19.6.1 puts no bound on it, so no
 * buffer may be sized from one.  PREC_MAX is the largest precision
 * fmt_f/fmt_e ever write out in full: the exact expansion of a double
 * runs to at most 767 significant digits and its last fractional place
 * is the 1074th (the smallest subnormal is 2^-1074), so every place
 * past PREC_MAX is a zero no matter what the value is.  emit_float
 * formats with the precision clamped to PREC_MAX and streams the
 * dropped zeros straight out, which makes the body length independent
 * of the caller's precision. */
#define PREC_MAX 1080
/* Worst-case body: 309 integer digits (DBL_MAX at %f), a point and
 * PREC_MAX fractional digits -- 1390 bytes, rounded up.  The other
 * shapes are smaller: %e is one digit, a point, PREC_MAX more and a
 * five-byte exponent, and %g hands fmt_f a precision of P-1-decexp,
 * which makes its body PREC_MAX+1 whatever decexp is. */
#define BODYMAX 1536

/* ------------------------------------------------------------------
 * FORMAT CURSOR
 *
 * The directive scanner reads its format through gf() and steps by `st`
 * bytes, so that one scanner serves fprintf() (a byte format) and,
 * once the wide entry points exist, fwprintf() (a wide one).  Every
 * character a conversion specification can contain is ASCII.
 *
 * A MACRO rather than a static function, and measured rather than
 * assumed: the compiler this library ships through is tcc, which does
 * no inlining, so a fetch helper written as a function is a real call
 * per format character.  The identical change cost 17% in
 * src/stdio/scanf.c (a7c2277).
 *
 * `st` is an int, and the width of the pointer arithmetic below is
 * decided by that declared type rather than by a cast at any one site.
 *
 * The cursor is named `fp`, not `p`, deliberately: renaming it makes
 * any site that still dereferences the old pointer a COMPILE error
 * rather than a silent one-byte read of a wide format unit.  At st == 1
 * such a miss behaves perfectly and is invisible to every test, which
 * is the whole hazard of a stride refactor.
 * ------------------------------------------------------------------ */
#define gf(q, s) ((s) == 1 ? (unsigned)(unsigned char)*(q) \
	                           : (unsigned)*(const wchar_t *)(q))

/* ------------------------------------------------------------------
 * THE SINK
 *
 * Everything this formatter emits goes through out() below, so that one
 * body of code can serve fprintf() -- bytes, and a return counted in
 * bytes -- and fwprintf(), whose return is "the number of wide
 * characters transmitted" (fwprintf.html RETURN VALUE).
 *
 * KNOWN RESIDUAL COST, measured, so nobody re-derives it and reaches for
 * the alternative without the argument.  One formatter instead of two
 * costs about 5.9% here: 1.180s -> 1.250s over eight rounds of 500000
 * iterations of five snprintf() calls, uninstrumented, x86_64-win32-tcc
 * under Wine, the variants interleaved in one loop and minima taken.
 * That residual is the struct indirection below plus the `st == 1`
 * branch per format character; two further costs that were NOT
 * inherent have already been removed (out() re-deriving
 * `wide && f->wmem` on every call, and memsetting the whole struct
 * where only the mbstate_t needs zeroing -- together they were the
 * difference between 11.0% and 5.9%).
 *
 * Removing the last 5.9% would mean compiling this formatter twice from
 * a template, one instantiation per stride.  CONSIDERED AND DECLINED,
 * and the reason is correctness rather than effort: %ls and %lc are
 * written once for four argument/sink combinations precisely so they
 * cannot drift, and two instantiations reintroduce exactly that
 * surface -- a conversion added to one and not the other is a defect no
 * differential test catches, because both are generated from one source
 * and look consistent.  This library's consumers are configure, gcc,
 * tcc and sh, all compile-bound or I/O-bound, where 5.9% of the
 * formatter is not measurable.  Revisit only with a real workload that
 * shows printf dominating.
 *
 * `count` and `bad` moved off the parameter lists and into the struct
 * deliberately, and not for tidiness: the signature change makes every
 * one of the ~40 call sites a compile error until it is converted,
 * which is the only way to be sure none was missed.  A refactor whose
 * misses still compile is a refactor whose misses ship.
 *
 * `ost` is the conversion state for encoding wide characters onto a
 * BYTE stream.  It lives here rather than in a local because a
 * supplementary character is two wchar_t on this target: wcrtomb()
 * holds the high surrogate and writes nothing until its partner
 * arrives, so the state must survive between the units of one %ls.
 * Nothing uses it yet; it arrives with the conversions that need it.
 * ------------------------------------------------------------------ */
struct sink {
	FILE *f;
	int wide;       /* emit wide characters, and count them */
	int widemem;    /* wide AND the buffer holds wchar_t: precomputed,
	                 * because out() is the hottest function here and
	                 * tcc will not hoist the two loads itself */
	long count;     /* logical (untruncated) total, in sink units */
	int bad;
	mbstate_t ost;
};

/* sk is required by every function in this file that takes one: each
 * dereferences it unconditionally, first statement in most cases
 * (`sk->bad`/`sk->count` below), the caller's own struct sink on the
 * stack (vfprintf_st's own `struct sink sink, *sk = &sink;`), never a
 * value that could legitimately be null. */
static int count_fits(struct sink *sk, size_t n) __attribute__((nonnull(1)));
static int count_fits(struct sink *sk, size_t n)
{
	if (n <= (size_t)(INT_MAX - sk->count)) return 1;
	errno = EOVERFLOW;
	sk->f->err = 1;
	sk->bad = 1;
	return 0;
}

/* Emit n bytes of ASCII text -- everything this file GENERATES (digits,
 * signs, padding, exponents, "0x", "(nil)", "(null)"), and nothing that
 * came from a caller's string.  That restriction is what makes a wide
 * sink almost free: an ASCII wide character encodes to the very byte it
 * already is, so on a byte-backed stream the bytes written are the same
 * and the count is the same number either way -- one unit per byte.
 * The one case that must convert is a buffer that holds wchar_t rather
 * than their encoding, which is what f->wmem marks.
 *
 * A short write is a real error unless f is a fixed memory buffer
 * (sprintf/snprintf), in which case it is just truncation. */
static void out(struct sink *sk, const char *s, size_t n) __attribute__((nonnull(1)));
static void out(struct sink *sk, const char *s, size_t n)
{
	if (sk->bad) return;
	if (!count_fits(sk, n)) return;
	if (sk->widemem) {
		while (n) {
			wchar_t stage[32];
			size_t k = n < 32 ? n : 32, i;
			for (i = 0; i < k; i++) stage[i] = (wchar_t)(unsigned char)s[i];
			if (__fwrite(stage, sizeof *stage, k, sk->f) != k) {
				if (!sk->f->is_mem || sk->f->mem_dynamic) { sk->f->err = 1; sk->bad = 1; return; }
			}
			sk->count += (long)k;
			s += k; n -= k;
		}
		return;
	}
	if (n && __fwrite(s, 1, n, sk->f) != n) {
		if (!sk->f->is_mem || sk->f->mem_dynamic) { sk->f->err = 1; sk->bad = 1; return; }
	}
	sk->count += (long)n;
}

static void pad(struct sink *sk, char c, size_t n) __attribute__((nonnull(1)));
static void pad(struct sink *sk, char c, size_t n) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	char buf[16];
	size_t emit = n;
	size_t skipped = 0;

	if (sk->bad || !count_fits(sk, n)) return;
	/* A fixed memory sink still counts everything snprintf()/swprintf()
	 * would have written, but bytes past its capacity are discarded.  Do
	 * not visit that discarded tail 16 bytes at a time: besides making a
	 * perfectly valid large width take linear time, that would make the
	 * aggregate INT_MAX return-value check practically unreachable.  The
	 * memory bookkeeping is in bytes; a wide memory sink consumes one
	 * wchar_t-sized unit for each padding character. */
	if (sk->f->is_mem && !sk->f->mem_dynamic) {
		size_t avail = sk->f->mem_pos < sk->f->mem_size
		             ? sk->f->mem_size - sk->f->mem_pos : 0;
		if (sk->widemem) avail /= sizeof(wchar_t);
		if (emit > avail) { skipped = emit - avail; emit = avail; }
	}
	memset(buf, c, sizeof buf);
	while (emit && !sk->bad) {
		size_t k = emit < sizeof buf ? emit : sizeof buf;
		out(sk, buf, k);
		emit -= k;
	}
	/* count_fits() above proved this whole run representable.  out()
	 * counted the stored prefix; account for the fixed buffer's discarded
	 * tail without touching it. */
	if (!sk->bad) sk->count += (long)skipped;
}

/* Emit n wide characters that came from a CALLER (%ls, %lc, or a %s
 * converted up into a wide sink).  Unlike out(), these can be anything,
 * so the ASCII shortcut it takes is not available here.
 *
 * On a byte-backed stream each unit is encoded with wcrtomb() through
 * sk->ost, which is why that state lives in the sink: a supplementary
 * character is TWO wchar_t on this target, and wcrtomb() answers 0 for
 * the high surrogate -- accepted, nothing written -- holding it until
 * the low one arrives.  The count still advances, because what is
 * counted is wide characters. */
static void out_units(struct sink *sk, const wchar_t *w, size_t n) __attribute__((nonnull(1)));
static void out_units(struct sink *sk, const wchar_t *w, size_t n)
{
	if (sk->bad) return;
	if (!count_fits(sk, n)) return;
	if (sk->f->wmem) {
		if (n && __fwrite(w, sizeof *w, n, sk->f) != n) {
			if (!sk->f->is_mem || sk->f->mem_dynamic) { sk->f->err = 1; sk->bad = 1; return; }
		}
		sk->count += (long)n;
		return;
	}
	{
		size_t i;
		char buf[MB_LEN_MAX];
		for (i = 0; i < n; i++) {
			size_t r = wcrtomb(buf, w[i], &sk->ost);
			if (r == (size_t)-1) { sk->f->err = 1; sk->bad = 1; return; }
			if (r && __fwrite(buf, 1, r, sk->f) != r) {
				if (!sk->f->is_mem || sk->f->mem_dynamic) { sk->f->err = 1; sk->bad = 1; return; }
			}
			sk->count++;
		}
	}
}

/* ------------------------------------------------------------------
 * STRING AND CHARACTER ARGUMENTS
 *
 * The one place in this formatter where what is emitted did not come
 * from this file.  BOTH sides vary independently -- the argument is a
 * char * or a wchar_t *, and the sink counts bytes or wide characters
 * -- so there are four cases, two copies and two conversions:
 *
 *   char *   -> bytes     copy       fprintf  "%s"
 *   wchar_t* -> bytes     wcrtomb    fprintf  "%ls"
 *   wchar_t* -> wide      copy       fwprintf "%ls"
 *   char *   -> wide      mbrtowc    fwprintf "%s"
 *
 * fprintf.html, the s conversion: with an l qualifier the wide
 * characters "shall be converted to characters (each as if by a call to
 * the wcrtomb() function)"; "if a precision is specified, no more than
 * that many bytes shall be written", and "a partial character shall not
 * be written".  fwprintf.html says the mirror image for a plain %s --
 * bytes converted "as if by repeated calls to mbrtowc()", with the
 * precision counting wide characters.  In both directions the precision
 * and the field width are measured in the SINK's unit, never the
 * argument's.
 *
 * One function measures and emits, called twice: once with emit == 0 to
 * get the length the field width must pad against, and once with
 * emit == 1 to write it.  Two passes rather than a staging buffer
 * because a string argument has no bound -- the same reason nothing
 * here is ever sized from a caller's precision (see PREC_MAX).
 * ------------------------------------------------------------------ */
/* arg is required too: every one of the four branches below
 * dereferences it, whichever is taken (strlen(s), w[n], *w, or *s). */
static long str_arg(struct sink *sk, const void *arg, int wide_arg, int prec, int emit)
    __attribute__((nonnull(1, 2)));
static long str_arg(struct sink *sk, const void *arg, int wide_arg, int prec, int emit) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	mbstate_t st;
	long units = 0;

	memset(&st, 0, sizeof st);

	if (!wide_arg && !sk->wide) {			/* char * -> bytes */
		const char *s = arg;
		size_t n = strlen(s);
		if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
		if (emit) out(sk, s, n);
		return (long)n;
	}
	if (wide_arg && sk->wide) {			/* wchar_t * -> wide */
		const wchar_t *w = arg;
		size_t n = 0;
		while (w[n] && (prec < 0 || n < (size_t)prec)) n++;
		if (emit) out_units(sk, w, n);
		return (long)n;
	}
	if (wide_arg) {					/* wchar_t * -> bytes */
		const wchar_t *w = arg;
		char buf[MB_LEN_MAX];
		for (; *w; w++) {
			size_t r = wcrtomb(buf, *w, &st);
			if (r == (size_t)-1) break;	/* [EILSEQ]: stop here */
			/* "a partial character shall not be written": one that
			 * does not fit inside the precision ENTIRELY is not
			 * written at all. */
			if (prec >= 0 && units + (long)r > prec) break;
			units += (long)r;
			if (emit && r) out(sk, buf, r);
		}
		return units;
	}
	{						/* char * -> wide */
		const char *s = arg;
		while (*s || !mbsinit(&st)) {
			wchar_t wc = 0;
			size_t r = mbrtowc(&wc, s, MB_LEN_MAX, &st);
			size_t used;
			if (r == (size_t)-1 || r == (size_t)-2) break;
			/* (size_t)-3 is a low surrogate delivered from state
			 * alone, consuming nothing: the second half of a
			 * supplementary character, and a wide character of its
			 * own. */
			used = r == (size_t)-3 ? 0 : r;
			if (prec >= 0 && units >= prec) break;
			units++;
			if (emit) out_units(sk, &wc, 1);
			s += used;
			if (!used && !*s && mbsinit(&st)) break;
		}
		return units;
	}
}

/* %s and %ls: measure, pad, emit, pad. */
static void emit_str(struct sink *sk, const void *arg, int wide_arg, int prec, // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
                     int flags, int width)
{
	long n = str_arg(sk, arg, wide_arg, prec, 0);
	long padn = width - n;
	if (padn < 0) padn = 0;
	if (flags & 4) { str_arg(sk, arg, wide_arg, prec, 1); pad(sk, ' ', (size_t)padn); }
	else { pad(sk, ' ', (size_t)padn); str_arg(sk, arg, wide_arg, prec, 1); }
}

/* ---- exact decimal expansion of a double ---------------------------- */

/* Big non-negative integers in base 10^9, least significant limb first,
 * so that reading the decimal digits back out is a matter of splitting
 * limbs rather than dividing a binary big integer down.  Only mul_small
 * is needed: nothing here ever adds, subtracts or divides.
 *
 * DEC_LIMBS is chosen so nothing can overflow it.  The widest value
 * formed below is (2^53-1) * 5^1074, the numerator of the smallest
 * subnormals, which has 767 digits and so 86 limbs; the widest of the
 * other case, 2^1024, has 309 digits.  EXACT_DIG is the matching bound
 * on the digits themselves. */
#define DEC_LIMBS 88
#define EXACT_DIG 768

/* a = a * m, for m small enough that limb * m + carry stays inside a
 * uint64 (every m used here is below 2^30). */
static int mul_small(uint32_t *a, int n, uint32_t m) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	uint64_t carry = 0;
	int i;

	for (i = 0; i < n; i++) {
		uint64_t t = (uint64_t)a[i] * m + carry;
		a[i] = (uint32_t)(t % 1000000000u);
		carry = t / 1000000000u;
	}
	/* cannot run past DEC_LIMBS; do not corrupt memory if it does */
	while (carry && n < DEC_LIMBS) {
		a[n++] = (uint32_t)(carry % 1000000000u);
		carry /= 1000000000u;
	}
	return n;
}

/* The exact decimal expansion of a finite v >= 0: d[0..nd) are its
 * significant digits, and d[0] is the 10^decexp place, so the value is
 * 0.d * 10^(decexp+1) with every place past d[nd-1] a zero.  nd is
 * never more than EXACT_DIG, and d[0] is '0' only for v == 0. */
struct dec {
	int nd;
	int decexp;
	char d[EXACT_DIG];
};

static void dec_exact(double v, struct dec *D)
{
	union { double f; uint64_t i; } u;
	uint32_t bn[DEC_LIMBS];
	uint64_t m;
	int e2, bl = 0, k, i, j, nfrac = 0;
	char *p;

	u.f = v;
	e2 = (int)(u.i >> 52 & 0x7ff);
	m = u.i & 0xfffffffffffffULL;
	if (e2) { m |= (uint64_t)1 << 52; e2 -= 1075; }
	else e2 = -1074;   /* subnormal: no implicit bit, the same scale */

	if (!m) { D->nd = 1; D->decexp = 0; D->d[0] = '0'; return; }
	while (m) { bn[bl++] = (uint32_t)(m % 1000000000u); m /= 1000000000u; }

	/* v = m * 2^e2.  For e2 >= 0 that is the integer m << e2; for
	 * e2 < 0 it is m * 5^-e2 with the point -e2 places from the right,
	 * since m / 2^k == m * 5^k / 10^k.  Either way one big integer
	 * carries every digit, so no division is needed to produce them. */
	while (e2 > 0) {
		k = e2 > 29 ? 29 : e2;
		bl = mul_small(bn, bl, 1u << k);
		e2 -= k;
	}
	if (e2 < 0) {
		nfrac = -e2;
		for (k = nfrac; k > 0; ) {
			if (k >= 12) { bl = mul_small(bn, bl, 244140625u); k -= 12; }  /* 5^12 */
			else {
				uint32_t f = 1;
				while (k--) f *= 5;
				bl = mul_small(bn, bl, f);
			}
		}
	}

	p = D->d;
	{
		uint32_t hi = bn[bl - 1];
		char t[10];
		i = 0;
		do { t[i++] = (char)('0' + (int)(hi % 10)); hi /= 10; } while (hi);
		while (i) *p++ = t[--i];
	}
	for (i = bl - 2; i >= 0; i--) {
		uint32_t w = bn[i];
		for (j = 8; j >= 0; j--) { p[j] = (char)('0' + (int)(w % 10)); w /= 10; }
		p += 9;
	}
	D->nd = (int)(p - D->d);
	D->decexp = D->nd - 1 - nfrac;
	/* trailing zeros are implicit anyway, and dropping them keeps the
	 * "is the discarded tail nonzero" test in dec_round short */
	while (D->nd > 1 && D->d[D->nd - 1] == '0') D->nd--;
}

/* Round D to want >= 1 significant digits, to nearest with ties to
 * even.  Asking for more digits than the expansion has is a no-op: the
 * rest are zeros already, and every reader here treats an index past nd
 * as a zero.  A carry out of the leading digit bumps decexp, leaving
 * "1" followed by zeros. */
static void dec_round(struct dec *D, int want)
{
	int i, up;

	if (want >= D->nd) return;
	up = D->d[want] > '5';
	if (D->d[want] == '5') {
		for (i = want + 1; i < D->nd; i++) if (D->d[i] != '0') { up = 1; break; }
		if (!up) up = (D->d[want - 1] - '0') & 1;   /* a tie goes to even */
	}
	D->nd = want;
	if (!up) {
		while (D->nd > 1 && D->d[D->nd - 1] == '0') D->nd--;
		return;
	}
	for (i = want - 1; i >= 0; i--) {
		if (D->d[i] != '9') { D->d[i]++; D->nd = i + 1; return; }
		D->d[i] = '0';
	}
	D->d[0] = '1';
	D->nd = 1;
	D->decexp++;
}

/* %f-style body (no sign): pos digits before the point, then a point
 * and prec digits after it, from D rounded to decexp+1+prec significant
 * digits.  When that count is not positive the whole value sits below
 * the last place shown, and the result is a zero there -- or a one, if
 * the value reaches half of it. */
static int fmt_f(char *buf, struct dec *D, int prec, int alt)
{
	int want = D->decexp + 1 + prec, pos, i, n = 0;

	if (want >= 1) dec_round(D, want);
	else {
		/* want == 0 puts the leading digit exactly one place below the
		 * last one shown, so the value rounds up to a one there when
		 * that digit is past 5, or is 5 with anything nonzero after
		 * it; an exact half ties to the even zero.  want < 0 puts it
		 * further down still, which always rounds to zero. */
		D->d[0] = (want == 0 && (D->d[0] > '5' || (D->d[0] == '5' && D->nd > 1)))
		          ? '1' : '0';
		D->nd = 1;
		D->decexp = -prec;
	}
	pos = D->decexp + 1;
	if (pos <= 0) {
		buf[n++] = '0';
		if (prec > 0 || alt) buf[n++] = '.';
		for (i = 0; i < -pos && i < prec; i++) buf[n++] = '0';
		for (i = 0; i < prec + pos; i++) buf[n++] = (char)(i < D->nd ? D->d[i] : '0');
	} else {
		for (i = 0; i < pos; i++) buf[n++] = (char)(i < D->nd ? D->d[i] : '0');
		if (prec > 0 || alt) buf[n++] = '.';
		for (i = 0; i < prec; i++) buf[n++] = (char)(pos + i < D->nd ? D->d[pos + i] : '0');
	}
	return n;
}

/* %e-style body (no sign).  *epos receives the offset of the 'e', the
 * point at which emit_float splices in any zeros a clamped precision
 * left out of the mantissa. */
static int fmt_e(char *buf, struct dec *D, int prec, int alt, int upper, int *epos) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int i, n = 0;

	dec_round(D, prec + 1);
	buf[n++] = D->d[0];
	if (prec > 0 || alt) {
		buf[n++] = '.';
		for (i = 1; i < prec + 1; i++) buf[n++] = (char)(i < D->nd ? D->d[i] : '0');
	}
	*epos = n;
	buf[n++] = upper ? 'E' : 'e';
	buf[n++] = D->decexp < 0 ? '-' : '+';
	{
		unsigned ax = (unsigned)(D->decexp < 0 ? -D->decexp : D->decexp);
		char eb[8]; int ei = 0;
		if (ax == 0) eb[ei++] = '0';
		while (ax) { eb[ei++] = (char)('0' + ax % 10); ax /= 10; }
		while (ei < 2) eb[ei++] = '0';
		while (ei--) buf[n++] = eb[ei];
	}
	return n;
}

/* strip trailing fractional zeros (and a bare trailing point) from a
 * body already formatted by fmt_f/fmt_e, for %g without '#'. */
static int strip_g(char *buf, int n, int has_exp) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int mant_end = n, i;
	if (has_exp) { for (i = 0; i < n; i++) if (buf[i] == 'e' || buf[i] == 'E') { mant_end = i; break; } }
	i = mant_end;
	if (memchr(buf, '.', mant_end)) {
		while (i > 0 && buf[i - 1] == '0') i--;
		if (i > 0 && buf[i - 1] == '.') i--;
	}
	if (i != mant_end) memmove(buf + i, buf + mant_end, (size_t)(n - mant_end));
	return n - (mant_end - i);
}

/* %a-style body: the hex significand and the binary exponent in
 * decimal, without the sign and without the "0x" -- emit_float carries
 * those in the prefix, so that a '0' flag pads between them the way
 * C99 7.19.6.1p6 asks.  *epos receives the offset of the 'p', where
 * the zeros of a precision clamped to PREC_MAX belong.
 *
 * The 52 mantissa bits of a double are exactly 13 hex digits, so every
 * digit past the 13th is a zero whatever the value; a precision below
 * 13 rounds to nearest with ties to even, like the arithmetic itself. */
static int fmt_a(char *buf, double v, int prec, int alt, int upper, int *epos) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	union { double f; uint64_t i; } u;
	uint64_t man;
	int e, nd, i, n = 0;
	char lead;

	u.f = v;
	e = (int)(u.i >> 52 & 0x7ff);
	man = u.i & 0xfffffffffffffULL;
	if (!e) { lead = '0'; e = man ? -1022 : 0; }   /* subnormal, or zero */
	else { lead = '1'; e -= 1023; }

	if (prec < 0) {
		/* no precision given: exactly the digits the value needs */
		nd = 13;
		while (nd > 0 && !(man >> (52 - 4 * nd) & 0xf)) nd--;
	} else if (prec < 13) {
		int shift = (13 - prec) * 4;
		uint64_t rem = man & (((uint64_t)1 << shift) - 1);
		uint64_t half = (uint64_t)1 << (shift - 1);
		man >>= shift;
		/* a tie goes to even, where at a precision of 0 the digit
		 * that has to end up even is the leading one */
		if (rem > half || (rem == half && ((prec ? man : (uint64_t)(lead - '0')) & 1))) man++;
		if (man >> (4 * prec)) { man = 0; lead++; }   /* carried out of the digits */
		man <<= shift;
		nd = prec;
	} else nd = 13;   /* the caller's extra digits are zeros; see PREC_MAX */

	buf[n++] = lead;
	if (nd > 0 || alt) buf[n++] = '.';
	for (i = 0; i < nd; i++) buf[n++] = hex[man >> (48 - 4 * i) & 0xf];
	if (prec > 13) for (i = 13; i < prec; i++) buf[n++] = '0';
	*epos = n;
	buf[n++] = upper ? 'P' : 'p';
	buf[n++] = e < 0 ? '-' : '+';
	{
		unsigned ax = (unsigned)(e < 0 ? -e : e);
		char eb[8]; int ei = 0;
		if (ax == 0) eb[ei++] = '0';
		while (ax) { eb[ei++] = (char)('0' + ax % 10); ax /= 10; }
		while (ei--) buf[n++] = eb[ei];
	}
	return n;
}

/* Write a body of n bytes with `zeros` further '0' spliced in at offset
 * zpos (the end of the mantissa), which is where a precision clamped to
 * PREC_MAX left off. */
static void out_body(struct sink *sk, const char *body, int n, int zpos, long zeros) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	out(sk, body, (size_t)zpos);
	pad(sk, '0', (size_t)zeros);
	out(sk, body + zpos, (size_t)(n - zpos));
}

static void emit_float(struct sink *sk, double v, int conv, int prec, int alt, int flags, int width)
    __attribute__((nonnull(1)));
static void emit_float(struct sink *sk, double v, int conv, int prec, int alt, int flags, int width) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	char body[BODYMAX];
	struct dec D;
	char pfx[3];
	int n, neg = signbit(v);
	int upper = conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A';
	char sign = (char)(neg ? '-' : (flags & 1 ? '+' : (flags & 2 ? ' ' : 0)));
	char av = (char)(conv == 'F' ? 'f' : conv == 'E' ? 'e' : conv == 'G' ? 'g' :
	                 conv == 'A' ? 'a' : conv);
	int prefixlen = 0;
	long zeros = 0;   /* mantissa places past PREC_MAX, all of them zeros */
	int zpos = 0;     /* where in body they belong */
	int total, special = 0;

	v = fabs(v);
	/* A NaN keeps whatever sign the flags ask for: C99 7.19.6.1p6 says a
	 * signed conversion under '+' always begins with a sign, and p8's
	 * "[-]nan" only makes the minus conditional.  glibc and musl both
	 * print "+nan". */
	/* body[] is a length-tracked buffer (n), never a NUL-terminated C
	 * string -- out_body below always writes exactly n bytes. */
	if (isnan(v)) { memcpy(body, upper ? "NAN" : "nan", 3); n = 3; special = 1; } // NOLINT(bugprone-not-null-terminated-result)
	else if (isinf(v)) { memcpy(body, upper ? "INF" : "inf", 3); n = 3; special = 1; } // NOLINT(bugprone-not-null-terminated-result)
	else if (av == 'a') {
		int pu = prec > PREC_MAX ? PREC_MAX : prec;
		zeros = prec > PREC_MAX ? (long)prec - pu : 0;
		n = fmt_a(body, v, pu, alt, upper, &zpos);
	}
	else if (av == 'f' || av == 'e') {
		int p = prec < 0 ? 6 : prec;
		int pu = p > PREC_MAX ? PREC_MAX : p;
		zeros = (long)p - pu;
		dec_exact(v, &D);
		if (av == 'f') { n = fmt_f(body, &D, pu, alt); zpos = n; }
		else n = fmt_e(body, &D, pu, alt, upper, &zpos);
	} else { /* g/G */
		int P = prec < 0 ? 6 : (prec == 0 ? 1 : prec);
		int PU = P > PREC_MAX ? PREC_MAX : P;
		dec_exact(v, &D);
		/* the form is chosen from the rounded value's exponent (C99
		 * 7.19.6.1p8), so round first; fmt_e/fmt_f then round to the
		 * same width again, which is a no-op */
		dec_round(&D, PU);
		/* without '#' the zeros a clamped precision drops are exactly
		 * the ones strip_g would take off again, so they never go out */
		zeros = alt ? (long)P - PU : 0;
		/* decexp is at most 308, so the form is the same whether the
		 * unclamped or the clamped precision picks it */
		if (D.decexp < -4 || D.decexp >= P) {
			n = fmt_e(body, &D, PU - 1, alt, upper, &zpos);
			if (!alt) n = strip_g(body, n, 1);
		} else {
			n = fmt_f(body, &D, PU - 1 - D.decexp, alt);
			zpos = n;
			if (!alt) n = strip_g(body, n, 0);
		}
	}
	if (zeros <= 0 || zpos > n) { zeros = 0; zpos = n; }

	if (sign) pfx[prefixlen++] = sign;
	if (av == 'a' && !special) { pfx[prefixlen++] = '0'; pfx[prefixlen++] = upper ? 'X' : 'x'; }

	/* C99 7.19.6.1p3: the count printf returns is an int, so refuse a
	 * conversion whose zero run alone would not fit in one rather than
	 * spend an age emitting a result that cannot be reported. */
	if (zeros > (long)(INT_MAX - n - prefixlen)) {
		errno = EOVERFLOW;
		sk->f->err = 1;
		sk->bad = 1;
		return;
	}
	total = n + (int)zeros + prefixlen;

	{
		int pad_n = width - total;
		if (pad_n < 0) pad_n = 0;
		if (flags & 4) { /* left */
			out(sk, pfx, (size_t)prefixlen);
			out_body(sk, body, n, zpos, zeros);
			pad(sk, ' ', (size_t)pad_n);
		} else if ((flags & 8) && !special) { /* zero pad, never for inf/nan */
			out(sk, pfx, (size_t)prefixlen);
			pad(sk, '0', (size_t)pad_n);
			out_body(sk, body, n, zpos, zeros);
		} else {
			pad(sk, ' ', (size_t)pad_n);
			out(sk, pfx, (size_t)prefixlen);
			out_body(sk, body, n, zpos, zeros);
		}
	}
}

/* ------------------------------------------------------------------
 * THE ARGUMENT LIST: fprintf.html's numbered conversions
 *
 * "Conversions can be applied to the nth argument after the format in
 * the argument list, rather than to the next unused argument.  In this
 * case, the conversion specifier character % ... is replaced by the
 * sequence "%n$", where n is a decimal integer in the range
 * [1,{NL_ARGMAX}] ... The format can contain either numbered argument
 * conversion specifications (that is, "%n$" and "*m$"), or unnumbered
 * argument conversion specifications (that is, % and *), but not both."
 *
 * A numbered format reads its arguments out of order and va_arg cannot
 * be rewound, so they have to be collected into an indexable table
 * before the first conversion runs.  Collecting them needs each
 * argument's TYPE -- va_arg is a type-directed macro, not a byte count
 * -- and an argument's type is known only from the conversion
 * specification that names it.  That is the whole reason the format is
 * scanned twice: build_argtab() below walks it for types and fills the
 * table, and the ordinary loop then sources every argument from the
 * table rather than from the va_list.
 *
 * THE UNNUMBERED PATH DOES NOT PAY FOR THIS.  There is no pre-scan, no
 * table and no allocation: the format is classified at the first
 * conversion specification, out of the "n$" that specification's own
 * parse already looked for, and until a numbered one appears the
 * arguments come off the va_list exactly as they always did.  A format
 * that never writes "%n$" -- every format in this tree -- pays one
 * predictable branch per argument fetched.  A malloc here would be paid
 * by every printf in every program linked against this library, which
 * is why the table is NL_ARGMAX entries of frame instead.
 *
 * KNOWN RESIDUAL COST, measured, in the same register as the sink's
 * note above, so that nobody re-derives it: the unnumbered path is
 * about 5% slower than it was before any of this existed.  2030 ms
 * against 2140 ms of CPU TIME -- clock(), not wall, because this box is
 * shared -- over eight rounds of 500000 iterations of five snprintf()
 * calls, minima taken, x86_64-win32-tcc under Wine, the two builds
 * interleaved.
 *
 * Two candidate causes were measured and are NOT it.  The table: an
 * otherwise identical build with the array shrunk to one entry came out
 * at the same 2140 ms, so the frame is free.  The added calls: folding
 * arg_type() into parse_spec() came out at the same 2140 ms too, so it
 * is not call overhead either.  What is left is the specification being
 * parsed into a struct that the conversion reads back, and that is
 * exactly what buys both passes ONE parser.  Two parsers would recover
 * the 5% and cost a grammar obliged to agree with itself in two places
 * -- the trade this file has already refused once, on the same grounds,
 * at the sink.
 * ------------------------------------------------------------------ */

/* The type an argument is fetched with.  These are the types va_arg
 * itself is handed: the default argument promotions have already been
 * applied at the call site, so there is no A_CHAR or A_SHORT -- %hhd's
 * argument arrives as an int and is narrowed after it is fetched. */
enum { A_NONE, A_INT, A_UINT, A_LONG, A_ULONG, A_LLONG, A_ULLONG,
       A_SIZE, A_SSIZE, A_PTRDIFF, A_WINT, A_DOUBLE, A_PTR };

/* One fetched argument, normalised: every integer widens into i or u on
 * the way out of va_arg, so the conversions below read one member per
 * signedness instead of one per length modifier.  That is what keeps
 * the C type a specification names in one place -- pop_arg() below, and
 * the two cases TAKE spells out for speed, each under the same constant.
 * The alternative -- every conversion doing its own va_arg AND a
 * separate table saying what type it would have used -- is two mappings
 * that must agree, with nothing to notice when they stop agreeing: a
 * %zu that popped a long into a slot typed as a size_t would misread
 * every later argument of a numbered format. */
union varg {
	long long i;
	unsigned long long u;
	double d;
	void *p;
};

/* a is written unconditionally on every path through the switch below,
 * including the default (A_NONE) case (`a->i = 0`); ap is dereferenced
 * by every case except that same A_NONE default, but at every real
 * call site in this file ap is the address of a local va_list (`&aq`)
 * or a parameter that is itself always one (build_argtab's own ap,
 * below), never a value that could legitimately be null -- the same
 * "no legitimate NULL value" reasoning as dirent's __dirstream_next()
 * out parameter (src/dirent/dirent_internal.h). */
static void pop_arg(union varg *a, int type, va_list *ap)
    __attribute__((nonnull(1, 3)));
static void pop_arg(union varg *a, int type, va_list *ap)
{
	switch (type) {
	case A_INT:     a->i = va_arg(*ap, int); break;
	case A_UINT:    a->u = va_arg(*ap, unsigned int); break;
	case A_LONG:    a->i = va_arg(*ap, long); break;
	case A_ULONG:   a->u = va_arg(*ap, unsigned long); break;
	case A_LLONG:   a->i = va_arg(*ap, long long); break;
	case A_ULLONG:  a->u = va_arg(*ap, unsigned long long); break; // NOLINT(bugprone-branch-clone) -- va_arg must name the exact unsigned long long source type; the following size_t case only canonicalizes identically on LLP64
	/* LLP64: long is 32 bits here while size_t and ptrdiff_t are 64, so
	 * `long` is simply the wrong type to pull these with -- "%zd" of a
	 * value above 4G printed its low half.  fprintf.html: z "applies to
	 * a size_t or the corresponding signed integer type argument", t
	 * likewise for ptrdiff_t.  Pull each as the type the page names.
	 * src/stdio/scanf.c implements the same grammar and has always done
	 * this correctly; printf.c was the only offender. */
	case A_SIZE:    a->u = va_arg(*ap, size_t); break;
	case A_SSIZE:   a->i = va_arg(*ap, ssize_t); break; // NOLINT(bugprone-branch-clone) -- va_arg must name the exact ssize_t source type; the following ptrdiff_t case only canonicalizes identically on this ABI
	/* ptrdiff_t is a signed type whatever the conversion's signedness
	 * is -- the length-modifier table gives t no unsigned counterpart --
	 * so %tu/%to/%tx fetch it as one and reinterpret afterwards. */
	case A_PTRDIFF: a->i = va_arg(*ap, ptrdiff_t); break;
	case A_WINT:    a->i = va_arg(*ap, wint_t); break;
	case A_DOUBLE:  a->d = va_arg(*ap, double); break;
	case A_PTR:     a->p = va_arg(*ap, void *); break;
	default:        a->i = 0; break;   /* A_NONE: nothing is fetched */
	}
}

/* The type a conversion fetches, from the pair that decides it.  A_NONE
 * for one that fetches nothing: an unknown conversion (which this
 * formatter emits literally, consuming no argument) or a format that
 * ended before its conversion character. */
static int arg_type(int lm, int conv) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	switch (conv) {
	case 'd': case 'i':
		switch (lm) {
		case LM_l: return A_LONG;
		case LM_ll: case LM_j: return A_LLONG;
		/* widthmod-ok: A_SSIZE is fetched as ssize_t in pop_arg(). */
		case LM_z: return A_SSIZE;
		/* widthmod-ok: A_PTRDIFF is fetched as ptrdiff_t in pop_arg(). */
		case LM_t: return A_PTRDIFF;
		default: return A_INT;      /* hh and h promote to int */
		}
	case 'u': case 'o': case 'x': case 'X':
		switch (lm) {
		case LM_l: return A_ULONG;
		case LM_ll: case LM_j: return A_ULLONG;
		/* widthmod-ok: A_SIZE is fetched as size_t in pop_arg(). */
		case LM_z: return A_SIZE;
		/* widthmod-ok: A_PTRDIFF is fetched as ptrdiff_t in pop_arg(). */
		case LM_t: return A_PTRDIFF;
		default: return A_UINT;
		}
	case 'c': return lm == LM_l ? A_WINT : A_INT;
	case 's': case 'p': case 'n': return A_PTR;
	/* L is accepted and ignored, as it always was here: long double is
	 * a double on both of this library's targets (checked: tcc gives
	 * win32 i386 and x86_64 an 8-byte long double), so %Lf fetches the
	 * same argument %f does. */
	case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
	case 'a': case 'A': return A_DOUBLE;
	default: return A_NONE;
	}
}

/* One parsed conversion specification.  width and prec hold a literal
 * one; wpos and ppos are -1 when there is no '*' at all, 0 for a '*'
 * that takes the next unused argument, and n for a "*n$" that names
 * one.  argpos is 0 when the specification is unnumbered. */
struct spec {
	int argpos;
	int flags;      /* 1=+ 2=space 4=- 8=0 16=# 32=' */
	int width, wpos;
	int prec, ppos;
	int lm;
	int conv;       /* the conversion character, 0 at end of format */
	int type;       /* arg_type(lm, conv) */
	int width_overflow;
};

/* "%n$" and "*n$": a digit run terminated by '$'.  It cannot be told
 * from a width until the '$' is seen ("%1$s" against "%12s"), so the
 * only way to read it is to look ahead and rewind -- hence the original
 * cursor coming back when there is no '$'.  n has no leading zero,
 * being "a decimal integer in the range [1,{NL_ARGMAX}]", so a leading
 * '0' is the zero flag and never an index, and "%0$d" is a width of 0
 * rather than a slot nothing can fill.
 *
 * The accumulator stops once it is past NL_ARGMAX: the value is only
 * ever compared against that bound, and accumulating an arbitrarily
 * long digit run unclamped is signed overflow. */
/* fp is dereferenced unconditionally via gf(fp, st) (the very next
 * statement after *n = 0); n is written unconditionally, first
 * statement. */
static const char *scan_argno(const char *fp, int st, int *n) __attribute__((nonnull(1, 3)));
static const char *scan_argno(const char *fp, int st, int *n)
{
	const char *q = fp;
	int v = 0;

	*n = 0;
	if (gf(q, st) < '1' || gf(q, st) > '9') return fp;
	while (gf(q, st) >= '0' && gf(q, st) <= '9') {
		if (v <= NL_ARGMAX) {
			unsigned digit = (unsigned)(gf(q, st) - '0');
			v = (int)((unsigned)v * 10u + digit);
		}
		q += st;
	}
	if (gf(q, st) != '$') return fp;
	*n = v;
	return q + st;
}

/* Parse one conversion specification, from the character after its '%'
 * to its conversion character, where the cursor is left -- or on the
 * terminating null of a format that ends mid-specification, which the
 * caller sees as sp->conv == 0.
 *
 * ONE parser, called by both passes.  A second scanner written for the
 * type-collecting pass would be a copy of this grammar obliged to agree
 * with it exactly, and a disagreement between the two is neither a
 * compile error nor a visible one: it is the wrong argument silently
 * fetched for a conversion.  The cost is one call per DIRECTIVE, which
 * is not the cost gf() above was made a macro to avoid -- that one was
 * a call per format CHARACTER. */
/* sp is written unconditionally, first statement (`sp->flags = 0;`);
 * fp is dereferenced unconditionally too, right after those
 * initializations (`gf(fp, st)`). */
static const char *parse_spec(const char *fp, int st, struct spec *sp) __attribute__((nonnull(1, 3)));
static const char *parse_spec(const char *fp, int st, struct spec *sp)
{
	int n;

	sp->flags = 0;
	sp->width = 0;
	sp->wpos = -1;
	sp->prec = -1;
	sp->ppos = -1;
	sp->lm = LM_NONE;
	sp->width_overflow = 0;

	/* The guard, rather than letting scan_argno() return early on its
	 * own: tcc does not inline, so on the overwhelmingly common
	 * specification -- one that does not begin with a digit at all --
	 * this comparison is the whole cost of looking for an index,
	 * instead of a call that finds nothing. */
	sp->argpos = 0;
	if (gf(fp, st) >= '1' && gf(fp, st) <= '9')
		fp = scan_argno(fp, st, &sp->argpos);

	for (;; fp += st) {
		if (gf(fp, st) == '-') sp->flags |= 4;
		else if (gf(fp, st) == '+') sp->flags |= 1;
		else if (gf(fp, st) == ' ') sp->flags |= 2;
		else if (gf(fp, st) == '0') sp->flags |= 8;
		else if (gf(fp, st) == '#') sp->flags |= 16;
		/* fprintf.html's flag table: "'  [CX] (The <apostrophe>.)  The
		 * integer portion of the result of a decimal conversion ( %i,
		 * %d, %u, %f, %F, %g, or %G ) shall be formatted with thousands'
		 * grouping characters. ... The non-monetary grouping character
		 * is used."
		 *
		 * A [CX] flag, i.e. base POSIX rather than XSI, so it must be
		 * ACCEPTED whatever the locale.  Accepted and then ignored is
		 * not a stub here, it is the complete implementation: the
		 * grouping to apply is LC_NUMERIC's `grouping`, which is "" in
		 * the POSIX locale (src/misc/locale.c), and the POSIX locale is
		 * the only one this library has -- setlocale() accepts nothing
		 * else.  An empty grouping specification means no separators, so
		 * the flagged conversion must produce byte-for-byte what the
		 * unflagged one produces, which is what ignoring it does.  The
		 * bit is recorded rather than dropped so that a future locale
		 * with real grouping has somewhere to hook on.
		 *
		 * Leaving it out of this loop was NOT a cosmetic defect.  The
		 * apostrophe ended the flag scan, fell through the conversion
		 * switch's default arm, and that arm emits the bytes literally
		 * WITHOUT consuming an argument -- so every conversion after a
		 * %' in the same format read the previous one's argument.
		 * printf("%'d %s\n", total, name) handed `total` to %s to
		 * dereference as a char *.  And in the POSIX locale the correct
		 * output for %'d is identical to %d, so the one visible symptom
		 * was the least alarming one. */
		else if (gf(fp, st) == '\'') sp->flags |= 32;
		else break;
	}

	if (gf(fp, st) == '*') {
		fp = scan_argno(fp + st, st, &n);
		sp->wpos = n;
	} else {
		while (gf(fp, st) >= '0' && gf(fp, st) <= '9') {
			int digit = (int)(gf(fp, st) - '0');
			if (sp->width > (INT_MAX - digit) / 10) {
				sp->width = INT_MAX;
				sp->width_overflow = 1;
			} else sp->width = (int)((unsigned)sp->width * 10u +
				(unsigned)digit);
			fp += st;
		}
	}
	if (gf(fp, st) == '.') {
		fp += st;
		if (gf(fp, st) == '*') { fp = scan_argno(fp + st, st, &n); sp->ppos = n; }
		else {
			sp->prec = 0;
			while (gf(fp, st) >= '0' && gf(fp, st) <= '9') {
				int digit = (int)(gf(fp, st) - '0');
				if (sp->prec > (INT_MAX - digit) / 10)
					sp->prec = INT_MAX;
				else sp->prec = (int)((unsigned)sp->prec * 10u +
					(unsigned)digit);
				fp += st;
			}
		}
	}
	for (;;) {
		if (gf(fp, st) == 'h') { sp->lm = sp->lm == LM_h ? LM_hh : LM_h; fp += st; }
		else if (gf(fp, st) == 'l') { sp->lm = sp->lm == LM_l ? LM_ll : LM_l; fp += st; }
		else if (gf(fp, st) == 'j') { sp->lm = LM_j; fp += st; }
		else if (gf(fp, st) == 'z') { sp->lm = LM_z; fp += st; }
		else if (gf(fp, st) == 't') { sp->lm = LM_t; fp += st; }
		else if (gf(fp, st) == 'L') { sp->lm = LM_L; fp += st; }
		else break;
	}
	sp->conv = (int)gf(fp, st);
	sp->type = arg_type(sp->lm, sp->conv);
	return fp;
}

/* Collect the arguments a numbered format names into tab[1..NL_ARGMAX],
 * in index order, and return the highest index used -- or -1 for a
 * format this cannot serve, which the caller reports as [EINVAL].
 *
 * Everything POSIX leaves undefined here becomes that -1 rather than a
 * guess, because the alternative to a diagnosed refusal is not a
 * slightly wrong answer: it is arguments read at the wrong offsets for
 * the rest of the format, i.e. an int dereferenced as a char *.  Two
 * things are refused -- mixing the two forms ("but not both") and an
 * index outside [1,{NL_ARGMAX}] -- and a gap in the numbering is not
 * one of them; see the end of this function for why. */
/* fmt is dereferenced unconditionally via gf(fp, st) in the main loop
 * condition; tab and ap are only actually touched once max > 0 (a
 * format with at least one argument-consuming specification), but at
 * this file's one real call site both are the address of a real local
 * (`argv`, a fixed-size array that always decays to a non-null
 * address; `&aq`) -- never a value a caller could legitimately pass as
 * null, the same reasoning as pop_arg's own ap above. */
static int build_argtab(const char *fmt, int st, union varg *tab, va_list *ap)
    __attribute__((nonnull(1, 3, 4)));
static int build_argtab(const char *fmt, int st, union varg *tab, va_list *ap)
{
	unsigned char types[NL_ARGMAX + 1];
	const char *fp = fmt;
	struct spec sp;
	int i, max = 0;

	memset(types, A_NONE, sizeof types);
	while (gf(fp, st)) {
		if (gf(fp, st) != '%') { fp += st; continue; }
		fp += st;
		if (gf(fp, st) == '%') { fp += st; continue; }
		fp = parse_spec(fp, st, &sp);
		if (sp.conv) fp += st;
		if (sp.width_overflow) return -2;

		/* The unnumbered forms, in a format that has already shown a
		 * numbered one: a '*' naming no argument, or a conversion that
		 * fetches one and names none. */
		if (sp.wpos == 0 || sp.ppos == 0) return -1;
		if (sp.type != A_NONE && !sp.argpos) return -1;

		/* "*m$" is always an int -- fprintf.html: "the argument ...
		 * shall be converted to an int". */
		if (sp.wpos > 0) {
			if (sp.wpos > NL_ARGMAX) return -1;
			types[sp.wpos] = A_INT;
			if (sp.wpos > max) max = sp.wpos;
		}
		if (sp.ppos > 0) {
			if (sp.ppos > NL_ARGMAX) return -1;
			types[sp.ppos] = A_INT;
			if (sp.ppos > max) max = sp.ppos;
		}
		if (sp.type != A_NONE) {
			if (sp.argpos > NL_ARGMAX) return -1;
			/* Naming one argument twice is the point of the feature and
			 * is fine; naming it with two different types is undefined,
			 * and the last specification wins because it is the one
			 * still in hand. */
			types[sp.argpos] = (unsigned char)sp.type;
			if (sp.argpos > max) max = sp.argpos;
		}
	}

	/* A GAP: an index below the highest one used that no specification
	 * names, as in "%9$d" where the first eight arguments are passed and
	 * never mentioned.  POSIX does not say what that means.  What it
	 * must not do is read a slot nothing wrote or walk the va_list by a
	 * width nobody stated, so the unnamed argument is skipped as an int:
	 * that is what the default argument promotions produce for
	 * everything narrower, so it is the likeliest guess, and on the
	 * LLP64 arch every argument slot is one register wide anyway, which
	 * makes the guess unobservable there.  Filling the slot rather than
	 * leaving it alone is the half that matters: after this loop every
	 * entry in [1,max] has been written before anything can read one. */
	for (i = 1; i < max + 1; i++)
		if (types[i] == A_NONE) types[i] = A_INT;

	for (i = 1; i < max + 1; i++) pop_arg(&tab[i], types[i], ap);
	return max;
}

/* The argument a specification names: out of the table for a numbered
 * format, off the va_list for an unnumbered one.  A macro because the
 * `else` arm must expand va_arg in this function's own frame, and
 * because tcc does not inline -- the branch is one predictable test,
 * a call would not be.
 *
 * A_INT and A_PTR are spelled out here rather than left to pop_arg, and
 * the reason is measurement rather than taste: between them they are
 * "%d", "%c", every "*" width and precision, "%s", "%p" and "%n", which
 * is nearly every conversion any of this library's consumers writes.
 * Under tcc, whose every static function is a real call, routing those
 * through pop_arg cost 2-3% of the formatter, and the two extra
 * comparisons here cost nothing measurable.  The duplication is safe in
 * the way duplication rarely is: each arm is guarded by the very
 * constant that selects the same arm of pop_arg's switch, so the two
 * cannot disagree about a type without disagreeing about `ty`. */
#define TAKE(dst, pos, ty) do { \
	if (argtab) (dst) = argtab[pos]; \
	else if ((ty) == A_INT) (dst).i = va_arg(aq, int); \
	else if ((ty) == A_PTR) (dst).p = va_arg(aq, void *); \
	else pop_arg(&(dst), (ty), &aq); \
} while (0)

/* f is dereferenced unconditionally (`sink.widemem = sink.wide &&
 * f->wmem;`); fmt is dereferenced unconditionally by the main loop's
 * own gf(fp, st). ap is a va_list BY VALUE, not a pointer this
 * attribute can describe. */
static int vfprintf_st(FILE *f, const char *fmt, va_list ap, int st) __attribute__((nonnull(1, 2)));
static int vfprintf_st(FILE *f, const char *fmt, va_list ap, int st)
{
	struct sink sink, *sk = &sink;
	const char *fp = fmt;
	/* Internal callers use bytes or wchar_t units.  Keep that contract local
	 * to the pointer-difference division as well as at the call sites. */
	if (st != 1 && st != (int)sizeof(wchar_t)) {
		errno = EINVAL;
		return -1;
	}
	/* Only a numbered format ever touches these.  Eighty-odd bytes of
	 * frame is what buys the common path its freedom from a malloc. */
	union varg argv[NL_ARGMAX + 1];
	union varg *argtab = 0;
	/* A local COPY of the argument list, because everything below fetches
	 * through its ADDRESS.  va_list is an array type on some ABIs -- not
	 * on either of this library's targets, which is why tcc accepts &ap
	 * without a murmur and tools/lint.sh's host gcc pass does not -- and
	 * a parameter of array type is a pointer, so &ap would be a pointer
	 * to the wrong thing there.  Copying costs the caller nothing:
	 * vfprintf.html leaves "the value of ap after the return"
	 * unspecified. */
	va_list aq;
	va_copy(aq, ap);

	sink.f = f;
	sink.wide = st != 1;
	sink.widemem = sink.wide && f->wmem;
	sink.count = 0;
	sink.bad = 0;
	memset(&sink.ost, 0, sizeof sink.ost);

	while (gf(fp, st) && !sk->bad) {
		if (gf(fp, st) != '%') {
			/* A run of ordinary characters.  These come from the
			 * CALLER's format, so in a wide format they are wide
			 * characters and may be anything -- out()'s ASCII
			 * shortcut does not apply, and neither does its length,
			 * which would be the run's byte size rather than its
			 * character count. */
			const char *start = fp;
			while (gf(fp, st) && gf(fp, st) != '%') fp += st;
			if (st == 1) out(sk, start, (size_t)(fp - start));
			else out_units(sk, (const wchar_t *)start,
			               (size_t)((fp - start) / st));
			continue;
		}
		fp += st;
		if (gf(fp, st) == '%') { out(sk, "%", 1); fp += st; continue; }

		{
			struct spec sp;
			union varg a;
			int flags, width, prec;

			fp = parse_spec(fp, st, &sp);
			if (sp.width_overflow) {
				errno = EOVERFLOW;
				sk->f->err = 1;
				sk->bad = 1;
				break;
			}

			/* The first specification that names an argument settles
			 * the whole format: from here every argument comes out of
			 * the table, and build_argtab() has already refused the
			 * format if any other specification in it is unnumbered.
			 * Until such a specification appears nothing is scanned
			 * twice and nothing is stored, so an unnumbered format runs
			 * exactly as it did before numbered ones existed. */
			if (!argtab && (sp.argpos || sp.wpos > 0 || sp.ppos > 0)) {
				int argresult = build_argtab(fmt, st, argv, &aq);
				if (argresult < 0) {
					/* fprintf.html ERRORS: "[EINVAL] There are
					 * insufficient arguments" -- the nearest named
					 * failure, and the honest one to report for a
					 * format whose arguments cannot be located.  The
					 * stream's error indicator is deliberately NOT set
					 * the way the [EOVERFLOW] and [EILSEQ] paths above
					 * set it: nothing went wrong with the stream, and
					 * ferror() answers for the file rather than for the
					 * caller's format string. */
					errno = argresult == -2 ? EOVERFLOW : EINVAL;
					if (argresult == -2) sk->f->err = 1;
					sk->bad = 1;
					break;
				}
				argtab = argv;
			}

			flags = sp.flags;
			/* Width, then precision, then the conversion's own argument
			 * -- the order the unnumbered form consumes them in, which
			 * is the only order it CAN consume them in. */
			width = sp.width;
			if (sp.wpos >= 0) {
				TAKE(a, sp.wpos, A_INT);
				width = (int)a.i;
				/* "A negative field width is taken as a '-' flag
				 * followed by a positive field width."  INT_MIN's
				 * mathematical magnitude is INT_MAX+1: it cannot be
				 * returned by these int-returning interfaces, and a
				 * plain `-width` would itself be signed-overflow UB. */
				if (width == INT_MIN) {
					errno = EOVERFLOW;
					sk->f->err = 1;
					sk->bad = 1;
					break;
				}
				if (width < 0) { flags |= 4; width = -width; }
			}
			prec = sp.prec;
			if (sp.ppos >= 0) {
				TAKE(a, sp.ppos, A_INT);
				prec = (int)a.i;
				/* "A negative precision is taken as if the precision
				 * were omitted." */
				if (prec < 0) prec = -1;
			}
			/* Zeroed unconditionally, then overwritten by the fetch
			 * that a conversion taking an argument performs.  Nothing
			 * below reads `a` for a conversion that takes none -- an
			 * unknown one, which is echoed literally -- so this is not
			 * a correctness fix; it makes the reads in the switch
			 * UNCONDITIONALLY defined instead of defined by a
			 * correlation between sp.type and sp.conv that no local
			 * reader can check.  clang-analyzer could not check it
			 * either and reported all fourteen of them. */
			a.i = 0;
			if (sp.type != A_NONE) TAKE(a, sp.argpos, sp.type);

			switch (sp.conv) {
			case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
				int base = sp.conv == 'o' ? 8 : (sp.conv == 'x' || sp.conv == 'X') ? 16 : 10;
				int upper = sp.conv == 'X';
				int issigned = sp.conv == 'd' || sp.conv == 'i';
				int neg = 0;
				unsigned long long uv;
				char digbuf[32]; int dn = 0, zpad;
				char prefix[3]; int pn = 0;

				if (issigned) {
					long long sv;
					/* The fetch above took this at the width its
					 * length modifier names and sign-extended it (see
					 * pop_arg), so the only work left is the two
					 * modifiers naming a type NARROWER than the int
					 * their argument was promoted to. */
					switch (sp.lm) {
					case LM_hh: sv = (signed char)a.i; break; // NOLINT(bugprone-signed-char-misuse,cert-str34-c) -- deliberate sign extension of a %hhd argument, not a table index
					case LM_h: sv = (short)a.i; break;
					default: sv = a.i; break;
					}
					neg = sv < 0;
					/* Negate after widening, not before: -sv is
					 * undefined for LLONG_MIN, whose magnitude is not
					 * representable as a long long.  Converting first
					 * and negating the unsigned value is well defined
					 * (C99 6.3.1.3p2: it wraps modulo 2**64, which is
					 * exactly the magnitude wanted). */
					uv = neg ? __neg_mag((unsigned long long)sv) : (unsigned long long)sv;
				} else {
					switch (sp.lm) {
					case LM_hh: uv = (unsigned char)a.u; break;
					case LM_h: uv = (unsigned short)a.u; break;
					/* t was fetched as the signed ptrdiff_t it names
					 * (see pop_arg); this reinterprets it, exactly as
					 * "(unsigned long long)va_arg(ap, ptrdiff_t)" did. */
					/* widthmod-ok: pop_arg() fetched ptrdiff_t; this is its unsigned interpretation. */
					case LM_t: uv = (unsigned long long)a.i; break;
					default: uv = a.u; break;
					}
				}

				if (uv == 0 && prec == 0) { /* "" for 0 with explicit precision 0 */ }
				else {
					unsigned long long t = uv;
					do { digbuf[dn++] = "0123456789abcdef"[t % (unsigned)base] ; if (upper && digbuf[dn-1] > '9') digbuf[dn-1] -= 32; t /= (unsigned)base; } while (t);
				}
				/* A precision is a minimum digit count with no upper
				 * bound (C99 7.19.6.1p5), so the leading zeros it
				 * calls for are padded out to the stream rather than
				 * stored: digbuf holds only the digits a value can
				 * actually have. */
				zpad = prec > dn ? prec - dn : 0;

				if (neg) prefix[pn++] = '-';
				else if (issigned && (flags & 1)) prefix[pn++] = '+';
				else if (issigned && (flags & 2)) prefix[pn++] = ' ';
				/* '#' octal needs a leading zero only if the precision
				 * has not already put one there */
				if ((flags & 16) && base == 8 && !zpad && (dn == 0 || digbuf[dn-1] != '0')) digbuf[dn++] = '0';
				if ((flags & 16) && base == 16 && uv != 0) { prefix[pn++] = '0'; prefix[pn++] = upper ? 'X' : 'x'; }

				if (zpad > INT_MAX - dn - pn) { errno = EOVERFLOW; sk->f->err = 1; sk->bad = 1; break; }
				{
					int total = dn + pn + zpad;
					int padn = width - total; if (padn < 0) padn = 0;
					int zero = (flags & 8) && !(flags & 4) && prec < 0;
					if (flags & 4) {
						out(sk, prefix, (size_t)pn);
						pad(sk, '0', (size_t)zpad);
						{ char rev[sizeof digbuf]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(sk, rev, (size_t)dn); }
						pad(sk, ' ', (size_t)padn);
					} else if (zero) {
						out(sk, prefix, (size_t)pn);
						pad(sk, '0', (size_t)padn);
						{ char rev[sizeof digbuf]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(sk, rev, (size_t)dn); }
					} else {
						pad(sk, ' ', (size_t)padn);
						out(sk, prefix, (size_t)pn);
						pad(sk, '0', (size_t)zpad);
						{ char rev[sizeof digbuf]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(sk, rev, (size_t)dn); }
					}
				}
				break;
			}
			case 'c': {
				/* fprintf.html: with an l qualifier "the wint_t
				 * argument shall be converted as if by an ls
				 * conversion specification with no precision and an
				 * argument that points to a two-element array of type
				 * wchar_t, the first element containing the wint_t
				 * argument ... and the second a null wide character".
				 * Written literally, so %lc and %ls cannot disagree.
				 *
				 * fwprintf.html, plain %c: "the int argument shall be
				 * converted to a wide character as if by calling
				 * btowc()". */
				if (sp.lm == LM_l) {
					wchar_t wc[2];
					wc[0] = (wchar_t)a.i;
					wc[1] = 0;
					emit_str(sk, wc, 1, -1, flags, width);
				} else if (sk->wide) {
					wchar_t wc[2];
					wint_t b = btowc((int)a.i);
					/* btowc() answers WEOF for any byte that is not a
					 * complete character on its own, which under this
					 * library's UTF-8 is every byte from 0x80 up.  There
					 * is no wide character to emit, so the conversion
					 * fails rather than inventing one: fwprintf.html's
					 * ERRORS refer to fputwc(), whose [EILSEQ] is
					 * exactly "the wide-character code ... does not
					 * correspond to a valid character". */
					if (b == WEOF) {
						errno = EILSEQ;
						sk->f->err = 1;
						sk->bad = 1;
						break;
					}
					wc[0] = (wchar_t)b;
					wc[1] = 0;
					emit_str(sk, wc, 1, -1, flags, width);
				} else {
					char c = (char)a.i;
					int padn = width - 1; if (padn < 0) padn = 0;
					if (flags & 4) { out(sk, &c, 1); pad(sk, ' ', (size_t)padn); }
					else { pad(sk, ' ', (size_t)padn); out(sk, &c, 1); }
				}
				break;
			}
			case 's': {
				const void *arg = a.p;
				int wide_arg = sp.lm == LM_l;
				/* A null pointer is undefined, but printing "(null)"
				 * rather than dereferencing it is what this library
				 * already did and what glibc does; the wide form gets
				 * the same courtesy, in its own width. */
				if (!arg) {
					static const wchar_t wnull[7] = { '(', 'n', 'u', 'l', 'l', ')', 0 };
					arg = wide_arg ? (const void *)wnull : (const void *)"(null)";
				}
				emit_str(sk, arg, wide_arg, prec, flags, width);
				break;
			}
			case 'p': {
				void *ptr = a.p;
				uintptr_t uv = (uintptr_t)ptr;
				int dn = 2;   /* the "0x" prefix, emitted literally below */
				if (!ptr) { out(sk, "(nil)", 5); break; }
				{
					char rev[sizeof(uintptr_t) * 2]; int rn = 0;
					do { rev[rn++] = "0123456789abcdef"[uv % 16]; uv /= 16; } while (uv);
					{
						int padn = width - (dn + rn); if (padn < 0) padn = 0;
						if (flags & 4) {
							out(sk, "0x", 2);
							{ char b2[sizeof(uintptr_t)*2]; int i; for (i=0;i<rn;i++) b2[i]=rev[rn-1-i]; out(sk,b2,(size_t)rn); }
							pad(sk, ' ', (size_t)padn);
						} else {
							pad(sk, ' ', (size_t)padn);
							out(sk, "0x", 2);
							{ char b2[sizeof(uintptr_t)*2]; int i; for (i=0;i<rn;i++) b2[i]=rev[rn-1-i]; out(sk,b2,(size_t)rn); }
						}
					}
				}
				break;
			}
			case 'n': {
				void *ptr = a.p;
				switch (sp.lm) {
				case LM_hh: *(signed char *)ptr = (signed char)sk->count; break;
				case LM_h: *(short *)ptr = (short)sk->count; break;
				case LM_l: *(long *)ptr = (long)sk->count; break;
				case LM_ll: case LM_j: *(long long *)ptr = (long long)sk->count; break;
				/* The worst of the three: this stored through
				 * *(long *), writing FOUR bytes into the caller's
				 * EIGHT-byte size_t and leaving the upper four whatever
				 * they happened to be -- silent corruption of an object
				 * the caller owns, not merely a wrong number printed. */
				case LM_z: *(size_t *)ptr = (size_t)sk->count; break;
				case LM_t: *(ptrdiff_t *)ptr = (ptrdiff_t)sk->count; break;
				default: *(int *)ptr = (int)sk->count; break;
				}
				break;
			}
			case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
			case 'a': case 'A': {
				emit_float(sk, a.d, sp.conv, prec, flags & 16, flags, width);
				break;
			}
			default:
				/* an unknown conversion: emit it literally, the way
				 * glibc does -- the conversion character and its '%',
				 * without any "n$" that came between them, since what
				 * is being echoed is a specification that named no
				 * conversion at all */
				if (sp.conv) {
					out(sk, "%", 1);
					if (st == 1) out(sk, fp, 1);
					else out_units(sk, (const wchar_t *)fp, 1);
				}
				break;
			}
			if (sp.conv) fp += st;
		}
	}
	va_end(aq);
	return sk->bad ? -1 : (int)sk->count;
}

int __vfprintf(FILE *f, const char *fmt, va_list ap)
{
	return vfprintf_st(f, fmt, ap, 1);
}

/* sprintf/snprintf/vsprintf/vsnprintf share this: a throwaway FILE that
 * is a fixed (or, for plain sprintf, unbounded) memory buffer exactly
 * as __file_write already knows how to fill. */
/* fmt is required (forwarded into __vfprintf() unconditionally, which
 * itself requires it). s is deliberately NOT required: vasprintf()
 * below calls this with s == 0, cap == 0 to measure a format's length
 * without writing anything, mirroring vsnprintf(s, 0, ...)'s own
 * POSIX-documented "s may be a null pointer when n (here, cap) is
 * zero" convention (fprintf.html/snprintf) -- unlike the mem-family/
 * str-n family's own "still valid even at n == 0" ISO convention, this
 * family's own description explicitly carves out the opposite
 * exception, which is exactly the "unless explicitly stated otherwise"
 * escape ISO C 7.21.1p2 itself anticipates. Marking s here would be a
 * false claim about a real, load-bearing caller. */
static int vxprintf_mem(char *s, size_t cap, const char *fmt, va_list ap) __attribute__((nonnull(3)));
static int vxprintf_mem(char *s, size_t cap, const char *fmt, va_list ap)
{
	FILE mf; // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- implementation-owned transient memory-stream adapter is constructed from scratch, not copied
	int r;
	memset(&mf, 0, sizeof mf);
	mf.fd = -1;
	mf.pid = -1;
	mf.is_mem = 1;
	mf.writable = 1;
	mf.bufmode = _IONBF;   /* every write must land in mem_buf right away: nothing ever flushes this FILE */
	mf.mem_buf = (unsigned char *)s;
	mf.mem_size = cap;
	r = __vfprintf(&mf, fmt, ap);
	/* __fwrite gives even an unbuffered memory FILE a one-byte staging
	 * buffer through __ensure_buf, and this FILE is a local that never
	 * sees fclose, so releasing it is ours to do -- otherwise every
	 * sprintf/snprintf that writes anything leaks that byte.  Same
	 * ownership rule as vsscanf_impl in scanf.c. */
	free(mf.buf);
	if (cap) {
		size_t pos = mf.mem_len;
		if (pos >= cap) pos = cap - 1;
		s[pos] = 0;
	}
	return r;
}

int vsprintf(char *__restrict s, const char *__restrict fmt, __isoc_va_list ap)
{
	return vxprintf_mem(s, (size_t)-1, fmt, ap);
}
/* fprintf.html ERRORS: "The snprintf() function shall fail if:
 * [EOVERFLOW] The value of n is greater than {INT_MAX}." -- a shall-fail,
 * so the call is refused up front: nothing is formatted, s is not
 * touched, and the return is negative with errno set (RETURN VALUE: "If
 * an output error was encountered, these functions shall return a
 * negative value and set errno").  The clause exists because the return
 * type is int and the value promised is the number of bytes that WOULD
 * have been written had n been sufficiently large: past {INT_MAX} that
 * number need not be representable, so there is no right answer to
 * return and POSIX makes the call fail instead of inventing one.
 *
 * It belongs here rather than in vxprintf_mem() because n is the thing
 * being checked, and only this pair of interfaces has one.  vsprintf()
 * passes (size_t)-1 as a "no bound" sentinel and vasprintf() passes a
 * length it computed itself; neither takes an n from the caller, so
 * neither may be refused for the size of its cap.  vfprintf.html makes
 * vsnprintf() "equivalent to ... snprintf()" and sends its ERRORS back
 * to fprintf(), which is why both entry points come through here.
 *
 * The wide sibling swprintf() has an [EOVERFLOW] too (see
 * vswprintf_impl below), but it is a different clause -- n or more wide
 * characters requested -- and needs no ceiling of its own: swprintf()
 * returns a count of what it actually wrote, never a would-have-been
 * length, so an n past {INT_MAX} has nothing unrepresentable to report. */
int vsnprintf(char *__restrict s, size_t n, const char *__restrict fmt, __isoc_va_list ap)
{
	if (n > (size_t)INT_MAX) { errno = EOVERFLOW; return -1; }
	return vxprintf_mem(s, n, fmt, ap);
}
int vfprintf(FILE *__restrict f, const char *__restrict fmt, __isoc_va_list ap)
{
	return __vfprintf(f, fmt, ap);
}
int vprintf(const char *__restrict fmt, __isoc_va_list ap)
{
	return __vfprintf(stdout, fmt, ap);
}

int sprintf(char *__restrict s, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vsprintf(s, fmt, ap);
	va_end(ap);
	return r;
}
int snprintf(char *__restrict s, size_t n, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vsnprintf(s, n, fmt, ap);
	va_end(ap);
	return r;
}
int printf(const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}
int fprintf(FILE *__restrict f, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}

int vdprintf(int fd, const char *__restrict fmt, __isoc_va_list ap)
{
	/* No FILE exists for fd; wrap it in one just for the call, the way
	 * fdopen would, but without touching the descriptor table. */
	FILE f; // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- implementation-owned transient descriptor-stream adapter is constructed from scratch, not copied
	int r;
	memset(&f, 0, sizeof f);
	f.fd = fd;
	f.pid = -1;
	f.writable = 1;
	f.bufmode = _IONBF;
	r = __vfprintf(&f, fmt, ap);
	if (fflush(&f) < 0) r = -1;
	/* __ensure_buf() allocated f.buf on the first write (one byte, for
	 * _IONBF) and nothing else will ever free it: this FILE is a stack
	 * object that never reaches fclose(), and fflush() only drains the
	 * buffer, it does not release it.  Without this, every dprintf()/
	 * vdprintf() call leaks -- caught by ASan under tools/asan-build.sh
	 * once test/posix-stdio.c started calling them at all.  Guarded on
	 * user_buf the same way __fclose_locked() and setvbuf() are, even
	 * though nothing can have handed this FILE a user buffer, so the
	 * ownership rule stays stated in one form everywhere. */
	if (f.buf && !f.user_buf) free(f.buf);
	return r;
}
int dprintf(int fd, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vdprintf(fd, fmt, ap);
	va_end(ap);
	return r;
}

int vasprintf(char **s, const char *fmt, __isoc_va_list ap)
{
	va_list ap2;
	int n;
	va_copy(ap2, ap);
	n = vxprintf_mem(0, 0, fmt, ap2);
	va_end(ap2);
	if (n < 0) { *s = 0; return -1; }
	*s = malloc((size_t)n + 1);
	if (!*s) return -1;
	return vxprintf_mem(*s, (size_t)n + 1, fmt, ap);
}
int asprintf(char **s, const char *fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vasprintf(s, fmt, ap);
	va_end(ap);
	return r;
}

/* ------------------------------------------------------------------
 * THE WIDE FAMILY: fwprintf.html
 *
 * "Equivalent to fprintf() ... except that the argument format is a
 * wide-character string" and the result is wide characters.  Same
 * formatter, stride sizeof(wchar_t), sink counting wide characters.
 * ------------------------------------------------------------------ */
int __vfwprintf(FILE *f, const wchar_t *fmt, va_list ap)
{
	if (!f->wide) f->wide = 1;
	return vfprintf_st(f, (const char *)fmt, ap, (int)sizeof(wchar_t));
}

/* swprintf() is NOT snprintf() with a different unit, and the
 * difference is the whole reason this does not reuse vxprintf_mem():
 *
 *   snprintf  "shall return the number of bytes that would have been
 *              written had n been sufficiently large"  -- truncation is
 *              reported by a return >= n, and the caller re-sizes.
 *   swprintf  "If n or more wide characters were requested to be
 *              written, swprintf() shall return a negative value, and
 *              set errno"  -- there is no would-have-been length.
 *
 * So the buffer is given to the formatter as a wchar_t-holding memory
 * FILE (wmem, exactly like open_wmemstream()'s), the logical count is
 * compared against n afterwards, and overflow becomes -1/[EOVERFLOW]
 * rather than a length.  One wide character is reserved for the
 * terminating null, which is why the test is `>= n` and not `> n`. */
/* Unlike vxprintf_mem's own s above, swprintf() has no "just measure"
 * calling convention to make s optional: `if (!n) { errno = EOVERFLOW;
 * return -1; }` treats n == 0 as a real error, not a documented
 * "s may be null" case (swprintf.html has no would-have-been-length to
 * report, so there is no snprintf(s, 0, ...)-style idiom for it -- see
 * this function's own comment above). s is written unconditionally
 * (`s[mf.mem_len / sizeof(wchar_t)] = 0;`) on every path that is not
 * that n == 0 error; fmt is forwarded into vfprintf_st() the same way. */
static int vswprintf_impl(wchar_t *s, size_t n, const wchar_t *fmt, va_list ap)
    __attribute__((nonnull(1, 3)));
static int vswprintf_impl(wchar_t *s, size_t n, const wchar_t *fmt, va_list ap)
{
	FILE mf; // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- implementation-owned transient wide memory-stream adapter is constructed from scratch, not copied
	int r;

	if (!n) { errno = EOVERFLOW; return -1; }
	memset(&mf, 0, sizeof mf);
	mf.fd = -1;
	mf.pid = -1;
	mf.is_mem = 1;
	mf.wmem = 1;
	mf.wide = 1;
	mf.writable = 1;
	mf.bufmode = _IONBF;
	mf.mem_buf = (unsigned char *)s;
	/* One unit short of the caller's array: the terminating null lives
	 * in the unit this hides, so an overrun is detected as a short
	 * write rather than by writing it. */
	mf.mem_size = (n - 1) * sizeof(wchar_t);
	r = vfprintf_st(&mf, (const char *)fmt, ap, (int)sizeof(wchar_t));
	/* The terminating null is unconditional when n is nonzero, including
	 * the truncation/error path.  mem_len is the prefix actually stored. */
	s[mf.mem_len / sizeof(wchar_t)] = 0;
	/* Same buffer ownership as vxprintf_mem(): a local FILE never sees
	 * fclose, so the staging buffer __ensure_buf() gave it is ours. */
	free(mf.buf);
	if (r < 0) return r;
	if ((size_t)r >= n) { errno = EOVERFLOW; return -1; }
	return r;
}

int vswprintf(wchar_t *__restrict s, size_t n, const wchar_t *__restrict fmt, __isoc_va_list ap)
{
	return vswprintf_impl(s, n, fmt, ap);
}
int vfwprintf(FILE *__restrict f, const wchar_t *__restrict fmt, __isoc_va_list ap)
{
	return __vfwprintf(f, fmt, ap);
}
int vwprintf(const wchar_t *__restrict fmt, __isoc_va_list ap)
{
	return __vfwprintf(stdout, fmt, ap);
}

int swprintf(wchar_t *__restrict s, size_t n, const wchar_t *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vswprintf_impl(s, n, fmt, ap);
	va_end(ap);
	return r;
}
int fwprintf(FILE *__restrict f, const wchar_t *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfwprintf(f, fmt, ap);
	va_end(ap);
	return r;
}
int wprintf(const wchar_t *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfwprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}

// NOLINTEND(misc-include-cleaner)
