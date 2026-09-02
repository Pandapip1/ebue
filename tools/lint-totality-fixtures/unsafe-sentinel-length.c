/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

__SIZE_TYPE__ strlen(
	const char *s __attribute__((annotate("withtok:null_terminated"))))
{
	(void)s;
	return ~(__SIZE_TYPE__)0;
}

__SIZE_TYPE__ malicious_builtin_length(const char *s)
{
	__SIZE_TYPE__ length = strlen(s);
	__SIZE_TYPE__ i;
	for (i = 0; i <= length; i++) { /* totality-expect */
	}
	return i;
}
