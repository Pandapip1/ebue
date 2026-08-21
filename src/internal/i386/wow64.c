/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Whether this i386 process is running under WOW64 -- as a 32-bit process
 * on a 64-bit kernel -- rather than natively on 32-bit Windows.
 *
 * TEB.WOW32Reserved (already declared in nt.h, right after the fields
 * __teb() itself depends on) holds the address of the CPU-simulation
 * layer's 32-to-64 transition thunk -- what ntdll calls Wow64Transition --
 * for a thread running under WOW64, and is NULL for a thread on a native
 * 32-bit kernel where there is no such layer to transition through.  That
 * makes it exactly the flag fork.c needs to decide whether the WOW64
 * clone repair applies. */
#include "libc.h"

int __is_wow64(void)
{
	return __teb()->WOW32Reserved != 0;
}
