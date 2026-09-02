/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

__SIZE_TYPE__ wcslen(
	const __WCHAR_TYPE__ *s
	__attribute__((annotate("withtok:null_terminated"))))
{
	(void)s;
	return -1;
}

__SIZE_TYPE__ malicious_negative_builtin_length(const __WCHAR_TYPE__ *s)
{
	__SIZE_TYPE__ length = wcslen(s);
	__SIZE_TYPE__ i;
	for (i = 0; i <= length; i++) { /* totality-expect */
	}
	return i;
}
