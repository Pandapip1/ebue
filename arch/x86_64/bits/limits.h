/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#define PAGESIZE 4096
#define LONG_BIT 32
#define LONG_MAX  0x7fffffffL
#define LLONG_MAX  0x7fffffffffffffffLL

/* ssize_t is `long long` here (_Addr in arch/x86_64/bits/alltypes.h.in
 * is 64-bit even though `long` stays 32-bit under this target's LLP64
 * model), so SSIZE_MAX must NOT be LONG_MAX -- that would silently cap
 * it at 2^31-1 despite ssize_t actually holding 64 bits. */
#define SSIZE_MAX 0x7fffffffffffffffLL
