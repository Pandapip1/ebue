/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

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
/* memcmp/memchr (src/string/memcmp.c, memchr.c) read their buffer
 * arguments and nothing else: no writes through either pointer, no
 * errno, no global/static state, no I/O.  Two calls with the same
 * three arguments and unchanged memory in between always agree, which
 * is exactly __pure__'s contract -- matching glibc's own memcmp/memchr
 * declarations. */
int memcmp (const void *, const void *, size_t) __attribute__((nonnull(1, 2), __pure__));
void *memchr (const void *, int, size_t) __attribute__((nonnull(1), __pure__));

char *strcpy (char *__restrict, const char *__restrict __NTLIBC_STRING);
char *strncpy (char *__restrict, const char *__restrict, size_t);

char *strcat (char *__restrict __NTLIBC_STRING,
              const char *__restrict __NTLIBC_STRING);
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
/* strcmp reads through l/r only (src/string/strcmp.c), same __pure__
 * reasoning as memcmp above -- matching glibc's real strcmp
 * declaration, __attribute__((pure, nonnull(1, 2))). */
int strcmp (const char * __NTLIBC_STRING, const char * __NTLIBC_STRING)
    __attribute__((nonnull(1, 2), __pure__));
/* strncmp's loop condition (`*l && *r && n && *l == *r`) DOES short
 * circuit r's dereference on l's, but the unconditional `return *l -
 * *r;` after the loop dereferences both regardless of how the loop
 * ended -- reached on every path except n == 0 (which returns 0 before
 * either pointer is touched at all, the same mem*-style escape). Reads
 * only, same __pure__ reasoning as strcmp. */
int strncmp (const char *, const char *, size_t) __attribute__((nonnull(1, 2), __pure__));

/* strcoll (src/string/strcoll.c) is a one-line forward to strcmp():
 * "collation order in the POSIX locale is byte order" per that file's
 * own comment, and this tree has no locale but POSIX/C -- so, unlike a
 * real libc with installable collation tables, there is no runtime
 * state this could ever vary on. Deliberately left without nonnull
 * (matching string.h's own banner comment above), but that is an
 * orthogonal, unrelated proof obligation -- __pure__ only needs "no
 * side effects, deterministic in the arguments", which holds
 * regardless of whether l/r's own nullness has been proven. */
int strcoll (const char * __NTLIBC_STRING, const char * __NTLIBC_STRING)
    __attribute__((__pure__));
size_t strxfrm (char *__restrict, const char *__restrict, size_t);

/* src/string/strchr.c forwards to strchrnul(s, c) unconditionally and
 * dereferences its result; s is required (see strchrnul below), c is
 * an int value, not a pointer. The result itself is proven nonnull
 * too, via strchrnul's own `returns_nonnull` (see its comment below),
 * not anything expressible on strchr's own signature. Reads only,
 * matching glibc's real strchr __attribute__((pure)). */
char *strchr (const char * __NTLIBC_STRING, int)
    __attribute__((nonnull(1), __pure__));
/* strrchr (src/string/strrchr.c) forwards into memrchr(s, c,
 * strlen(s)+1) -- reads only, same __pure__ reasoning. */
char *strrchr (const char * __NTLIBC_STRING, int) __attribute__((__pure__));

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
size_t strcspn (const char *, const char *) __attribute__((nonnull(1, 2), __pure__));
size_t strspn (const char *, const char *) __attribute__((nonnull(1, 2), __pure__));
/* strpbrk(s, b) forwards straight into strcspn(s, b) with no check of
 * its own, inheriting that function's real requirement on both, and
 * the same read-only __pure__ reasoning. */
char *strpbrk (const char *, const char *) __attribute__((nonnull(1, 2), __pure__));
/* strstr requires n (`if (!n[0]) ...` dereferences it first) and h:
 * `h = strchr(h, *n)` is reached whenever n[0] != 0, and strchr always
 * dereferences its first argument once n[0] is known nonzero (see
 * strchrnul's own comment). Reads only, matching glibc's real strstr
 * __attribute__((pure)). */
char *strstr (const char *, const char *) __attribute__((nonnull(1, 2), __pure__));
char *strtok (char *__restrict, const char *__restrict);

/* strlen (src/string/strlen.c): a read-only scan for the terminating
 * NUL, no writes, no errno, no globals -- matching glibc's own strlen
 * declaration, __attribute__((pure, nonnull(1))), cited as this
 * project's own precedent for the whole family. */
