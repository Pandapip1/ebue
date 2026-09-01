/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <string.h>

size_t strlcat(char *d, const char *s withtok(null_terminated), size_t n)
{
	size_t l = strnlen(d, n);
	if (l == n) return l + strlen(s);
	return l + strlcpy(d+l, s, n-l);
}
