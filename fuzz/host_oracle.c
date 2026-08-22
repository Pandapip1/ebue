/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The oracle: the host C library's answer to the same question, for the
 * harnesses that check ntlibc for *wrong results* rather than only for
 * memory errors.  strtod("1e442") returning NaN is not something a
 * sanitizer can see; a differential check is.
 *
 * This is the one file in fuzz/ compiled against the *host* headers, so
 * it cannot include anything of ntlibc's.  It also cannot just call
 * strtod(): ntlibc's strtod is linked into the same executable and would
 * win, and the oracle would be comparing ntlibc against itself.  dlsym on
 * libc.so.6 gets the real one -- ntlibc's definitions are built hidden, so
 * they are not in the dynamic symbol table and cannot be found this way.
 *
 * Reporting lives here too, and deliberately so: a harness must not format
 * a mismatch with ntlibc's own snprintf, which is itself under test.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

typedef double (*strtod_fn)(const char *, char **);
typedef float (*strtof_fn)(const char *, char **);
typedef long long (*strtoll_fn)(const char *, char **, int);

static void *libc(void)
{
	static void *h;
	if (!h) {
		h = dlopen("libc.so.6", RTLD_LAZY | RTLD_LOCAL);
		if (!h) { fprintf(stderr, "oracle: %s\n", dlerror()); abort(); }
	}
	return h;
}

static void *sym(const char *n)
{
	void *p = dlsym(libc(), n);
	if (!p) { fprintf(stderr, "oracle: no %s\n", n); abort(); }
	return p;
}

double host_strtod(const char *s, size_t *endoff, int *err)
{
	static strtod_fn f;
	char *e;
	double r;
	if (!f) f = (strtod_fn)sym("strtod");
	errno = 0;
	r = f(s, &e);
	*err = errno;
	*endoff = (size_t)(e - s);
	return r;
}

float host_strtof(const char *s, size_t *endoff, int *err)
{
	static strtof_fn f;
	char *e;
	float r;
	if (!f) f = (strtof_fn)sym("strtof");
	errno = 0;
	r = f(s, &e);
	*err = errno;
	*endoff = (size_t)(e - s);
	return r;
}

long long host_strtoll(const char *s, size_t *endoff, int base, int *err)
{
	static strtoll_fn f;
	char *e;
	long long r;
	if (!f) f = (strtoll_fn)sym("strtoll");
	errno = 0;
	r = f(s, &e, base);
	*err = errno;
	*endoff = (size_t)(e - s);
	return r;
}

/* The printf harness's oracle: glibc's snprintf, one argument at a time. */
typedef int (*snprintf_fn)(char *, size_t, const char *, ...);
static snprintf_fn hsnp(void)
{
	static snprintf_fn f;
	if (!f) f = (snprintf_fn)sym("snprintf");
	return f;
}
int host_snprintf_1d(char *b, size_t n, const char *f, double v)      { return hsnp()(b, n, f, v); }
int host_snprintf_1ll(char *b, size_t n, const char *f, long long v)  { return hsnp()(b, n, f, v); }
int host_snprintf_1s(char *b, size_t n, const char *f, const char *v) { return hsnp()(b, n, f, v); }
int host_snprintf_1p(char *b, size_t n, const char *f, void *v)       { return hsnp()(b, n, f, v); }
int host_snprintf_0(char *b, size_t n, const char *f)                 { return hsnp()(b, n, f); }

/* ---------------------------------------------------------- reporting */

/*
 * Formatted with the host's snprintf and written with write(2).  Not
 * fprintf: in this executable `fprintf` is ntlibc's (it is the definition
 * the linker sees), so an oracle that reported through it would be
 * printing the numbers with the very code it is meant to be checking --
 * and ntlibc's printf does not implement %a, which is exactly the format
 * a bit-level float difference has to be shown in.
 */
static char rbuf[4096];
static size_t rlen;

static void emit(void)
{
	ssize_t ignored = write(2, rbuf, rlen);
	(void)ignored;
	rlen = 0;
}

static void addf(const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = ((int (*)(char *, size_t, const char *, va_list))sym("vsnprintf"))
	      (rbuf + rlen, sizeof rbuf - rlen, fmt, ap);
	va_end(ap);
	if (n > 0) rlen += (size_t)n < sizeof rbuf - rlen ? (size_t)n : sizeof rbuf - rlen - 1;
}

static void addq(const char *s)
{
	addf("\"");
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') addf("\\%c", c);
		else if (c >= 0x20 && c < 0x7f) addf("%c", c);
		else addf("\\x%02x", c);
	}
	addf("\"");
}

/*
 * The host's abort(), not ntlibc's.  ntlibc's abort goes through its own
 * raise(), which on a native build has no real signal to deliver -- the
 * process would just stop, and libFuzzer would neither print a report nor
 * name the reproducing input.  A genuine SIGABRT is what its crash
 * handler is waiting for.
 */
static void host_abort(void)
{
	static void (*f)(void);
	if (!f) f = (void (*)(void))sym("abort");
	f();
	_exit(70);
}

void oracle_mismatch_d(const char *what, const char *in, double got, double want)
{
	addf("MISMATCH %s\n  input : ", what); addq(in);
	addf("\n  ntlibc: %.17g (%a)\n  glibc : %.17g (%a)\n", got, got, want, want);
	emit();
	host_abort();
}

void oracle_mismatch_i(const char *what, const char *in, long long got, long long want)
{
	addf("MISMATCH %s\n  input : ", what); addq(in);
	addf("\n  ntlibc: %lld\n  glibc : %lld\n", got, want);
	emit();
	host_abort();
}

void oracle_mismatch_s(const char *what, const char *in, const char *got, const char *want)
{
	addf("MISMATCH %s\n  input : ", what); addq(in);
	addf("\n  ntlibc: "); addq(got);
	addf("\n  glibc : "); addq(want);
	addf("\n");
	emit();
	host_abort();
}