size_t strlen (const char * __NTLIBC_STRING)
    __attribute__((nonnull(1), __pure__));

/* strerror (src/string/strerror.c) returns a pointer into a fixed,
 * compile-time-initialized `static const char *const __errmsgs[]`
 * table indexed by e -- never written anywhere in this tree, so two
 * calls with the same e always return the same address; no errno, no
 * I/O, no other global touched. Note this would NOT be safe to mark in
 * a libc whose strerror() can vary with the current LC_MESSAGES
 * locale (real POSIX behaviour on most systems) -- it is safe here
 * specifically because src/misc/locale.c's setlocale() never accepts
 * any locale but "C"/"POSIX", so there is no second message table this
 * could ever select at runtime. */
char *strerror (int) __attribute__((__pure__));

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
/* strnlen (src/string/strnlen.c) is memchr(s, 0, n) plus arithmetic --
 * reads only, no writes, no errno, no globals; matching glibc's real
 * strnlen __attribute__((pure)). Left without nonnull here (matching
 * this header's own banner comment: it was never separately verified
 * against the ownership sweep's own proof obligation), an unrelated,
 * orthogonal claim from __pure__. */
size_t strnlen (const char *, size_t) __attribute__((__pure__));
char *strdup (const char * __NTLIBC_STRING)
    __NTLIBC_RETURNS_OWNERSHIP(malloc);
char *strndup (const char *, size_t) __NTLIBC_RETURNS_OWNERSHIP(malloc);
/* strsignal (src/string/strsignal.c): same fixed-static-table shape as
 * strerror() above, indexed by sig -- __pure__ for the same reason. */
char *strsignal(int) __attribute__((__pure__));
/* strerror_l/strcoll_l (src/string/strerror.c, strcoll.c) both
 * `(void)loc;` their own locale_t and forward straight into
 * strerror()/strcoll() above -- ignoring an argument entirely is still
 * a deterministic function of it, and this tree's one-locale design
 * (see strerror's own comment above) is exactly why there is no second
 * behaviour loc could ever select. */
char *strerror_l (int, locale_t) __attribute__((__pure__));
int strcoll_l (const char *, const char *, locale_t) __attribute__((__pure__));
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
int strverscmp (const char *, const char *) __attribute__((nonnull(1, 2), __pure__));
/* strchrnul dereferences s unconditionally: `if (!c) return s +
 * strlen(s);` calls strlen(s) when c == 0, and the loop below
 * dereferences *s at least once otherwise. Reads only. Also
 * genuinely returns_nonnull: every one of its own two return
 * statements (`(char *)s + strlen(s)` and the loop's own `(char
 * *)s`) is s itself plus pointer arithmetic that only ever advances
 * forward within the same NUL-terminated string -- never NULL as
 * long as s is (already required above). src/string/strchr.c's own
 * `char *r = strchrnul(s, c); ... *(unsigned char *)r ...` is what
 * this unblocks: OwnershipChecker.cpp's own isAlwaysNonNull now
 * honors `returns_nonnull` the same way checkBeginFunction already
 * honors `nonnull` on parameters. */
char *strchrnul(const char *, int) __attribute__((nonnull(1), __pure__, returns_nonnull));
/* strcasestr: h is dereferenced in its own loop condition (`for (;
 * *h; h++)`, evaluated at least once); n is dereferenced first via
 * `strlen(n)`, unconditionally, before h is ever touched. Reads only
 * (via strncasecmp/tolower, both already read-only). */
char *strcasestr(const char *, const char *) __attribute__((nonnull(1, 2), __pure__));
/* memmem: the needle (n0, param 3) is dereferenced directly (`*n` in
 * `memchr(h0, *n, k)`) whenever there is a non-empty search to do (`if
 * (!l) return h;` is the only escape, and it never touches n0 or h0);
 * the haystack (h0, param 1) is then handed to memchr(), which itself
 * dereferences it once l/k are both known nonzero -- the same
 * "genuinely required once there is a real range to search" shape as
 * mem*'s own n == 0 convention, matching glibc's real memmem
 * nonnull(1, 3). Reads only. */
void *memmem(const void *, size_t, const void *, size_t) __attribute__((nonnull(1, 3), __pure__));
/* Same n == 0 escape as mem*'s own family (glibc: memrchr nonnull(1)).
 * Reads only. */
void *memrchr(const void *, int, size_t) __attribute__((nonnull(1), __pure__));
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

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
