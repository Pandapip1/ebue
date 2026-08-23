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

/* Only shortcut to the builtin on a compiler that actually has it.
 *
 * tcc does not: it has no __builtin_alloca, so an unguarded #define turned
 * every alloca() call into `unresolved reference to '__builtin_alloca'` at
 * link time -- and defeated the very thing each arch's src/alloca.S was written
 * for, since that file exists precisely so tcc can call alloca as an
 * ordinary cdecl function (see its header comment).
 *
 * tcc defines __has_builtin(x) as 0 for everything, so the __has_builtin
 * form below is the whole test on its own; the __GNUC__ arm is only for
 * compilers old enough to predate __has_builtin. Without any of them the
 * declaration above stands and the call goes to alloca.S. */
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
