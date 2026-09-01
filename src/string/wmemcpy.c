/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <wchar.h>
#include <string.h>

wchar_t *wmemcpy(
	wchar_t *__restrict d withtok(writable_elements(n)),
	const wchar_t *__restrict s withtok(readable_elements(n)), size_t n)
{
	return memcpy(d, s, n * sizeof(wchar_t));
}

// NOLINTEND(misc-include-cleaner)
