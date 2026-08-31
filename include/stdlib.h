/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif

#define __NEED_size_t
#define __NEED_wchar_t

#include <bits/alltypes.h>

int atoi (const char *);
long atol (const char *);
long long atoll (const char *);
double atof (const char *);

float strtof (const char *__restrict, char **__restrict);
double strtod (const char *__restrict, char **__restrict);
long double strtold (const char *__restrict, char **__restrict);

long strtol (const char *__restrict, char **__restrict, int);
unsigned long strtoul (const char *__restrict, char **__restrict, int);
long long strtoll (const char *__restrict, char **__restrict, int);
unsigned long long strtoull (const char *__restrict, char **__restrict, int);

int rand (void);
void srand (unsigned);

void *malloc (size_t);
void *calloc (size_t, size_t);
void *realloc (void *, size_t);
void free (void *);
void *aligned_alloc(size_t, size_t);

_Noreturn void abort (void);
int atexit (void (*) (void));
_Noreturn void exit (int);
_Noreturn void _Exit (int);
int at_quick_exit (void (*) (void));
_Noreturn void quick_exit (int);

/* getenv's name is required (POSIX: behaviour undefined if name is a
 * null pointer), and src/env/getenv.c never checks it -- unlike
 * setenv()/unsetenv() below, which deliberately DO check their own name
 * argument and return EINVAL on NULL (a defensive convention beyond what
 * POSIX requires), so those two are left unmarked: a nonnull attribute
 * there would tell the compiler their own `if (!name ...)` guard is dead
 * code, which is false. */
char *getenv (const char *) __attribute__((nonnull(1)));

int system (const char *);

void *bsearch (const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
void qsort (void *, size_t, size_t, int (*)(const void *, const void *));

/* abs/labs/llabs (src/stdlib/abs.c): `a > 0 ? a : -a`, a total
 * function of the one argument with no writes, no errno, no globals.
 * (abs(INT_MIN) etc. is signed-overflow UB, exactly like strlen(NULL)
 * is UB for nonnull-annotated strlen -- pure does not require the
 * function be defined on every representable input, only that it have
 * no side effects and be deterministic where it is defined.) */
int abs (int) __attribute__((__pure__));
long labs (long) __attribute__((__pure__));
long long llabs (long long) __attribute__((__pure__));

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

/* div/ldiv/lldiv (src/stdlib/div.c): `n / d`, `n % d` into a struct
 * returned by value -- the hidden pointer real ABIs use to return a
 * large struct is a calling-convention detail invisible to the C
 * abstract machine, not a pointer *argument* the C source itself
 * writes through, so it does not trip the "writes through a pointer
 * argument" disqualifier. No errno (integer division does not set it
 * in C), no globals, no I/O; d == 0 or n == INT_MIN/-1-shaped overflow
 * is UB, same caveat as abs() above. */
div_t div (int, int) __attribute__((__pure__));
ldiv_t ldiv (long, long) __attribute__((__pure__));
lldiv_t lldiv (long long, long long) __attribute__((__pure__));

int mblen (const char *, size_t);
int mbtowc (wchar_t *__restrict, const char *__restrict, size_t);
int wctomb (char *, wchar_t);
size_t mbstowcs (wchar_t *__restrict, const char *__restrict, size_t);
size_t wcstombs (char *__restrict, const wchar_t *__restrict, size_t);

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

#define MB_CUR_MAX (__ctype_get_mb_cur_max())
size_t __ctype_get_mb_cur_max(void);

#define RAND_MAX (0x7fffffff)

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)

#define WNOHANG    1
#define WUNTRACED  2

#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WSTOPSIG(s) WEXITSTATUS(s)
#define WIFEXITED(s) (!WTERMSIG(s))
#define WIFSTOPPED(s) ((short)((((unsigned)(s)&0xffff)*0x10001u)>>8) > 0x7f00)
#define WIFSIGNALED(s) (((s)&0xffff)-1U < 0xffu)

int posix_memalign (void **, size_t, size_t);
int setenv (const char *, const char *, int);
int unsetenv (const char *);
int mkstemp (char *);
int mkostemp (char *, int);
char *mkdtemp (char *);
int getsubopt (char **, char *const *, char **);
int rand_r (unsigned *);

#endif


#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
char *realpath (const char *__restrict, char *__restrict);
long int random (void);
void srandom (unsigned int);
char *initstate (unsigned int, char *, size_t);
char *setstate (char *);
/* Like getenv, putenv's argument is required and src/env/setenv.c's
 * putenv() never null-checks it -- POSIX again leaves NULL undefined
 * rather than specifying an error return, and unlike setenv/unsetenv
 * this function does not opt into the defensive EINVAL convention
 * either. */
int putenv (char *) __attribute__((nonnull(1)));
int posix_openpt (int);  /* undefined-ok: Unix98 pseudo-terminal allocation
	has no NT counterpart (NT's console/pipe model is a different shape
	entirely); grantpt/unlockpt/ptsname[_r] below are the rest of the
	same PTY API and share this reason */
int grantpt (int);  /* undefined-ok: see posix_openpt */
int unlockpt (int);  /* undefined-ok: see posix_openpt */
char *ptsname (int);  /* undefined-ok: see posix_openpt */
char *l64a (long);
long a64l (const char *);
void setkey (const char *);  /* undefined-ok: DES-based, like crypt()/
	encrypt() in unistd.h -- not reimplemented from scratch */
double drand48 (void);
double erand48 (unsigned short [3]);
long int lrand48 (void);
long int nrand48 (unsigned short [3]);
long mrand48 (void);
long jrand48 (unsigned short [3]);
void srand48 (long);
unsigned short *seed48 (unsigned short [3]);
void lcong48 (unsigned short [7]);
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#include <alloca.h>
char *mktemp (char *);
int mkstemps (char *, int);
int mkostemps (char *, int, int);
void *valloc (size_t);
void *memalign(size_t, size_t);
size_t malloc_usable_size(void *);
int getloadavg(double *, int);
#define WCOREDUMP(s) ((s) & 0x80)
#define WIFCONTINUED(s) ((s) == 0xffff)
void *reallocarray (void *, size_t, size_t);
void qsort_r (void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);
#endif

#ifdef _GNU_SOURCE
int ptsname_r(int, char *, size_t);  /* undefined-ok: see posix_openpt in
	the _XOPEN_SOURCE block above */
char *ecvt(double, int, int *, int *);
char *fcvt(double, int, int *, int *);
char *gcvt(double, int, char *);
#endif

#ifdef __cplusplus
}
#endif

#endif
