/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <string.h>

void *mempcpy(void *dest withtok(writable_span(n))
	withtok(disjoint_span(src, n)),
	const void *src withtok(readable_span(n)), size_t n)
{
	return (char *)memcpy(dest, src, n) + n;
}
