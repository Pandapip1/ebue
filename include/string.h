/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_STRING_H
#define	_STRING_H

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
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define __NEED_locale_t
#endif

#include <bits/alltypes.h>

/* The mem-family and str-family functions below are required to accept non-null dest/src
 * pointers by ISO C 7.24 (behaviour undefined otherwise, even when a
 * size_t count is zero -- e.g. 7.24.1p2's "even if n is zero" wording
 * for the mem* functions), the same real contract glibc's own
 * <string.h> marks with __nonnull -- and this directory's own .c files below
 * confirm it: none of them null-check before dereferencing. A few
 * genuinely do NOT require every pointer argument because their own
 * body has a real, structural escape (an early return that is taken
 * before the argument would ever be touched, not an incidental gap) --
 * those are called out individually where the discrepancy from their
 * sibling functions might otherwise look like an oversight. strcpy/
 * strncpy/strcat/strncat/strcoll/strxfrm/strrchr/strtok/strerror below
 * are deliberately left unmarked: strcpy/strcat simply forward to
 * stpcpy/strncat without dereferencing anything themselves (nothing in
 * THEIR own bodies for the attribute to describe), and strncpy/strcoll/
 * strxfrm/strrchr/strtok/strerror were never flagged by this tree's own
 * ownership sweep (strtok's own findings are all bounds/extent proofs,
 * a different obligation nonnull cannot express) -- left for a future,
 * separately-verified pass rather than guessed at here. */
void *memcpy (void *__restrict, const void *__restrict, size_t) __attribute__((nonnull(1, 2)));
void *memmove (void *, const void *, size_t) __attribute__((nonnull(1, 2)));
void *memset (void *, int, size_t) __attribute__((nonnull(1)));
int memcmp (const void *, const void *, size_t) __attribute__((nonnull(1, 2)));
void *memchr (const void *, int, size_t) __attribute__((nonnull(1)));

char *strcpy (char *__restrict, const char *__restrict);
char *strncpy (char *__restrict, const char *__restrict, size_t);

char *strcat (char *__restrict, const char *__restrict);
/* strncat's d is required (its own body's first statement, `d +=
 * strlen(d);`, dereferences it via strlen() with no check); s is
 * required the same way despite the `while (n && *s) ...` short
 * circuit -- matching mem*'s own "valid even at n == 0" contract, not
 * a genuine escape (there is no legitimate "s may be garbage when n is
 * 0" reading of strncat's own real contract). */
char *strncat (char *__restrict, const char *__restrict, size_t) __attribute__((nonnull(1, 2)));

/* Both l and r are required for strcmp: `*l == *r && *l` evaluates
 * both operands of `==` every time the loop condition is checked, with
 * no short circuit that could skip either one, so this is a real,
 * unconditional dereference of both -- not merely of whichever side
 * `&&` happens to test first. */
int strcmp (const char *, const char *) __attribute__((nonnull(1, 2)));
/* strncmp's loop condition (`*l && *r && n && *l == *r`) DOES short
 * circuit r's dereference on l's, but the unconditional `return *l -
 * *r;` after the loop dereferences both regardless of how the loop
 * ended -- reached on every path except n == 0 (which returns 0 before
 * either pointer is touched at all, the same mem*-style escape). */
int strncmp (const char *, const char *, size_t) __attribute__((nonnull(1, 2)));

int strcoll (const char *, const char *);
size_t strxfrm (char *__restrict, const char *__restrict, size_t);

/* src/string/strchr.c forwards to strchrnul(s, c) unconditionally and
 * dereferences its result; s is required (see strchrnul below), c is
 * an int value, not a pointer. */
char *strchr (const char *, int) __attribute__((nonnull(1)));
char *strrchr (const char *, int);

/* strcspn/strspn both require s and c: `if (!c[0] || !c[1])` (strcspn)
 * and `if (!c[0]) return 0;` (strspn) test c's CONTENT, not c's own
 * nullness -- dereferencing c[0] at all already requires c to be
 * valid, so neither check protects a caller that passes a genuinely
 * null c, and strspn's early return on c[0] == 0 is the same
 * "incidentally unreached, not a documented NULL convention" shape as
 * mem*'s own n == 0 escape (matching glibc's real strcspn/strspn,
 * both nonnull(1, 2)) -- not a real, callable "s may be invalid here"
 * contract the way strtok_r's own `if (!s && ...)` below genuinely is
 * (that one tests s's own nullness directly, and branches to
 * documented, meaningful behaviour, not just an early return). */
size_t strcspn (const char *, const char *) __attribute__((nonnull(1, 2)));
size_t strspn (const char *, const char *) __attribute__((nonnull(1, 2)));
/* strpbrk(s, b) forwards straight into strcspn(s, b) with no check of
 * its own, inheriting that function's real requirement on both. */
char *strpbrk (const char *, const char *) __attribute__((nonnull(1, 2)));
/* strstr requires n (`if (!n[0]) ...` dereferences it first) and h:
 * `h = strchr(h, *n)` is reached whenever n[0] != 0, and strchr always
 * dereferences its first argument once n[0] is known nonzero (see
 * strchrnul's own comment). */
