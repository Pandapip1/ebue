/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdlib.h>
#include <signal.h>
#include "libc.h"

_Noreturn void abort(void)
{
	__raise_internal(SIGABRT);
	/* If a handler returned, or SIGABRT was ignored, die anyway -- with
	 * the status a Unix process killed by SIGABRT would have. */
	__nt_exit(128 + SIGABRT);
}
