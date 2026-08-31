/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <string.h>
#include <locale.h>

int strcoll(const char *l, const char *r)
{
	return strcmp(l, r);
}

int strcoll_l(const char *l, const char *r, locale_t loc)
{
	(void)loc;
	return strcmp(l, r);
}

// NOLINTEND(misc-include-cleaner)
