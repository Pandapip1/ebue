/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#define PAGESIZE 4096
/* Real Linux/aarch64 LP64: `long` is 64-bit (unlike arch/x86_64/bits,
 * which targets Windows x86_64's LLP64 `long` and keeps this at 32).
 * ssize_t is `long long` here too (see arch/aarch64/bits/alltypes.h.in),
 * so LONG_MAX and SSIZE_MAX end up numerically equal on this arch --
 * no LLP64-style mismatch between the two to guard against. */
#define LONG_BIT 64
#define LONG_MAX  0x7fffffffffffffffL
#define LLONG_MAX  0x7fffffffffffffffLL

#define SSIZE_MAX 0x7fffffffffffffffLL
