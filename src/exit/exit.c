/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * exit and friends.  The process is ended with NtTerminateProcess rather
 * than RtlExitUserProcess: the latter runs the loader's shutdown, which
 * notifies every loaded DLL and, through kernel32, tells csrss -- none of
 * which a forked child (which has no csrss connection) can survive, and
 * none of which a program with no DLLs but ntdll needs.
 */
#include <stdlib.h>
#include <unistd.h>
#include "libc.h"

#define ATEXIT_MAX 128
static void (*handlers[ATEXIT_MAX])(void);
static int nhandlers;
static void (*qhandlers[32])(void);
static int nqhandlers;

int atexit(void (*f)(void))
{
	if (nhandlers >= ATEXIT_MAX) return -1;
	handlers[nhandlers++] = f;
	return 0;
}

int at_quick_exit(void (*f)(void))
{
	if (nqhandlers >= 32) return -1;
	qhandlers[nqhandlers++] = f;
	return 0;
}

void __funcs_on_exit(void)
{
	while (nhandlers > 0) handlers[--nhandlers]();
}

_Noreturn void __nt_exit(int code)
{
	NtTerminateProcess(NtCurrentProcess(), code);
	for (;;) NtTerminateProcess(NtCurrentProcess(), code);
}

_Noreturn void _Exit(int code) { __nt_exit(code); }
_Noreturn void _exit(int code) { __nt_exit(code); }

_Noreturn void exit(int code)
{
	__funcs_on_exit();
	__stdio_exit();
	__nt_exit(code);
}

_Noreturn void quick_exit(int code)
{
	while (nqhandlers > 0) qhandlers[--nqhandlers]();
	__nt_exit(code);
}
