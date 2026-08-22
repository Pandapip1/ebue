/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#define PAGESIZE 4096
#define LONG_BIT 32
#define LONG_MAX  0x7fffffffL
#define LLONG_MAX  0x7fffffffffffffffLL

/* ssize_t is `int` here (_Addr in arch/i386/bits/alltypes.h.in), so its
 * max coincides numerically with LONG_MAX but is a distinct type. */
#define SSIZE_MAX 0x7fffffff
