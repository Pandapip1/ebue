/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <inttypes.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int near(double a, double b, double rel)
{
	if (a == b) return 1;
	if (b == 0) return fabs(a) < rel;
	return fabs(a - b) <= rel * fabs(b);
}

int main(void)
{
	char *end;

	/* strtol basics */
	CHECK(strtol("123", 0, 10) == 123);
	CHECK(strtol("  -456xyz", &end, 10) == -456 && !strcmp(end, "xyz"));
	CHECK(strtol("0x1A", &end, 16) == 26 && *end == 0);
	CHECK(strtol("0x1A", &end, 0) == 26 && *end == 0);
	CHECK(strtol("017", 0, 0) == 15);
	CHECK(strtol("z", 0, 36) == 35);
	CHECK(strtol("101", 0, 2) == 5);
	CHECK(strtol("10", 0, 8) == 8);
	CHECK(strtol("+42", 0, 10) == 42);

	/* no digits: endptr = nptr */
	{ const char *s = "abc"; CHECK(strtol(s, &end, 10) == 0 && end == (char *)s); }
	{ const char *s = "0x"; CHECK(strtol(s, &end, 16) == 0 && end == (char *)s + 1); }
	{ const char *s = "  +"; CHECK(strtol(s, &end, 10) == 0 && end == (char *)s); }

	/* clamping: long is 32-bit on both our arches */
	CHECK(sizeof(long) == 4);
	errno = 0; CHECK(strtol("2147483648", 0, 10) == LONG_MAX && errno == ERANGE);
	errno = 0; CHECK(strtol("-2147483649", 0, 10) == LONG_MIN && errno == ERANGE);
	errno = 0; CHECK(strtol("2147483647", 0, 10) == LONG_MAX && errno == 0);
	errno = 0; CHECK(strtol("-2147483648", 0, 10) == LONG_MIN && errno == 0);
	errno = 0; CHECK(strtoul("4294967295", 0, 10) == ULONG_MAX && errno == 0);
	errno = 0; CHECK(strtoul("4294967296", 0, 10) == ULONG_MAX && errno == ERANGE);
	CHECK(strtoul("-1", 0, 10) == ULONG_MAX);
	errno = 0; CHECK(strtoll("9223372036854775807", 0, 10) == LLONG_MAX && errno == 0);
	errno = 0; CHECK(strtoll("9223372036854775808", 0, 10) == LLONG_MAX && errno == ERANGE);
	errno = 0; CHECK(strtoll("-9223372036854775808", 0, 10) == LLONG_MIN && errno == 0);
	errno = 0; CHECK(strtoll("-9223372036854775809", 0, 10) == LLONG_MIN && errno == ERANGE);
	errno = 0; CHECK(strtoull("18446744073709551615", 0, 10) == ULLONG_MAX && errno == 0);
	errno = 0; CHECK(strtoull("18446744073709551616", 0, 10) == ULLONG_MAX && errno == ERANGE);
	CHECK(strtoimax("-42", 0, 0) == -42);
	CHECK(strtoumax("0xff", 0, 0) == 255);

	/* atoi family */
	CHECK(atoi("-17") == -17);
	CHECK(atol("100000") == 100000L);
	CHECK(atoll("123456789012345") == 123456789012345LL);
	CHECK(atof("2.5") == 2.5);

	/* strtod */
	CHECK(strtod("0", &end) == 0.0 && *end == 0);
	CHECK(strtod("1.5", 0) == 1.5);
	CHECK(strtod("-3.25e2", 0) == -325.0);
	CHECK(strtod("  .5", 0) == 0.5);
	CHECK(strtod("5.", 0) == 5.0);
	CHECK(near(strtod("3.14159265358979", 0), 3.14159265358979, 1e-15));
	CHECK(near(strtod("1e100", 0), 1e100, 1e-15));
	CHECK(near(strtod("1e-100", 0), 1e-100, 1e-15));
	CHECK(near(strtod("2.2250738585072014e-308", 0), 2.2250738585072014e-308, 1e-15));
	CHECK(near(strtod("1.7976931348623157e308", 0), 1.7976931348623157e308, 1e-15));
	CHECK(near(strtod("123456789012345678901234567890", 0), 1.2345678901234568e29, 1e-14));

	/* hex floats are exact */
	CHECK(strtod("0x1.8p3", 0) == 12.0);
	CHECK(strtod("0x10", 0) == 16.0);
	CHECK(strtod("0x.8p1", 0) == 1.0);
	CHECK(strtod("0x1p-1074", 0) == 4.9406564584124654e-324);
	CHECK(strtod("-0x1.fffffffffffffp1023", 0) == -1.7976931348623157e308);

	/* inf/nan */
	CHECK(strtod("inf", &end) == HUGE_VAL && *end == 0);
	CHECK(strtod("-Infinity", &end) == -HUGE_VAL && *end == 0);
	CHECK(strtod("infx", &end) == HUGE_VAL && !strcmp(end, "x"));
	{ double d = strtod("nan", &end); CHECK(d != d && *end == 0); }
	{ double d = strtod("NaN(abc123)", &end); CHECK(d != d && *end == 0); }
	{ double d = strtod("nan(", &end); CHECK(d != d && !strcmp(end, "(")); }

	/* nothing parsed */
	{ const char *s = "xyz"; CHECK(strtod(s, &end) == 0 && end == (char *)s); }
	{ const char *s = "e5"; CHECK(strtod(s, &end) == 0 && end == (char *)s); }
	{ const char *s = "0x"; strtod(s, &end); CHECK(end == (char *)s + 1); }

	/* ERANGE */
	errno = 0; CHECK(strtod("1e400", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("-1e400", 0) == -HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("1e-400", 0) == 0 && errno == ERANGE);
	errno = 0; CHECK(strtof("1e39", 0) == HUGE_VALF && errno == ERANGE);
	errno = 0; CHECK(strtof("3.4e38", 0) < HUGE_VALF && errno == 0);

	/* strtof / strtold */
	CHECK(strtof("0.25", 0) == 0.25f);
	CHECK(strtold("0x1p100", 0) == 1267650600228229401496703205376.0L);
	CHECK(near((double)strtold("1e300", 0), 1e300, 1e-14));

	if (!fails) printf("strto: all tests passed\n");
	return fails != 0;
}
