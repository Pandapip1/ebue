/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <strings.h>
#include <string.h>

withtok(null_terminated)
char *index(const char *s withtok(null_terminated), int c)
{
	return strchr(s, c);
}
