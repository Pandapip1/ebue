/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdlib.h>
#include "ownership_stubs.h"

withtok(heap_allocated)
withtok(null_terminated)
char *strdup(const char *s withtok(null_terminated))
{
	size_t l = strlen(s);
	char *d = malloc(l+1);
	if (!d) return 0;
	memcpy(d, s, l+1);
	__ownership_string_terminated(d);
	return d;
}
