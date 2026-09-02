/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "plat_unistd.h"

int gethostname(char *name withtok(writable_span(len)), size_t len)
{
	/* This system's own hostname, however this backend actually knows
	 * it -- NT's COMPUTERNAME environment variable, or Linux's real
	 * uname(2) nodename; see __plat_hostname()'s own comment
	 * (src/internal/plat_unistd.h) for why the two backends differ here
	 * and why they still have to agree with uname()'s own nodename on
	 * whichever platform is running. 256 matches this library's own
	 * struct utsname field width (include/sys/utsname.h), comfortably
	 * above any real hostname either backend can produce. */
	char h[256];
	size_t n;
	__plat_hostname(h, sizeof h);
	n = strlen(h);
	if (n >= len) {
		if (len) memmove(name, h, len);
		return 0;
	}
	if (snprintf(name, len, "%s", h) != (int)n) return -1;
	return 0;
}

// NOLINTEND(misc-include-cleaner)
