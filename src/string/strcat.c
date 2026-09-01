/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <string.h>

char *strcat(char *__restrict dest, const char *__restrict src)
{
	/* strcat's specified interface supplies no destination extent; this
	 * implementation cannot add a bound without changing the public API. */
	// NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.strcpy)
	strcpy(dest + strlen(dest), src);
	return dest;
}

// NOLINTEND(misc-include-cleaner)
