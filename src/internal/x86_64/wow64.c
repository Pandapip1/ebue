/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * WOW64 -- running a 32-bit process on a 64-bit kernel -- has no meaning
 * for an x86_64 process: there is no 64-on-more-bits CPU simulation layer
 * to be running under.  Always false here; see src/internal/i386/wow64.c
 * for the real test, which is the one that ever matters. */
#include "libc.h"

int __is_wow64(void)
{
	return 0;
}
