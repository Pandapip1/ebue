/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _WCHAR_H
#define _WCHAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_FILE
#define __NEED___isoc_va_list
#define __NEED_size_t
#define __NEED_wchar_t
#define __NEED_wint_t
#define __NEED_mbstate_t
#define __NEED_locale_t
#define __NEED_struct_tm

#include <bits/alltypes.h>

#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif

#undef WEOF
#define WEOF 0xffffffffU

#define WCHAR_MIN 0
#define WCHAR_MAX 0xffff

/* This whole family mirrors this directory's own str-family/mem-family
 * .c bodies one for one (several of the .c files above say so explicitly), so the
 * same per-function reasoning given there applies here; only the
 * wchar_t-specific escapes are called out again below. wcscat/wcsrchr/
 * wcsnlen/wcsdup/wmemcpy/wmemmove were never flagged by this tree's own
 * ownership sweep (wcsrchr's own finding is a bounds/extent proof, not
 * a nullness one) -- left unmarked for a future, separately-verified
 * pass rather than guessed at here. */
/* wcscpy dereferences d and s together, unconditionally, in its own
 * loop condition (`(*d++ = *s++)`, evaluated at least once). */
wchar_t *wcscpy (wchar_t *__restrict, const wchar_t *__restrict) __attribute__((nonnull(1, 2)));
/* wcsncpy's `while (n && *s) ...` short-circuits s on n, and d is only
 * written inside that same loop body -- but wmemset(d, 0, n) always
 * runs afterward, unconditionally, and (like wmemset's own n == 0
 * escape below) dereferences d whenever n >= 1; s is dereferenced by
 * the loop's own condition on that same n >= 1 path. Both required,
 * matching mem*'s n == 0 convention, not a genuine escape. */
wchar_t *wcsncpy (wchar_t *__restrict, const wchar_t *__restrict, size_t) __attribute__((nonnull(1, 2)));
wchar_t *wcscat (wchar_t *__restrict, const wchar_t *__restrict);
/* wcsncat's d is required (`d += wcslen(d);` dereferences it via
 * wcslen() unconditionally, first statement); s is required the same
 * n == 0 convention as strncat's s above. */
wchar_t *wcsncat (wchar_t *__restrict, const wchar_t *__restrict, size_t) __attribute__((nonnull(1, 2)));
/* wcscmp: `*l == *r && *l` evaluates both sides of `==` every time,
 * same as strcmp -- no short circuit that could skip either. */
