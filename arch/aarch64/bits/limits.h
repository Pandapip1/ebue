/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#define PAGESIZE 4096
/* Linux/aarch64 is LP64, while Windows/aarch64 is LLP64.  Both use a
 * 64-bit address type, but Windows keeps C `long` at 32 bits. */
#ifdef _WIN32
#define LONG_BIT 32
#define LONG_MAX  0x7fffffffL
#else
#define LONG_BIT 64
#define LONG_MAX  0x7fffffffffffffffL
#endif
#define LLONG_MAX  0x7fffffffffffffffLL

#define SSIZE_MAX 0x7fffffffffffffffLL
