/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __vfprintf: the one formatter every printf/fprintf/sprintf/snprintf
 * variant calls into.  sprintf and snprintf are __vfprintf writing into
 * a throwaway FILE that is a fixed-size memory buffer exactly like
 * fmemopen's (see mem.c and __file_write in buf.c) but built on the
 * stack, so truncation and buffer-filling logic is not written twice.
 *
 * Floating-point conversions (%f/%e/%g and their capitals) are formatted
 * with plain double arithmetic -- multiply/divide to normalise, round
 * with +0.5, peel off decimal digits -- rather than an exact
 * arbitrary-precision decimal conversion such as glibc's.  That is
 * simpler and, for every ordinary magnitude, indistinguishable from
 * correct; it can be a unit or two off in the last digit for values
 * that sit exactly on a rounding boundary, and loses precision well
 * before 10^18.  %a/%A (hex float) and positional (%n$) arguments are
 * not implemented; nothing in this tree uses either.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <errno.h>
#include "stdio_impl.h"

enum { LM_NONE, LM_hh, LM_h, LM_l, LM_ll, LM_j, LM_z, LM_t, LM_L };

/* Write n bytes to f, tracking the logical (untruncated) total in
 * *count.  A short write is a real error unless f is a fixed memory
 * buffer (sprintf/snprintf), in which case it is just truncation. */
static void out(FILE *f, const char *s, size_t n, long *count, int *bad)
{
	if (*bad) return;
	if (n && __fwrite(s, 1, n, f) != n) {
		if (!f->is_mem || f->mem_dynamic) { f->err = 1; *bad = 1; return; }
	}
	*count += (long)n;
}

static void pad(FILE *f, char c, size_t n, long *count, int *bad)
{
	char buf[16];
	memset(buf, c, sizeof buf);
	while (n && !*bad) {
		size_t k = n < sizeof buf ? n : sizeof buf;
		out(f, buf, k, count, bad);
		n -= k;
	}
}

/* Round x (>= 0) to an integer.  A half rounds to even, like musl/glibc's
 * printf, but only when x is the caller's value unscaled (exact != 0):
 * after multiplying by a power of ten a half may be the product of an
 * inexact value (0.0005 * 1000), which the exact decimal expansion would
 * round up. */
static unsigned long long round_int(double x, int exact)
{
	unsigned long long iv = (unsigned long long)x;
	double frac = x - (double)iv;
	if (frac > 0.5 || (frac == 0.5 && (!exact || (iv & 1)))) iv++;
	return iv;
}

/* Decimal exponent of v (>0, finite) before any rounding: the power of
 * ten of its leading digit.  *m receives v scaled into [1, 10). */
static int decexp_of(double v, double *m)
{
	int e = 0;
	if (v != 0) {
		while (v >= 10.0) { v /= 10.0; e++; }
		while (v < 1.0) { v *= 10.0; e--; }
	}
	*m = v;
	return e;
}

/* Round v (>0, finite) to ndigits significant decimal digits.  digits[]
 * receives them left to right; *decexp is the power of ten of the
 * leftmost one (value == 0.digits * 10^(decexp+1), i.e. digits[0] is
 * the 10^decexp place).  Only the first 19 digits are computed (all a
 * double can carry, and all an unsigned long long can hold); the rest
 * are zero. */
static void dtoa(double v, int ndigits, char *digits, int *decexp)
{
	int e, i, nd;
	double m, scale = 1.0;
	unsigned long long iv, maxv = 1;

	if (ndigits < 1) ndigits = 1;
	if (ndigits > 34) ndigits = 34;
	nd = ndigits > 19 ? 19 : ndigits;
	e = decexp_of(v, &m);
	for (i = 1; i < nd; i++) scale *= 10.0;
	iv = round_int(m * scale, e == 0 && nd == 1);
	for (i = 0; i < nd; i++) maxv *= 10;
	if (iv >= maxv) { iv /= 10; e++; }
	for (i = nd - 1; i >= 0; i--) { digits[i] = (char)('0' + (int)(iv % 10)); iv /= 10; }
	for (i = nd; i < ndigits; i++) digits[i] = '0';
	*decexp = e;
}

/* %f-style body (no sign): pos digits before the point, then a point
 * and prec digits after it, taken from a decexp+1+prec-digit rounding
 * of v (clamped to at least one digit, which undersells precision only
 * when the whole request is below the least significant digit shown --
 * see the file comment). */
