/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _STDDEF_H
#define _STDDEF_H

#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif

#define __NEED_ptrdiff_t
#define __NEED_size_t
#define __NEED_wchar_t
#if __STDC_VERSION__ >= 201112L || defined(__cplusplus)
#define __NEED_max_align_t
#endif

#include <bits/alltypes.h>

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