int wcscmp (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
/* wcsncmp's loop condition (`n && *l == *r && *l`) short-circuits
 * everything on n, and unlike strncmp there is no unconditional
 * post-loop dereference to fall back on (`return n ? ... : 0;`) -- so
 * this is the same real "n == 0 means neither pointer is ever
 * touched" convention as mem*'s own family (matching glibc's real
 * wcsncmp nonnull(1, 2), not a genuine "may be invalid" reading). */
int wcsncmp (const wchar_t *, const wchar_t *, size_t) __attribute__((nonnull(1, 2), __pure__));
/* wcschr forwards straight into its own loop (`for (; *s && *s != c;
 * s++)`), dereferencing s at least once regardless of c; c is a
 * wchar_t value, not a pointer. Reads only. */
wchar_t *wcschr (const wchar_t *, wchar_t) __attribute__((nonnull(1), __pure__));
/* wcsrchr (src/string/wcsrchr.c) walks to the end via wcslen() and
 * scans backward -- reads only, same __pure__ reasoning as strrchr. */
wchar_t *wcsrchr (const wchar_t *, wchar_t) __attribute__((__pure__));
/* wcsstr requires n (`if (!n[0]) ...` dereferences it first) and h:
 * `h = wcschr(h, *n)` is reached whenever n[0] != 0, and wcschr always
 * dereferences its first argument once c is known nonzero. Reads
 * only. */
wchar_t *wcsstr (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
size_t wcslen (const wchar_t *) __attribute__((nonnull(1), __pure__));
wchar_t *wmemcpy (wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wmemmove (wchar_t *, const wchar_t *, size_t);
/* Same n == 0 escape as mem*'s own family (glibc: wmemset nonnull(1)). */
wchar_t *wmemset (wchar_t *, wchar_t, size_t) __attribute__((nonnull(1)));
/* wmemcmp/wmemchr (src/string/wmemcmp.c, wmemchr.c): reads only, same
 * __pure__ reasoning as memcmp/memchr in string.h. */
int wmemcmp (const wchar_t *, const wchar_t *, size_t) __attribute__((nonnull(1, 2), __pure__));
wchar_t *wmemchr (const wchar_t *, wchar_t, size_t) __attribute__((nonnull(1), __pure__));

/* wcsnlen (src/string/wcsnlen.c) is wmemchr(s, 0, n) plus arithmetic --
 * reads only, same __pure__ reasoning as strnlen (string.h). */
size_t wcsnlen (const wchar_t *, size_t) __attribute__((__pure__));
wchar_t *wcsdup (const wchar_t *) __NTLIBC_RETURNS_OWNERSHIP(malloc);
/* wcpcpy/wcpncpy dereference d/s the same unconditional way as
 * stpcpy/stpncpy above (their own header comment applies verbatim;
 * these two are a literal transliteration of those two). */
wchar_t *wcpcpy (wchar_t *__restrict, const wchar_t *__restrict) __attribute__((nonnull(1, 2)));
wchar_t *wcpncpy (wchar_t *__restrict, const wchar_t *__restrict, size_t) __attribute__((nonnull(1, 2)));
/* wcsspn/wcscspn's own loop condition (`*s && inset(set, *s)`) tests
 * s's CONTENT via `*s`, not set's nullness -- the short circuit that
 * skips inset(), and so set, when s is empty is the same "incidentally
 * unreached, not a documented convention" shape as strspn's own c[0]
 * escape above, not a real "set may be invalid" contract. Both s and
 * set are required (matching glibc's real wcsspn/wcscspn, both
 * nonnull(1, 2)); wcspbrk forwards straight into wcscspn(s, set) with
 * no check of its own, inheriting the same requirement on both. */
wchar_t *wcspbrk (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
size_t wcsspn (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
size_t wcscspn (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
/* wcstok's s is optional, the same "resume from *p" convention as
 * strtok_r's s (see string.h); sep and p are both required the same
 * way strtok_r's are. */
wchar_t *wcstok (wchar_t *__restrict, const wchar_t *__restrict, wchar_t **__restrict) __attribute__((nonnull(2, 3)));
/* wcscasecmp/wcsncasecmp mirror strcasecmp/strncasecmp's own reasoning
 * exactly (unconditional post-loop `return towlower(*l) -
 * towlower(*r);`, with wcsncasecmp's `if (!n) return 0;` the only real
 * escape). wcscasecmp_l/wcsncasecmp_l forward to them unconditionally,
 * ignoring their own locale_t (same ASCII-only C/POSIX reasoning as
 * strcasecmp_l above). */
int wcscasecmp (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
int wcsncasecmp (const wchar_t *, const wchar_t *, size_t) __attribute__((nonnull(1, 2), __pure__));
int wcscasecmp_l (const wchar_t *, const wchar_t *, locale_t) __attribute__((nonnull(1, 2), __pure__));
int wcsncasecmp_l (const wchar_t *, const wchar_t *, size_t, locale_t) __attribute__((nonnull(1, 2), __pure__));

wint_t btowc (int);
int wctob (wint_t);
int mbsinit (const mbstate_t *);
size_t mbrtowc (wchar_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
size_t wcrtomb (char *__restrict, wchar_t, mbstate_t *__restrict);
size_t mbrlen (const char *__restrict, size_t, mbstate_t *__restrict);
/* src is required in all four (src/stdlib/mbrtowc.c): each dereferences
 * `*src` unconditionally as its own very first statement, with no NULL
 * check -- every real call site in this tree (src/stdlib/mbtowc.c,
 * test/posix-wchar.c) always passes `&src`, a real on-stack local,
 * never NULL. The output buffer (ws/s, 1st parameter) and st (last
 * parameter) are both genuinely optional and left unmarked: `if (ws)
 * .../if (s) ...` throughout each body is a real, live "just count,
 * don't write" guard (POSIX's own documented convention for this
 * family), and `if (!st) st = &internal;` is a real, live "use my own
 * static state" guard, the same shape as setenv()/unsetenv()'s own name
 * check. */
size_t mbsrtowcs (wchar_t *__restrict, const char **__restrict, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));
size_t wcsrtombs (char *__restrict, const wchar_t **__restrict, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));
size_t mbsnrtowcs (wchar_t *__restrict, const char **__restrict, size_t, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));
size_t wcsnrtombs (char *__restrict, const wchar_t **__restrict, size_t, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));

/* Every FILE * in this whole wide family (src/stdio/wide.c) is
 * dereferenced unconditionally, the same "not the callee's job to
 * validate" contract as the byte-level stdio family (see
 * include/stdio.h's own comment). fgetws requires ws too (`*p = 0;`
 * unconditional on every path that is not the same n <= 0 escape
 * fgets' own s has); fputws requires ws the same way (`for (; *ws;
 * ws++)`, evaluated at least once). */
wint_t fgetwc (FILE *) __attribute__((nonnull(1)));
wchar_t *fgetws (wchar_t *__restrict, int, FILE *__restrict) __attribute__((nonnull(1, 3)));
wint_t fputwc (wchar_t, FILE *) __attribute__((nonnull(2)));
int fputws (const wchar_t *__restrict, FILE *__restrict) __attribute__((nonnull(1, 2)));
int fwide (FILE *, int) __attribute__((nonnull(1)));
wint_t getwc (FILE *) __attribute__((nonnull(1)));
wint_t getwchar (void);
wint_t putwc (wchar_t, FILE *) __attribute__((nonnull(2)));
wint_t putwchar (wchar_t);
/* ungetwc's f is dereferenced via `!f->readable`, a content check, not
 * a check of f's own nullness (same shape as ungetc above). */
wint_t ungetwc (wint_t, FILE *) __attribute__((nonnull(2)));
FILE *open_wmemstream (wchar_t **, size_t *);

/* fmt/f are required the same way as the byte printf family
 * (include/stdio.h's own comment); swprintf/vswprintf's s is required
 * too, unlike snprintf's -- see src/stdio/printf.c's vswprintf_impl()
 * comment for why swprintf has no "just measure" convention to make it
 * optional. */
int fwprintf (FILE *__restrict, const wchar_t *__restrict, ...) __attribute__((nonnull(1, 2)));
int swprintf (wchar_t *__restrict, size_t, const wchar_t *__restrict, ...) __attribute__((nonnull(1, 3)));
int wprintf (const wchar_t *__restrict, ...) __attribute__((nonnull(1)));
int vfwprintf (FILE *__restrict, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1, 2)));
int vswprintf (wchar_t *__restrict, size_t, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1, 3)));
int vwprintf (const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1)));
/* Same reasoning as the byte scanf family (include/stdio.h's own
 * comment): fmt/s required, f deliberately left unmarked. */
