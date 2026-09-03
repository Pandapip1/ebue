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
#include <stdlib.h>

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

/* This family mirrors string.h's mem/str family one for one; the same
 * nonnull/pure reasoning applies. wcscat/wcsrchr/wcsnlen/wcsdup/
 * wmemcpy/wmemmove are left unmarked, not independently verified. */
wchar_t *wcscpy (wchar_t *__restrict, const wchar_t *__restrict) __attribute__((nonnull(1, 2)));
wchar_t *wcsncpy (wchar_t *__restrict, const wchar_t *__restrict, size_t) __attribute__((nonnull(1, 2)));
wchar_t *wcscat (wchar_t *__restrict, const wchar_t *__restrict);
wchar_t *wcsncat (wchar_t *__restrict, const wchar_t *__restrict, size_t) __attribute__((nonnull(1, 2)));
int wcscmp (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
int wcsncmp (const wchar_t *, const wchar_t *, size_t) __attribute__((nonnull(1, 2), __pure__));
wchar_t *wcschr (const wchar_t *, wchar_t) __attribute__((nonnull(1), __pure__));
wchar_t *wcsrchr (const wchar_t *, wchar_t) __attribute__((__pure__));
wchar_t *wcsstr (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
size_t wcslen (const wchar_t *) __attribute__((nonnull(1), __pure__));
wchar_t *wmemcpy (
	wchar_t *__restrict d withtok(writable_elements(n)),
	const wchar_t *__restrict s withtok(readable_elements(n)), size_t n);
wchar_t *wmemmove (wchar_t *d withtok(writable_elements(n)),
	const wchar_t *s withtok(readable_elements(n)), size_t n);
wchar_t *wmemset (wchar_t *, wchar_t, size_t) __attribute__((nonnull(1)));
int wmemcmp (const wchar_t *, const wchar_t *, size_t) __attribute__((nonnull(1, 2), __pure__));
wchar_t *wmemchr (const wchar_t *, wchar_t, size_t) __attribute__((nonnull(1), __pure__));

size_t wcsnlen (const wchar_t *, size_t) __attribute__((__pure__));
withtok(heap_allocated)
wchar_t *wcsdup (const wchar_t *);
wchar_t *wcpcpy (wchar_t *__restrict, const wchar_t *__restrict) __attribute__((nonnull(1, 2)));
wchar_t *wcpncpy (wchar_t *__restrict, const wchar_t *__restrict, size_t) __attribute__((nonnull(1, 2)));
wchar_t *wcspbrk (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
size_t wcsspn (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
size_t wcscspn (const wchar_t *, const wchar_t *) __attribute__((nonnull(1, 2), __pure__));
/* s is optional: same "resume from *p" convention as strtok_r's s
 * (string.h). */
wchar_t *wcstok (wchar_t *__restrict, const wchar_t *__restrict, wchar_t **__restrict) __attribute__((nonnull(2, 3)));
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
/* Output buffer (ws/s) and st are both optional: "just count, don't
 * write" and "use my own static state" are real, live guards in each
 * body, not oversights. */
size_t mbsrtowcs (wchar_t *__restrict, const char **__restrict, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));
size_t wcsrtombs (char *__restrict, const wchar_t **__restrict, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));
size_t mbsnrtowcs (wchar_t *__restrict, const char **__restrict, size_t, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));
size_t wcsnrtombs (char *__restrict, const wchar_t **__restrict, size_t, size_t, mbstate_t *__restrict)
    __attribute__((nonnull(2)));

/* Every FILE * in this wide family is dereferenced unconditionally, same
 * as the byte-level stdio family (see include/stdio.h). */
wint_t fgetwc (FILE *) __attribute__((nonnull(1)));
wchar_t *fgetws (wchar_t *__restrict, int, FILE *__restrict) __attribute__((nonnull(1, 3)));
wint_t fputwc (wchar_t, FILE *) __attribute__((nonnull(2)));
int fputws (const wchar_t *__restrict, FILE *__restrict) __attribute__((nonnull(1, 2)));
int fwide (FILE *, int) __attribute__((nonnull(1)));
wint_t getwc (FILE *) __attribute__((nonnull(1)));
wint_t getwchar (void);
wint_t putwc (wchar_t, FILE *) __attribute__((nonnull(2)));
wint_t putwchar (wchar_t);
wint_t ungetwc (wint_t, FILE *) __attribute__((nonnull(2)));
FILE *open_wmemstream (wchar_t **, size_t *);

/* fmt/f required as in the byte printf family (include/stdio.h);
 * swprintf/vswprintf's s is also required, unlike snprintf's -- no
 * "just measure" convention here. */
int fwprintf (FILE *__restrict, const wchar_t *__restrict, ...) __attribute__((nonnull(1, 2)));
int swprintf (wchar_t *__restrict, size_t, const wchar_t *__restrict, ...) __attribute__((nonnull(1, 3)));
int wprintf (const wchar_t *__restrict, ...) __attribute__((nonnull(1)));
int vfwprintf (FILE *__restrict, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1, 2)));
int vswprintf (wchar_t *__restrict, size_t, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1, 3)));
int vwprintf (const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1)));
/* Same as the byte scanf family (include/stdio.h): fmt/s required, f
 * deliberately left unmarked. */
int fwscanf (FILE *__restrict, const wchar_t *__restrict, ...) __attribute__((nonnull(2)));
int swscanf (const wchar_t *__restrict, const wchar_t *__restrict, ...) __attribute__((nonnull(1, 2)));
int wscanf (const wchar_t *__restrict, ...) __attribute__((nonnull(1)));
int vfwscanf (FILE *__restrict, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(2)));
int vswscanf (const wchar_t *__restrict, const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1, 2)));
int vwscanf (const wchar_t *__restrict, __isoc_va_list) __attribute__((nonnull(1)));

/* tm is deliberately NOT marked: wcsftime() only forwards it into
 * strftime(), which is itself left unmarked for the same reason (see
 * time.h). */
size_t wcsftime (wchar_t *__restrict, size_t, const wchar_t *__restrict, const struct tm *__restrict)
    __attribute__((nonnull(1, 3)));

/* Forward to wcscmp(); pure for the same one-locale reason as strcoll
 * (string.h). */
int wcscoll (const wchar_t *, const wchar_t *) __attribute__((__pure__));
int wcscoll_l (const wchar_t *, const wchar_t *, locale_t) __attribute__((__pure__));
size_t wcsxfrm (wchar_t *__restrict, const wchar_t *__restrict, size_t);
size_t wcsxfrm_l (wchar_t *__restrict, const wchar_t *__restrict, size_t, locale_t);

/* Real terminal column widths over Unicode East Asian Width and
 * combining-mark data (src/internal/unicode_tables.c). Returns 0 for a
 * null wide character, -1 for a non-printable one. */
int wcwidth (wchar_t) __attribute__((__pure__));
int wcswidth (const wchar_t *, size_t) __attribute__((nonnull(1), __pure__));

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
