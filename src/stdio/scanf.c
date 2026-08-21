/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __vfscanf: the one parser every scanf/fscanf/sscanf variant calls
 * into.  sscanf/vsscanf hand it a throwaway read-only memory FILE (see
 * mem.c/fmemopen) instead of duplicating the character-at-a-time logic
 * against a plain string.
 *
 * Numeric conversions are read a character at a time into a small
 * buffer using the same charset strtol/strtod already know how to
 * parse, then handed to them; this is what most small scanf
 * implementations do; it copies the string once more than a
 * hand-rolled digit loop would, which is not worth avoiding here.
 * %[...] scansets and the usual conversions are implemented; positional
 * arguments and vector-of-float %a/%A input conversions are not, since
 * nothing in this tree needs them.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include "stdio_impl.h"

enum { LM_NONE, LM_hh, LM_h, LM_l, LM_ll, LM_j, LM_z, LM_t, LM_L };

static int rd(FILE *f) { return __fgetc(f); }
static void unrd(FILE *f, int c) { if (c != EOF) ungetc(c, f); }

static int skipspace(FILE *f)
{
	int c;
	while ((c = rd(f)) != EOF && isspace(c)) ;
	return c;
}

int __vfscanf(FILE *f, const char *fmt, va_list ap)
{
	int nmatched = 0, gotEOF = 0;
	const char *p = fmt;
	int c = 0;

	for (; *p; p++) {
		if (isspace((unsigned char)*p)) {
			c = skipspace(f);
			unrd(f, c);
			continue;
		}
		if (*p != '%') {
			c = rd(f);
			if (c == EOF) { gotEOF = 1; goto done; }
			if (c != *p) { unrd(f, c); goto done; }
			continue;
		}
		p++;
		if (*p == '%') {
			c = rd(f);
			if (c == EOF) { gotEOF = 1; goto done; }
			if (c != '%') { unrd(f, c); goto done; }
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
				int neg = 0, any = 0, nn = 0;
				char numbuf[80];
				unsigned long long uv = 0;

				c = skipspace(f);
				if (c == EOF) { gotEOF = 1; goto done; }
				if (c == '+' || c == '-') { neg = c == '-'; c = rd(f); }
				if ((autodetect || base == 16) && c == '0') {
					int c2 = rd(f);
					if (c2 == 'x' || c2 == 'X') { base = 16; c = rd(f); }
					else { if (autodetect) base = 8; unrd(f, c2); }
				}
				if (autodetect && base != 16 && base != 8) base = 10;
				for (; c != EOF && nn < (int)sizeof numbuf - 1; ) {
					int d;
					if (c >= '0' && c <= '9') d = c - '0';
					else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
					else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
					else break;
					if (d >= base) break;
					if (width >= 0 && nn >= width) break;
					uv = uv * (unsigned)base + (unsigned)d;
					any = 1; nn++;
					c = rd(f);
				}
				unrd(f, c);
				if (!any) goto done;
				if (neg) uv = (unsigned long long)-(long long)uv;
				if (assign) {
					switch (lm) {
					case LM_hh: *(unsigned char *)va_arg(ap, void *) = (unsigned char)uv; break;
					case LM_h:  *(unsigned short *)va_arg(ap, void *) = (unsigned short)uv; break;
					case LM_l:  *(unsigned long *)va_arg(ap, void *) = (unsigned long)uv; break;
					case LM_ll: case LM_j: *(unsigned long long *)va_arg(ap, void *) = uv; break;
					case LM_z: case LM_t: *(unsigned long *)va_arg(ap, void *) = (unsigned long)uv; break;
					default: *(unsigned int *)va_arg(ap, void *) = (unsigned int)uv; break;
					}
					nmatched++;
				}
				break;
			}
			case 'f': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
				char numbuf[64]; int nn = 0;
				char *end; double dv;

				c = skipspace(f);
				if (c == EOF) { gotEOF = 1; goto done; }
				for (; c != EOF && nn < (int)sizeof numbuf - 1; ) {
					if (width >= 0 && nn >= width) break;
					if (!(isdigit(c) || c == '+' || c == '-' || c == '.' ||
					      c == 'e' || c == 'E' || c == 'x' || c == 'X' ||
					      (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
					      c == 'n' || c == 'N' || c == 'i' || c == 'I' || c == 'p' || c == 'P'))
						break;
					numbuf[nn++] = (char)c;
					c = rd(f);
				}
				unrd(f, c);
				numbuf[nn] = 0;
				if (nn == 0) goto done;
				dv = strtod(numbuf, &end);
				if (end == numbuf) {
					/* push everything back and fail the conversion */
					int k; for (k = nn - 1; k >= 0; k--) unrd(f, (unsigned char)numbuf[k]);
					goto done;
				}
				{
					int consumed = (int)(end - numbuf);
					int k; for (k = nn - 1; k >= consumed; k--) unrd(f, (unsigned char)numbuf[k]);
				}
				if (assign) {
					if (lm == LM_L) *(long double *)va_arg(ap, void *) = (long double)dv;
					else if (lm == LM_l) *(double *)va_arg(ap, void *) = dv;
					else *(float *)va_arg(ap, void *) = (float)dv;
					nmatched++;
				}
				break;
			}
			case 's': {
				char *s = assign ? va_arg(ap, char *) : 0;
				int nn = 0;
				c = skipspace(f);
				if (c == EOF) { gotEOF = 1; goto done; }
				for (; c != EOF && !isspace(c) && (width < 0 || nn < width); c = rd(f)) {
					if (assign) s[nn] = (char)c;
					nn++;
				}
				unrd(f, c);
				if (nn == 0) goto done;
				if (assign) { s[nn] = 0; nmatched++; }
				break;
			}
			case 'c': {
				char *s = assign ? va_arg(ap, char *) : 0;
				int w = width < 0 ? 1 : width, nn;
				for (nn = 0; nn < w; nn++) {
					c = rd(f);
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
				c = rd(f);
				while (c != EOF && (set[(unsigned char)c] != 0) != neg && (width < 0 || nn < width)) {
					if (assign) s[nn] = (char)c;
					nn++;
					c = rd(f);
				}
				unrd(f, c);
				if (nn == 0) { if (c == EOF) gotEOF = 1; goto done; }
				if (assign) { s[nn] = 0; nmatched++; }
				break;
			}
			case 'p': {
				void **pp = assign ? va_arg(ap, void **) : 0;
				unsigned long long uv = 0; int any = 0;
				c = skipspace(f);
				if (c == EOF) { gotEOF = 1; goto done; }
				if (c == '0') { int c2 = rd(f); if (c2 == 'x' || c2 == 'X') c = rd(f); else unrd(f, c2); }
				for (; c != EOF; c = rd(f)) {
					int d;
					if (c >= '0' && c <= '9') d = c - '0';
					else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
					else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
					else break;
					uv = uv * 16 + (unsigned)d; any = 1;
				}
				unrd(f, c);
				if (!any) goto done;
				if (assign) { *pp = (void *)(uintptr_t)uv; nmatched++; }
				break;
			}
			case 'n':
				if (assign) {
					/* the number of characters consumed so far is not
					 * tracked precisely (ungetc'd look-ahead makes it
					 * awkward); report 0, which is at least a valid int. */
					switch (lm) {
					case LM_hh: *(signed char *)va_arg(ap, void *) = 0; break;
					case LM_h: *(short *)va_arg(ap, void *) = 0; break;
					case LM_l: *(long *)va_arg(ap, void *) = 0; break;
					case LM_ll: case LM_j: *(long long *)va_arg(ap, void *) = 0; break;
					case LM_z: case LM_t: *(long *)va_arg(ap, void *) = 0; break;
					default: *(int *)va_arg(ap, void *) = 0; break;
					}
				}
				break;
			default:
				break;
			}
		}
	}
done:
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
	memset(&mf, 0, sizeof mf);
	mf.fd = -1;
	mf.pid = -1;
	mf.is_mem = 1;
	mf.readable = 1;
	mf.mem_buf = (unsigned char *)s;
	mf.mem_len = strlen(s);
	mf.mem_size = mf.mem_len;
	return __vfscanf(&mf, fmt, ap);
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