char *strstr (const char *, const char *) __attribute__((nonnull(1, 2)));
char *strtok (char *__restrict, const char *__restrict);

size_t strlen (const char *) __attribute__((nonnull(1)));

char *strerror (int);

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#include <strings.h>
#endif

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
/* strtok_r's own s is genuinely optional -- `if (!s && !(s = *p)) return
 * 0;` is this function's documented "continue from *p" convention, the
 * exact same "caller-visible NULL is a real, legitimate value, not a
 * bug" shape src/env/setenv.c's own defensive checks were left
 * unmarked for in the previous ownership commit. sep and p are both
 * required: p is dereferenced unconditionally in that same `*p` read,
 * and sep is dereferenced by strspn()/strcspn() on every path that
 * does not return early via the "s is NULL and *p is NULL too" case. */
char *strtok_r (char *__restrict, const char *__restrict, char **__restrict) __attribute__((nonnull(2, 3)));
int strerror_r (int, char *, size_t);
/* stpcpy/stpncpy dereference both d and s unconditionally in their own
 * copy loop's condition (`(*d = *s)` / `n && (*d = *s)`) -- stpncpy's
 * `n &&` guard is the same size_t-count escape as mem*'s own n == 0
 * case (matching glibc's real stpncpy nonnull(1, 2)), not a genuine
 * "s need not be valid" reading. */
char *stpcpy(char *__restrict, const char *__restrict) __attribute__((nonnull(1, 2)));
char *stpncpy(char *__restrict, const char *__restrict, size_t) __attribute__((nonnull(1, 2)));
size_t strnlen (const char *, size_t);
char *strdup (const char *);
char *strndup (const char *, size_t);
char *strsignal(int);
char *strerror_l (int, locale_t);
int strcoll_l (const char *, const char *, locale_t);
size_t strxfrm_l (char *__restrict, const char *__restrict, size_t, locale_t);
#endif

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
/* Same n == 0 escape as mem*'s own family (glibc: memccpy nonnull(1, 2)). */
void *memccpy (void *__restrict, const void *__restrict, int, size_t) __attribute__((nonnull(1, 2)));
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
/* strsep's str is required (`char *s = *str;` dereferences it as the
 * very first statement); sep is deliberately left unmarked -- when
 * *str is NULL, `if (!s) return 0;` returns before sep is ever
 * touched, a real structural escape (str's own "resume here" NULL
 * convention, the same shape as strtok_r's s above), not an oversight. */
char *strsep(char **, const char *) __attribute__((nonnull(1)));
size_t strlcat (char *, const char *, size_t);
size_t strlcpy (char *, const char *, size_t);
void explicit_bzero (void *, size_t) __attribute__((nonnull(1)));
#endif

#ifdef _GNU_SOURCE
#define	strdupa(x)	strcpy(alloca(strlen(x)+1),x)
/* strverscmp dereferences l0/r0 unconditionally in its own first loop
 * (`for (dp = i = 0; l[i] == r[i]; i++)`), which -- like strcmp's `==`
 * above -- evaluates both sides every time, no short circuit. */
int strverscmp (const char *, const char *) __attribute__((nonnull(1, 2)));
/* strchrnul dereferences s unconditionally: `if (!c) return s +
 * strlen(s);` calls strlen(s) when c == 0, and the loop below
 * dereferences *s at least once otherwise. */
char *strchrnul(const char *, int) __attribute__((nonnull(1)));
/* strcasestr: h is dereferenced in its own loop condition (`for (;
 * *h; h++)`, evaluated at least once); n is dereferenced first via
 * `strlen(n)`, unconditionally, before h is ever touched. */
char *strcasestr(const char *, const char *) __attribute__((nonnull(1, 2)));
/* memmem: the needle (n0, param 3) is dereferenced directly (`*n` in
 * `memchr(h0, *n, k)`) whenever there is a non-empty search to do (`if
 * (!l) return h;` is the only escape, and it never touches n0 or h0);
 * the haystack (h0, param 1) is then handed to memchr(), which itself
 * dereferences it once l/k are both known nonzero -- the same
 * "genuinely required once there is a real range to search" shape as
 * mem*'s own n == 0 convention, matching glibc's real memmem
 * nonnull(1, 3). */
void *memmem(const void *, size_t, const void *, size_t) __attribute__((nonnull(1, 3)));
/* Same n == 0 escape as mem*'s own family (glibc: memrchr nonnull(1)). */
void *memrchr(const void *, int, size_t) __attribute__((nonnull(1)));
void *mempcpy(void *, const void *, size_t);
/* No basename here.  glibc's <string.h> declares a GNU basename that takes
 * a const char * and never modifies it, distinct from the POSIX basename in
 * <libgen.h>; musl used to paper over the difference with an unprototyped
 * `char *basename();', which is compatible with both.  C23 turns that into
 * `(void)', which conflicts with <libgen.h>'s real prototype, so musl
 * dropped the declaration in 1.2.5 ("string.h no longer provides
 * (C23-incompat) non-prototype decl of basename").  ntlibc follows: only
 * the POSIX basename exists, declared in <libgen.h>. */
#endif

#ifdef __cplusplus
}
#endif

#endif
