/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

_Noreturn void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
	fprintf(stderr, "Assertion failed: %s (%s: %s: %d)\n", expr, file, func, line);
	(void)fflush(0);
	abort();
}
