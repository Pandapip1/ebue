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

wchar_t *wcscpy (wchar_t *__restrict, const wchar_t *__restrict);
wchar_t *wcsncpy (wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wcscat (wchar_t *__restrict, const wchar_t *__restrict);
wchar_t *wcsncat (wchar_t *__restrict, const wchar_t *__restrict, size_t);
int wcscmp (const wchar_t *, const wchar_t *);
int wcsncmp (const wchar_t *, const wchar_t *, size_t);
wchar_t *wcschr (const wchar_t *, wchar_t);
wchar_t *wcsrchr (const wchar_t *, wchar_t);
wchar_t *wcsstr (const wchar_t *, const wchar_t *);
size_t wcslen (const wchar_t *);
wchar_t *wmemcpy (wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wmemmove (wchar_t *, const wchar_t *, size_t);
wchar_t *wmemset (wchar_t *, wchar_t, size_t);
int wmemcmp (const wchar_t *, const wchar_t *, size_t);
wchar_t *wmemchr (const wchar_t *, wchar_t, size_t);

size_t wcsnlen (const wchar_t *, size_t);
wchar_t *wcsdup (const wchar_t *);
wchar_t *wcpcpy (wchar_t *__restrict, const wchar_t *__restrict);
wchar_t *wcpncpy (wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wcspbrk (const wchar_t *, const wchar_t *);
size_t wcsspn (const wchar_t *, const wchar_t *);
size_t wcscspn (const wchar_t *, const wchar_t *);
wchar_t *wcstok (wchar_t *__restrict, const wchar_t *__restrict, wchar_t **__restrict);
int wcscasecmp (const wchar_t *, const wchar_t *);
int wcsncasecmp (const wchar_t *, const wchar_t *, size_t);
int wcscasecmp_l (const wchar_t *, const wchar_t *, locale_t);
int wcsncasecmp_l (const wchar_t *, const wchar_t *, size_t, locale_t);

wint_t btowc (int);
int wctob (wint_t);
int mbsinit (const mbstate_t *);
size_t mbrtowc (wchar_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
size_t wcrtomb (char *__restrict, wchar_t, mbstate_t *__restrict);
size_t mbrlen (const char *__restrict, size_t, mbstate_t *__restrict);
size_t mbsrtowcs (wchar_t *__restrict, const char **__restrict, size_t, mbstate_t *__restrict);
size_t wcsrtombs (char *__restrict, const wchar_t **__restrict, size_t, mbstate_t *__restrict);
size_t mbsnrtowcs (wchar_t *__restrict, const char **__restrict, size_t, size_t, mbstate_t *__restrict);
size_t wcsnrtombs (char *__restrict, const wchar_t **__restrict, size_t, size_t, mbstate_t *__restrict);

size_t wcsftime (wchar_t *__restrict, size_t, const wchar_t *__restrict, const struct tm *__restrict);

int wcscoll (const wchar_t *, const wchar_t *);
int wcscoll_l (const wchar_t *, const wchar_t *, locale_t);
size_t wcsxfrm (wchar_t *__restrict, const wchar_t *__restrict, size_t);
size_t wcsxfrm_l (wchar_t *__restrict, const wchar_t *__restrict, size_t, locale_t);

long wcstol (const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long wcstoul (const wchar_t *__restrict, wchar_t **__restrict, int);
long long wcstoll (const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long long wcstoull (const wchar_t *__restrict, wchar_t **__restrict, int);

#ifdef __cplusplus
}
#endif

#endif
