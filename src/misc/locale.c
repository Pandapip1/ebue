/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <locale.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

/* ntlibc supports exactly one locale, "C".  locale_t is an opaque
 * pointer; we hand out the address of one static object for it. */
struct __locale_struct { int dummy; };
static struct __locale_struct __c_locale;

char *setlocale(int cat, const char *name)
{
	if (cat < 0 || cat > LC_ALL) { errno = EINVAL; return 0; }
	if (!name) return (char *)"C";
	if (!*name || !strcmp(name, "C") || !strcmp(name, "POSIX"))
		return (char *)"C";
	/* setlocale(LC_ALL, "C;C;C;...") style composite names */
	if (cat == LC_ALL && !strncmp(name, "C;", 2)) return (char *)"C";
	return 0;
}

static struct lconv __posix_lconv = {
	.decimal_point = (char *)".",
	.thousands_sep = (char *)"",
	.grouping = (char *)"",
	.int_curr_symbol = (char *)"",
	.currency_symbol = (char *)"",
	.mon_decimal_point = (char *)"",
	.mon_thousands_sep = (char *)"",
	.mon_grouping = (char *)"",
	.positive_sign = (char *)"",
	.negative_sign = (char *)"",
	.int_frac_digits = 127,
	.frac_digits = 127,
	.p_cs_precedes = 127,
	.p_sep_by_space = 127,
	.n_cs_precedes = 127,
	.n_sep_by_space = 127,
	.p_sign_posn = 127,
	.n_sign_posn = 127,
	.int_p_cs_precedes = 127,
	.int_p_sep_by_space = 127,
	.int_n_cs_precedes = 127,
	.int_n_sep_by_space = 127,
	.int_p_sign_posn = 127,
	.int_n_sign_posn = 127,
};

struct lconv *localeconv(void)
{
	return &__posix_lconv;
}

locale_t newlocale(int mask, const char *name, locale_t base)
{
	(void)mask; (void)base;
	if (name && *name && strcmp(name, "C") && strcmp(name, "POSIX")) {
		errno = ENOENT;
		return 0;
	}
	return &__c_locale;
}

void freelocale(locale_t l)
{
	(void)l;
}

locale_t duplocale(locale_t l)
{
	(void)l;
	return &__c_locale;
}

locale_t uselocale(locale_t l)
{
	(void)l;
	return &__c_locale;
}
