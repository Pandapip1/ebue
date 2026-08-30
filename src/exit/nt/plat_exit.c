/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_exit.h -- see that header for
 * the contract.  Was, until this file existed, inline inside
 * src/exit/exit.c's __nt_exit(); nothing changed in substance, only
 * location.
 */
#include "libc.h"
#include "plat_exit.h"

_Noreturn void __plat_terminate(int code)
{
	NtTerminateProcess(NtCurrentProcess(), code);
	for (;;) NtTerminateProcess(NtCurrentProcess(), code);
}