int fwscanf (FILE *__restrict, const wchar_t *__restrict, ...) __attribute__((nonnull(2)));
int swscanf (const wchar_t *__restrict, const wchar_t *__restrict, ...) __attribute__((nonnull(1, 2)));
int wscanf (const wchar_t *__restrict, ...) __attribute__((nonnull(1)));
int vfwscanf (FILE *__restrict, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(2)));
int vswscanf (const wchar_t *__restrict, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1, 2)));
int vwscanf (const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1)));

/* s/f are required: src/time/wcsftime.c's wcsftime() itself dereferences
 * *f unconditionally (`for (; *f; f++)`) and writes through s directly
 * (its own PUT_WC macro's `s[pos++] = ...`, plus the unconditional
 * `s[pos] = 0;` on the non-overflow return), with no NULL check on
 * either -- test/posix-wchar.c's own `wcsftime(buf, 0, W(""), &tm)`
 * confirms n == 0 is a real, live case, but s itself is always a real
 * buffer there, never NULL. tm is deliberately NOT marked: wcsftime()'s
 * own body only ever forwards it into strftime() (`strftime(out, sizeof
 * out, fmt, tm)`) without dereferencing it itself, and strftime() is
 * itself left unmarked for the same "forwarded, callee already owns the
 * contract" reason (see time.h's own clock_gettime()/ctime_r() comments)
 * -- do_strftime(), strftime()'s static internal helper, is the one that
 * actually requires tm. */
size_t wcsftime (wchar_t *__restrict, size_t, const wchar_t *__restrict, const struct tm *__restrict)
    __attribute__((nonnull(1, 3)));

/* wcscoll/wcscoll_l (src/string/wcscoll.c): one-line forwards to
 * wcscmp() (already __pure__ above), same "one fixed C/POSIX locale"
 * reasoning as strcoll (string.h). */
int wcscoll (const wchar_t *, const wchar_t *) __attribute__((__pure__));
int wcscoll_l (const wchar_t *, const wchar_t *, locale_t) __attribute__((__pure__));
size_t wcsxfrm (wchar_t *__restrict, const wchar_t *__restrict, size_t);
size_t wcsxfrm_l (wchar_t *__restrict, const wchar_t *__restrict, size_t, locale_t);

double wcstod (const wchar_t *__restrict, wchar_t **__restrict);
float wcstof (const wchar_t *__restrict, wchar_t **__restrict);
long double wcstold (const wchar_t *__restrict, wchar_t **__restrict);
long wcstol (const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long wcstoul (const wchar_t *__restrict, wchar_t **__restrict, int);
long long wcstoll (const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long long wcstoull (const wchar_t *__restrict, wchar_t **__restrict, int);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
