/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include "libc.h"
#include "plat_unistd.h"

pid_t getpid(void)
{
	return (pid_t)(ULONG_PTR)__teb()->ClientId.UniqueProcess;
}

pid_t getppid(void)
{
	return __plat_getppid();
}

pid_t gettid(void)
{
	return (pid_t)(ULONG_PTR)__teb()->ClientId.UniqueThread;
}
