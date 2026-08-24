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
 *
 * errno itself needs the same treatment as strtod: `errno` is
 * `*__errno_location()`, and ntlibc's harnesses link this file's object
 * directly against ntlibc's *.o (see Makefile), which defines its own
 * __errno_location.  The plain `errno` macro from <errno.h> would therefore
 * resolve, at static link time, straight to ntlibc's thread-local errno
 * rather than glibc's -- there is no dynamic symbol lookup involved for a
 * call to a symbol the static linker can already see defined in the same
 * executable, so RTLD-level tricks (LD_PRELOAD, RTLD_NEXT) do not even
 * enter into it.  ntlibc's __errno_location is hidden-visibility, so it is
 * absent from the dynamic symbol table; going through dlsym() on the
 * explicit libc.so.6 handle above (same as strtod et al.) reaches glibc's
 * real __errno_location and cannot see ntlibc's hidden one at all.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

typedef double (*strtod_fn)(const char *, char **);
typedef float (*strtof_fn)(const char *, char **);
typedef long long (*strtoll_fn)(const char *, char **, int);
typedef unsigned long long (*strtoull_fn)(const char *, char **, int);
typedef int *(*errno_loc_fn)(void);

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

/* glibc's real errno, not ntlibc's -- see the comment at the top of the
 * file for why the plain `errno` macro cannot be used here. */
static int *host_errno_loc(void)
{
	static errno_loc_fn f;
	if (!f) f = (errno_loc_fn)sym("__errno_location");
	return f();
}

double host_strtod(const char *s, size_t *endoff, int *err)
{
	static strtod_fn f;
	char *e;
	double r;
	if (!f) f = (strtod_fn)sym("strtod");
	*host_errno_loc() = 0;
	r = f(s, &e);
	*err = *host_errno_loc();
	*endoff = (size_t)(e - s);
	return r;
}

float host_strtof(const char *s, size_t *endoff, int *err)
{
	static strtof_fn f;
	char *e;
	float r;
	if (!f) f = (strtof_fn)sym("strtof");
	*host_errno_loc() = 0;
	r = f(s, &e);
	*err = *host_errno_loc();
	*endoff = (size_t)(e - s);
	return r;
}

long long host_strtoll(const char *s, size_t *endoff, int base, int *err)
{
	static strtoll_fn f;
	char *e;
	long long r;
	if (!f) f = (strtoll_fn)sym("strtoll");
	*host_errno_loc() = 0;
	r = f(s, &e, base);
	*err = *host_errno_loc();
	*endoff = (size_t)(e - s);
	return r;
}

unsigned long long host_strtoull(const char *s, size_t *endoff, int base, int *err)
{
	static strtoull_fn f;
	char *e;
	unsigned long long r;
	if (!f) f = (strtoull_fn)sym("strtoull");
	*host_errno_loc() = 0;
	r = f(s, &e, base);
	*err = *host_errno_loc();
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

/* ------------------------------------------------- the struct stat seam
 *
 * See fuzz/statshim.h for why this is here.  This file is the only one in
 * fuzz/ compiled against the host headers, so it is the only one that can
 * write a host `struct stat` without transcribing its layout by hand.
 */
#include <sys/stat.h>
#include "statshim.h"

unsigned long __ntfuzz_host_stat_size(void) { return (unsigned long)sizeof(struct stat); }

void __ntfuzz_pack_stat(void *hostbuf, const struct ntfuzz_stat *s)
{
	struct stat *st = (struct stat *)hostbuf;
	memset(st, 0, sizeof *st);
	st->st_dev     = (dev_t)s->dev;
	st->st_ino     = (ino_t)s->ino;
	st->st_rdev    = (dev_t)s->rdev;
	st->st_nlink   = (nlink_t)s->nlink;
	st->st_mode    = (mode_t)s->mode;
	st->st_uid     = (uid_t)s->uid;
	st->st_gid     = (gid_t)s->gid;
	st->st_size    = (off_t)s->size;
	st->st_blksize = (blksize_t)s->blksize;
	st->st_blocks  = (blkcnt_t)s->blocks;
	st->st_atim.tv_sec  = (time_t)s->atim_sec;  st->st_atim.tv_nsec = (long)s->atim_nsec;
	st->st_mtim.tv_sec  = (time_t)s->mtim_sec;  st->st_mtim.tv_nsec = (long)s->mtim_nsec;
	st->st_ctim.tv_sec  = (time_t)s->ctim_sec;  st->st_ctim.tv_nsec = (long)s->ctim_nsec;
}