static int fmt_f(char *buf, double v, int prec, int alt)
{
	char digits[40];
	int decexp, ndigits, pos, i, n = 0;

	if (v == 0) {
		buf[n++] = '0';
		if (prec > 0 || alt) buf[n++] = '.';
		for (i = 0; i < prec; i++) buf[n++] = '0';
		return n;
	}
	/* Use the unrounded exponent to choose how many significant digits
	 * to ask for: rounding to a single digit (99.7 -> 1e2) would
	 * overestimate it and then truncate instead of round.  If the real
	 * rounding carries into a new place (99.7 at ".0f" -> "10" e2),
	 * the digits are all zero past the leading 1 and the zero padding
	 * below produces the right result. */
	{ double m; decexp = decexp_of(v, &m); }
	ndigits = decexp + 1 + prec;
	if (ndigits < 1) {
		/* v is small enough relative to prec that rounding to
		 * decexp+1+prec significant digits would clamp away the digit
		 * that decides how the last shown place rounds (e.g. 0.0005 at
		 * ".3f"): round the whole value to prec fractional digits
		 * directly instead. */
		double scale = 1.0;
		unsigned long long r;
		char tmp[40]; int tn = 0;
		for (i = 0; i < prec; i++) scale *= 10.0;
		r = round_int(v * scale, prec == 0);
		if (prec == 0) {
			/* v < 1 rounded to an integer: 0 or 1 */
			buf[n++] = (char)('0' + (int)r);
			if (alt) buf[n++] = '.';
			return n;
		}
		buf[n++] = '0';
		buf[n++] = '.';
		if (r == 0) tmp[tn++] = '0';
		while (r) { tmp[tn++] = (char)('0' + (int)(r % 10)); r /= 10; }
		while (tn < prec) tmp[tn++] = '0';
		for (i = tn - 1; i >= 0; i--) buf[n++] = tmp[i];
		return n;
	}
	if (ndigits > 34) ndigits = 34;
	dtoa(v, ndigits, digits, &decexp);
	pos = decexp + 1;
	if (pos <= 0) {
		buf[n++] = '0';
		if (prec > 0 || alt) buf[n++] = '.';
		for (i = 0; i < -pos && i < prec; i++) buf[n++] = '0';
		for (i = 0; i < prec + pos; i++) buf[n++] = i < ndigits ? digits[i] : '0';
	} else {
		for (i = 0; i < pos; i++) buf[n++] = i < ndigits ? digits[i] : '0';
		if (prec > 0 || alt) buf[n++] = '.';
		for (i = 0; i < prec; i++) buf[n++] = pos + i < ndigits ? digits[pos + i] : '0';
	}
	return n;
}

