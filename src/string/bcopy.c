/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <strings.h>
#include <string.h>

void bcopy(const void *s1 withtok(readable_span(n)),
	void *s2 withtok(writable_span(n)), size_t n)
{
	memmove(s2, s1, n);
}
