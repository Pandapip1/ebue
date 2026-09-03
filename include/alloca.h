/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _ALLOCA_H
#define _ALLOCA_H

#ifdef __cplusplus
extern "C" {
#endif

#define	__NEED_size_t
#include <bits/alltypes.h>

void *alloca(size_t);

/* tcc has no __builtin_alloca and reports __has_builtin(x) as 0 for
 * everything, so it falls through to the alloca.S implementation instead
 * of an unresolved reference. __GNUC__ only covers pre-__has_builtin
 * compilers. */
#if defined(__has_builtin)
# if __has_builtin(__builtin_alloca)
#  define alloca __builtin_alloca
# endif
#elif defined(__GNUC__)
# define alloca __builtin_alloca
#endif

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