static int fmt_e(char *buf, double v, int prec, int alt, int upper)
{
	char digits[40];
	int decexp, i, n = 0;
	int ndigits = prec + 1;
	if (ndigits > 34) ndigits = 34;

	if (v == 0) { decexp = 0; memset(digits, '0', sizeof digits); }
	else dtoa(v, ndigits, digits, &decexp);

	buf[n++] = digits[0];
	if (prec > 0 || alt) {
		buf[n++] = '.';
		for (i = 1; i <= prec; i++) buf[n++] = i < ndigits ? digits[i] : '0';
	}
	buf[n++] = upper ? 'E' : 'e';
	buf[n++] = decexp < 0 ? '-' : '+';
	{
		int ax = decexp < 0 ? -decexp : decexp;
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
static int strip_g(char *buf, int n, int has_exp)
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

static void emit_float(FILE *f, double v, int conv, int prec, int alt, int flags, int width, long *count, int *bad)
{
	char body[256];
	int n, neg = signbit(v);
	int upper = conv == 'F' || conv == 'E' || conv == 'G';
	char sign = neg ? '-' : (flags & 1 ? '+' : (flags & 2 ? ' ' : 0));
	char av = (char)(conv == 'F' ? 'f' : conv == 'E' ? 'e' : conv == 'G' ? 'g' : conv);
	size_t prefixlen = sign ? 1 : 0;

	v = fabs(v);
	if (isnan(v)) { memcpy(body, upper ? "NAN" : "nan", 3); n = 3; if (!neg) sign = 0; }
	else if (isinf(v)) { memcpy(body, upper ? "INF" : "inf", 3); n = 3; }
	else if (av == 'f') n = fmt_f(body, v, prec < 0 ? 6 : prec, alt);
	else if (av == 'e') n = fmt_e(body, v, prec < 0 ? 6 : prec, alt, upper);
	else { /* g/G */
		int P = prec < 0 ? 6 : (prec == 0 ? 1 : prec);
		char tmp[40]; int decexp;
		if (v == 0) decexp = 0; else dtoa(v, P, tmp, &decexp);
		if (decexp < -4 || decexp >= P) { n = fmt_e(body, v, P - 1, alt, upper); if (!alt) n = strip_g(body, n, 1); }
		else { n = fmt_f(body, v, P - 1 - decexp, alt); if (!alt) n = strip_g(body, n, 0); }
	}

	{
		int pad_n = width - (int)n - (int)prefixlen;
		if (pad_n < 0) pad_n = 0;
		if (flags & 4) { /* left */
			if (sign) out(f, &sign, 1, count, bad);
			out(f, body, (size_t)n, count, bad);
			pad(f, ' ', (size_t)pad_n, count, bad);
		} else if ((flags & 8) && !isnan(v)) { /* zero pad, not for nan */
			if (sign) out(f, &sign, 1, count, bad);
			pad(f, '0', (size_t)pad_n, count, bad);
			out(f, body, (size_t)n, count, bad);
		} else {
			pad(f, ' ', (size_t)pad_n, count, bad);
			if (sign) out(f, &sign, 1, count, bad);
			out(f, body, (size_t)n, count, bad);
		}
	}
}

int __vfprintf(FILE *f, const char *fmt, va_list ap)
{
	long count = 0;
	int bad = 0;
	const char *p = fmt;

	while (*p && !bad) {
		if (*p != '%') {
			const char *start = p;
			while (*p && *p != '%') p++;
			out(f, start, (size_t)(p - start), &count, &bad);
			continue;
		}
		p++;
		if (*p == '%') { out(f, "%", 1, &count, &bad); p++; continue; }

		{
			int flags = 0; /* 1=+ 2=space 4=- 8=0 16=# */
			int width = 0, prec = -1, haswidth = 0;
			int lm = LM_NONE;
			int neg_width = 0;

			for (;; p++) {
				if (*p == '-') flags |= 4;
				else if (*p == '+') flags |= 1;
				else if (*p == ' ') flags |= 2;
				else if (*p == '0') flags |= 8;
				else if (*p == '#') flags |= 16;
				else break;
			}
			if (*p == '*') { width = va_arg(ap, int); p++; haswidth = 1; if (width < 0) { neg_width = 1; width = -width; } }
			else while (*p >= '0' && *p <= '9') { width = width * 10 + (*p++ - '0'); haswidth = 1; }
			if (neg_width) flags |= 4;
			(void)haswidth;
			if (*p == '.') {
				p++;
				if (*p == '*') { prec = va_arg(ap, int); p++; }
				else { prec = 0; while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0'); }
				if (prec < 0) prec = -1;
			}
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
				int base = *p == 'o' ? 8 : (*p == 'x' || *p == 'X') ? 16 : 10;
				int upper = *p == 'X';
				int issigned = *p == 'd' || *p == 'i';
				int neg = 0;
				unsigned long long uv;
				char digbuf[32]; int dn = 0;
				char prefix[3]; int pn = 0;

				if (issigned) {
					long long sv;
					switch (lm) {
					case LM_hh: sv = (signed char)va_arg(ap, int); break;
					case LM_h: sv = (short)va_arg(ap, int); break;
					case LM_l: sv = va_arg(ap, long); break;
					case LM_ll: case LM_j: sv = va_arg(ap, long long); break;
					case LM_z: case LM_t: sv = va_arg(ap, long); break;
					default: sv = va_arg(ap, int); break;
					}
					neg = sv < 0;
					uv = neg ? (unsigned long long)(-(sv)) : (unsigned long long)sv;
				} else {
					switch (lm) {
					case LM_hh: uv = (unsigned char)va_arg(ap, unsigned int); break;
					case LM_h: uv = (unsigned short)va_arg(ap, unsigned int); break;
					case LM_l: uv = va_arg(ap, unsigned long); break;
					case LM_ll: case LM_j: uv = va_arg(ap, unsigned long long); break;
					case LM_z: case LM_t: uv = va_arg(ap, unsigned long); break;
					default: uv = va_arg(ap, unsigned int); break;
					}
				}

				if (uv == 0 && prec == 0) { /* "" for 0 with explicit precision 0 */ }
				else {
					unsigned long long t = uv;
					do { digbuf[dn++] = "0123456789abcdef"[t % (unsigned)base] ; if (upper && digbuf[dn-1] > '9') digbuf[dn-1] -= 32; t /= (unsigned)base; } while (t);
				}
				while (dn < prec && dn < (int)sizeof digbuf) digbuf[dn++] = '0';

				if (neg) prefix[pn++] = '-';
				else if (issigned && (flags & 1)) prefix[pn++] = '+';
				else if (issigned && (flags & 2)) prefix[pn++] = ' ';
				if ((flags & 16) && base == 8 && (dn == 0 || digbuf[dn-1] != '0')) digbuf[dn++] = '0';
				if ((flags & 16) && base == 16 && uv != 0) { prefix[pn++] = '0'; prefix[pn++] = upper ? 'X' : 'x'; }

				{
					int total = dn + pn;
					int padn = width - total; if (padn < 0) padn = 0;
					int zero = (flags & 8) && !(flags & 4) && prec < 0;
					if (flags & 4) {
						out(f, prefix, (size_t)pn, &count, &bad);
						{ char rev[32]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(f, rev, (size_t)dn, &count, &bad); }
						pad(f, ' ', (size_t)padn, &count, &bad);
					} else if (zero) {
						out(f, prefix, (size_t)pn, &count, &bad);
						pad(f, '0', (size_t)padn, &count, &bad);
						{ char rev[32]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(f, rev, (size_t)dn, &count, &bad); }
					} else {
						pad(f, ' ', (size_t)padn, &count, &bad);
						out(f, prefix, (size_t)pn, &count, &bad);
						{ char rev[32]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(f, rev, (size_t)dn, &count, &bad); }
					}
				}
				break;
			}
			case 'c': {
				char c = (char)va_arg(ap, int);
				int padn = width - 1; if (padn < 0) padn = 0;
				if (flags & 4) { out(f, &c, 1, &count, &bad); pad(f, ' ', (size_t)padn, &count, &bad); }
				else { pad(f, ' ', (size_t)padn, &count, &bad); out(f, &c, 1, &count, &bad); }
				break;
			}
			case 's': {
				const char *s = va_arg(ap, const char *);
				size_t n;
				int padn;
				if (!s) s = "(null)";
				n = strlen(s);
				if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
				padn = width - (int)n; if (padn < 0) padn = 0;
				if (flags & 4) { out(f, s, n, &count, &bad); pad(f, ' ', (size_t)padn, &count, &bad); }
				else { pad(f, ' ', (size_t)padn, &count, &bad); out(f, s, n, &count, &bad); }
				break;
			}
			case 'p': {
				void *ptr = va_arg(ap, void *);
				uintptr_t uv = (uintptr_t)ptr;
				char digbuf[2 + sizeof(uintptr_t) * 2]; int dn = 0;
				if (!ptr) { out(f, "(nil)", 5, &count, &bad); break; }
				digbuf[dn++] = 'x'; digbuf[dn++] = '0';
				{
					char rev[sizeof(uintptr_t) * 2]; int rn = 0;
					do { rev[rn++] = "0123456789abcdef"[uv % 16]; uv /= 16; } while (uv);
					{
						int padn = width - (dn + rn); if (padn < 0) padn = 0;
						if (flags & 4) {
							out(f, "0x", 2, &count, &bad);
							{ char b2[sizeof(uintptr_t)*2]; int i; for (i=0;i<rn;i++) b2[i]=rev[rn-1-i]; out(f,b2,(size_t)rn,&count,&bad); }
							pad(f, ' ', (size_t)padn, &count, &bad);
						} else {
							pad(f, ' ', (size_t)padn, &count, &bad);
							out(f, "0x", 2, &count, &bad);
							{ char b2[sizeof(uintptr_t)*2]; int i; for (i=0;i<rn;i++) b2[i]=rev[rn-1-i]; out(f,b2,(size_t)rn,&count,&bad); }
						}
					}
				}
				break;
			}
			case 'n': {
				void *ptr = va_arg(ap, void *);
				switch (lm) {
				case LM_hh: *(signed char *)ptr = (signed char)count; break;
				case LM_h: *(short *)ptr = (short)count; break;
				case LM_l: *(long *)ptr = (long)count; break;
				case LM_ll: case LM_j: *(long long *)ptr = (long long)count; break;
				case LM_z: case LM_t: *(long *)ptr = (long)count; break;
				default: *(int *)ptr = (int)count; break;
				}
				break;
			}
			case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
				double v = va_arg(ap, double);
				emit_float(f, v, *p, prec, flags & 16, flags, width, &count, &bad);
				break;
			}
			default:
				/* an unknown conversion: emit it literally, the way glibc does */
				if (*p) { out(f, "%", 1, &count, &bad); out(f, p, 1, &count, &bad); }
				break;
			}
			if (*p) p++;
		}
	}
	return bad ? -1 : (int)count;
}

/* sprintf/snprintf/vsprintf/vsnprintf share this: a throwaway FILE that
 * is a fixed (or, for plain sprintf, unbounded) memory buffer exactly
 * as __file_write already knows how to fill. */
static int vxprintf_mem(char *s, size_t cap, const char *fmt, va_list ap)
{
	FILE mf;
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
int vsnprintf(char *__restrict s, size_t n, const char *__restrict fmt, __isoc_va_list ap)
{
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
	FILE f;
	int r;
	memset(&f, 0, sizeof f);
	f.fd = fd;
	f.pid = -1;
	f.writable = 1;
	f.bufmode = _IONBF;
	r = __vfprintf(&f, fmt, ap);
	fflush(&f);
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
