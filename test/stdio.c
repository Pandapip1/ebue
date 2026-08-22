/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Run headless under Wine: WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d wine stdio.exe </dev/null
 * (a crashing check would otherwise block on Wine's crash dialog). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

extern char **environ;
int __spawn(const char *path, char *const argv[], char *const envp[]);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* snprintf into a fixed buffer and compare both the text and the return
 * value against what C requires. */
static char sbuf[256];
#define FMT(expect, ...) do { \
	int r_ = snprintf(sbuf, sizeof sbuf, __VA_ARGS__); \
	if (strcmp(sbuf, expect) != 0 || r_ != (int)strlen(expect)) { \
		fails++; \
		printf("FAIL %s:%d: snprintf(%s) -> \"%s\" (%d), want \"%s\"\n", \
		       __FILE__, __LINE__, #__VA_ARGS__, sbuf, r_, expect); \
	} \
} while (0)

/* A temp file in the current directory, the way test/stdlib.c does it. */
static char *make_tmp(const char *tmpl)
{
	char *t = strdup(tmpl);
	int fd = mkstemp(t);
	if (fd < 0) { free(t); return 0; }
	close(fd);
	return t;
}

static void test_printf(void)
{
	int n;

	/* integers */
	FMT("0", "%d", 0);
	FMT("42", "%d", 42);
	FMT("-42", "%i", -42);
	FMT("4294967295", "%u", 4294967295u);
	FMT("ff", "%x", 255);
	FMT("FF", "%X", 255);
	FMT("17", "%o", 15);
	FMT("-2147483648", "%d", INT_MIN);
	FMT("2147483647", "%d", INT_MAX);
	FMT("-9223372036854775808", "%lld", LLONG_MIN);
	FMT("9223372036854775807", "%lld", LLONG_MAX);
	FMT("18446744073709551615", "%llu", ULLONG_MAX);
	FMT("ffffffffffffffff", "%llx", ULLONG_MAX);
	FMT("-123456789", "%ld", -123456789L);
	FMT("123456789", "%lu", 123456789UL);
	FMT("12345", "%zu", (size_t)12345);
	FMT("-5", "%zd", (ssize_t)-5);
	FMT("-56", "%hhd", 200);
	FMT("200", "%hhu", 200);
	FMT("-1", "%hd", 65535);
	FMT("65535", "%hu", 65535);
	FMT("99", "%jd", (long long)99);
	FMT("-7", "%td", (long)-7);

	/* flags, width, precision */
	FMT("   42", "%5d", 42);
	FMT("42   ", "%-5d", 42);
	FMT("00042", "%05d", 42);
	FMT("-0042", "%05d", -42);
	FMT("+42", "%+d", 42);
	FMT("-42", "%+d", -42);
	FMT(" 42", "% d", 42);
	FMT("-42", "% d", -42);
	FMT("  +42", "%+5d", 42);
	FMT("+0042", "%+05d", 42);
	FMT("042", "%.3d", 42);
	FMT("  042", "%5.3d", 42);
	FMT("042  ", "%-5.3d", 42);
	FMT("  042", "%05.3d", 42);   /* 0 flag ignored with precision */
	FMT("", "%.0d", 0);
	FMT("   ", "%3.0d", 0);
	FMT("0xff", "%#x", 255);
	FMT("0XFF", "%#X", 255);
	FMT("0", "%#x", 0);
	FMT("017", "%#o", 15);
	FMT("0", "%#o", 0);
	FMT("0x00ff", "%#06x", 255);
	FMT("  0xff", "%#6x", 255);
	FMT("0xff  ", "%-#6x", 255);
	FMT("   42", "%*d", 5, 42);
	FMT("42   ", "%*d", -5, 42);
	FMT("42   ", "%-*d", 5, 42);
	FMT("  042", "%*.*d", 5, 3, 42);
	FMT("42", "%.*d", -1, 42);
	FMT("1234567", "%3d", 1234567);
	FMT("12345678901234567890", "%5llu", 12345678901234567890ull);

	/* chars and strings */
	FMT("a", "%c", 'a');
	FMT("  a", "%3c", 'a');
	FMT("a  ", "%-3c", 'a');
	FMT("hello", "%s", "hello");
	FMT("   hello", "%8s", "hello");
	FMT("hello   ", "%-8s", "hello");
	FMT("hel", "%.3s", "hello");
	FMT("hello", "%.10s", "hello");
	FMT("  hel", "%5.3s", "hello");
	FMT("he", "%.*s", 2, "hello");
	FMT("hello", "%.*s", -1, "hello");
	FMT("", "%.0s", "hello");
	FMT("", "%s", "");
	FMT("(null)", "%s", (char *)0);
	FMT("abc", "%s%s%s", "a", "b", "c");
	FMT("%", "%%");
	FMT("100%", "%d%%", 100);
	FMT("a%b", "%c%%%c", 'a', 'b');
	FMT("plain text", "plain text");
	FMT("", "");

	/* pointers */
	FMT("(nil)", "%p", (void *)0);
	FMT("0x1234", "%p", (void *)0x1234);
	FMT("0xdeadbeef", "%p", (void *)0xdeadbeefUL);
	FMT("    0x10", "%8p", (void *)0x10);
	FMT("0x10    ", "%-8p", (void *)0x10);

	/* %n */
	n = -1;
	FMT("abc", "abc%n", &n);
	CHECK(n == 3);
	{
		long ln = -1; short sn = -1; signed char cn = -1;
		snprintf(sbuf, sizeof sbuf, "12345%ln", &ln);
		CHECK(ln == 5);
		snprintf(sbuf, sizeof sbuf, "ab%hn", &sn);
		CHECK(sn == 2);
		snprintf(sbuf, sizeof sbuf, "%hhn", &cn);
		CHECK(cn == 0);
	}
	/* %n counts logical (untruncated) output */
	n = -1;
	CHECK(snprintf(sbuf, 3, "hello%n", &n) == 5);
	CHECK(n == 5);

	/* truncation and return values */
	memset(sbuf, 'x', sizeof sbuf);
	CHECK(snprintf(sbuf, 4, "hello") == 5);
	CHECK(strcmp(sbuf, "hel") == 0);
	memset(sbuf, 'x', sizeof sbuf);
	CHECK(snprintf(sbuf, 1, "hello") == 5);
	CHECK(sbuf[0] == 0 && sbuf[1] == 'x');
	memset(sbuf, 'x', sizeof sbuf);
	CHECK(snprintf(sbuf, 0, "hello") == 5);
	CHECK(sbuf[0] == 'x');
	CHECK(snprintf(0, 0, "hello %d", 123) == 9);
	CHECK(snprintf(0, 0, "") == 0);
	memset(sbuf, 'x', sizeof sbuf);
	CHECK(snprintf(sbuf, 6, "hello") == 5);
	CHECK(strcmp(sbuf, "hello") == 0 && sbuf[6] == 'x');
	memset(sbuf, 'x', sizeof sbuf);
	CHECK(snprintf(sbuf, 5, "%d", 123456) == 6);
	CHECK(strcmp(sbuf, "1234") == 0);
	memset(sbuf, 'x', sizeof sbuf);
	CHECK(snprintf(sbuf, 3, "%10s", "ab") == 10);
	CHECK(strcmp(sbuf, "  ") == 0);

	/* sprintf */
	memset(sbuf, 'x', sizeof sbuf);
	CHECK(sprintf(sbuf, "%d-%s", 7, "seven") == 7);
	CHECK(strcmp(sbuf, "7-seven") == 0);
	CHECK(sprintf(sbuf, "") == 0 && sbuf[0] == 0);

	/* long output through a memory FILE: larger than the 16-byte pad chunk */
	CHECK(snprintf(sbuf, sizeof sbuf, "%100d", 1) == 100);
	CHECK(strlen(sbuf) == 100 && sbuf[99] == '1' && sbuf[0] == ' ' && sbuf[98] == ' ');
	CHECK(snprintf(sbuf, sizeof sbuf, "%-100s|", "a") == 101);
	CHECK(sbuf[0] == 'a' && sbuf[99] == ' ' && sbuf[100] == '|');
	CHECK(snprintf(sbuf, sizeof sbuf, "%0100d", 1) == 100);
	CHECK(sbuf[0] == '0' && sbuf[98] == '0' && sbuf[99] == '1');

	/* floating point */
	FMT("0.000000", "%f", 0.0);
	FMT("1.000000", "%f", 1.0);
	FMT("-1.000000", "%f", -1.0);
	FMT("3.14", "%.2f", 3.14159);
	FMT("3.1416", "%.4f", 3.14159);
	FMT("3", "%.0f", 3.14159);
	FMT("3.", "%#.0f", 3.14159);
	/* rounding that carries into a new leading place */
	FMT("100", "%.0f", 99.7);
	FMT("100", "%.0f", 99.8);
	FMT("100", "%.0f", 99.5);
	FMT("1000", "%.0f", 999.6);
	FMT("10", "%.0f", 9.5);
	FMT("10.0", "%.1f", 9.99);
	FMT("0.10", "%.2f", 0.0999);
	FMT("1000000000000000000000", "%.0f", 1e21);
	FMT("1.000e+06", "%.3e", 9.9999e5);
	FMT("1e+01", "%.0e", 9.5);
	/* exact ties round to even, as musl/glibc do */
	FMT("0", "%.0f", 0.5);
	FMT("2", "%.0f", 1.5);
	FMT("2", "%.0f", 2.5);
	FMT("0.001", "%.3f", 0.0005);
	FMT("0.001", "%.3f", 0.001);
	FMT("0.000", "%.3f", 0.0001);
	FMT("0.001", "%.3f", 0.0006);
	/* every digit of the value's exact expansion, not a 17-digit
	 * approximation with zeros after it */
	FMT("0.1000000000000000055511151231257827021181583404541015625", "%.55f", 0.1);
	FMT("0.20000000000000001110223024625156540423631668090820312500", "%.56f", 0.2);
	FMT("3.3333333333333331482961625624739099293947219848632812500e-01", "%.55e", 1.0 / 3.0);
	FMT("0.100000000000000005551115123125782702", "%.36g", 0.1);
	FMT("99999999999999991611392", "%.0f", 1e23);
	FMT("123456789.000000", "%f", 123456789.0);
	FMT("10000000000", "%.0f", 1e10);
	FMT("-2.5", "%.1f", -2.5);
	FMT("      3.14", "%10.2f", 3.14159);
	FMT("3.14      ", "%-10.2f", 3.14159);
	FMT("+3.14", "%+.2f", 3.14159);
	FMT(" 3.14", "% .2f", 3.14159);
	FMT("0000003.14", "%010.2f", 3.14159);
	FMT("-000003.14", "%010.2f", -3.14159);
	FMT("0.50", "%.2f", 0.5);
	FMT("12.30", "%.2f", 12.3);
	FMT("1.234568e+04", "%e", 12345.678);
	FMT("1.234568E+04", "%E", 12345.678);
	FMT("0.000000e+00", "%e", 0.0);
	FMT("1.5e+00", "%.1e", 1.5);
	FMT("1e+00", "%.0e", 1.0);
	FMT("-1.250000e-01", "%e", -0.125);
	FMT("1.000000e+100", "%e", 1e100);
	FMT("1.000000e-10", "%e", 1e-10);
	FMT("0.0001", "%g", 0.0001);
	FMT("1e-05", "%g", 0.00001);
	FMT("100000", "%g", 100000.0);
	FMT("1e+06", "%g", 1000000.0);
	FMT("0", "%g", 0.0);
	FMT("1", "%g", 1.0);
	FMT("0.5", "%g", 0.5);
	FMT("3.14159", "%g", 3.14159);
	FMT("3.1", "%.2g", 3.14159);
	FMT("123457", "%g", 123456.789);
	FMT("1.23457e+06", "%g", 1234567.89);
	FMT("1.23457E+06", "%G", 1234567.89);
	FMT("1.00000", "%#g", 1.0);
	FMT("-0.5", "%g", -0.5);
	FMT("inf", "%f", 1.0 / 0.0);
	FMT("-inf", "%f", -1.0 / 0.0);
	FMT("INF", "%F", 1.0 / 0.0);
	/* 0.0/0.0 is a negative NaN on x86, so a '-' may be printed */
	CHECK(snprintf(sbuf, sizeof sbuf, "%f", 0.0 / 0.0) >= 3 && strcmp(sbuf + (sbuf[0] == '-'), "nan") == 0);
	CHECK(snprintf(sbuf, sizeof sbuf, "%E", 0.0 / 0.0) >= 3 && strcmp(sbuf + (sbuf[0] == '-'), "NAN") == 0);
	CHECK(snprintf(sbuf, sizeof sbuf, "%08f", 0.0 / 0.0) == 8 && strchr(sbuf, '0') == 0);   /* no zero padding for nan */
	FMT("   inf", "%6f", 1.0 / 0.0);
	FMT("+inf", "%+f", 1.0 / 0.0);

	/* %a/%A: exact, since a double's significand is 13 hex digits */
	FMT("0x1p+0", "%a", 1.0);
	FMT("0x0p+0", "%a", 0.0);
	FMT("-0x0p+0", "%a", -0.0);
	FMT("0x1p-1", "%a", 0.5);
	FMT("0x1p+1", "%a", 2.0);
	FMT("0x1.8p+0", "%a", 1.5);
	FMT("0x1.fep+7", "%a", 255.0);
	FMT("0x1.921f9f01b866ep+1", "%a", 3.14159);
	FMT("0X1.921F9F01B866EP+1", "%A", 3.14159);
	FMT("0x1.fffffffffffffp+1023", "%a", DBL_MAX);
	FMT("0x1p-1022", "%a", DBL_MIN);
	FMT("0x0.0000000000001p-1022", "%a", 5e-324);   /* subnormal: leading 0 */
	FMT("inf", "%a", 1.0 / 0.0);
	FMT("-INF", "%A", -1.0 / 0.0);
	/* an explicit precision rounds to nearest, ties to even -- at
	 * precision 0 that is the leading digit's parity */
	FMT("0x1p+0", "%.0a", 1.0);
	FMT("0x2p+0", "%.0a", 1.5);
	FMT("0x1p+1", "%.0a", 2.5);
	FMT("0x2p+1", "%.0a", 3.0);
	FMT("0x1p+0", "%.0a", 1.0625);
	FMT("0x1.0p+0", "%.1a", 1.03125);
	FMT("0x1.2p+0", "%.1a", 1.09375);
	FMT("0x1.8p+0", "%.1a", 1.5);
	FMT("0x1.00p+0", "%.2a", 1.0);
	FMT("0x1.922p+1", "%.3a", 3.14159);
	FMT("0x1.921f9f01b867p+1", "%.12a", 3.14159);
	FMT("0x1.921f9f01b866e0p+1", "%.14a", 3.14159);
	FMT("0x1p-1022", "%.0a", 2.2250738585072009e-308);
	FMT("0x0p-1022", "%.0a", 5e-324);
	FMT("0x1.p+0", "%#.0a", 1.0);
	FMT("0x0.0000p+0", "%.4a", 0.0);
	FMT("+0x1p+0", "%+a", 1.0);
	FMT("              0x1p+0", "%20a", 1.0);
	FMT("0x1p+0              ", "%-20a", 1.0);
	FMT("0x000000000000001p+0", "%020a", 1.0);   /* the 0 pads after "0x" */
	FMT("-0x00000000000001p+0", "%020a", -1.0);
	FMT("       inf", "%010a", 1.0 / 0.0);       /* ... but never for inf */
	FMT("       inf", "%010f", 1.0 / 0.0);
	FMT("      -inf", "%010e", -1.0 / 0.0);

	/* mixed */
	FMT("x=1, y=-2, s=abc, c=Z, 50%", "x=%d, y=%d, s=%s, c=%c, %d%%", 1, -2, "abc", 'Z', 50);
}

/* Conversions far larger than any fixed buffer.  C99 7.19.6.1 puts no
 * bound on a precision, and DBL_MAX at "%f" alone runs to 316 bytes, so
 * none of these may be formatted through a buffer sized from a guess.
 * The lengths, return values and digits below are all what glibc
 * produces: this printf's expansion is exact, so every digit of a
 * value's terminating decimal expansion has to match, and only the
 * places past the end of that expansion are zeros. */
static char hbuf[4096], hexp[4096];

/* "<head><n zeros><tail>", the shape every case here has */
static char *zstr(const char *head, int n, const char *tail)
{
	char *p = hexp;
	strcpy(p, head); p += strlen(head);
	memset(p, '0', (size_t)n); p += n;
	strcpy(p, tail);
	return hexp;
}
#define HUGE_FMT(head, nz, tail, ...) do { \
	int r_ = snprintf(hbuf, sizeof hbuf, __VA_ARGS__); \
	const char *e_ = zstr(head, nz, tail); \
	if (strcmp(hbuf, e_) != 0 || r_ != (int)strlen(e_)) { \
		fails++; \
		printf("FAIL %s:%d: snprintf(%s) -> %d bytes \"%.32s...\", want %d\n", \
		       __FILE__, __LINE__, #__VA_ARGS__, r_, hbuf, (int)strlen(e_)); \
	} \
} while (0)

static int allof(const char *s, int n, char c)
{
	int i;
	for (i = 0; i < n; i++) if (s[i] != c) return 0;
	return 1;
}

/* length, return value and the two ends, for values whose middle digits
 * this printf does not carry */
static void huge_ends(int line, const char *what, int r, int len, const char *head, const char *tail)
{
	int l = (int)strlen(hbuf), tl = (int)strlen(tail);
	if (r != len || l != len || strncmp(hbuf, head, strlen(head)) != 0 ||
	    l < tl || strcmp(hbuf + l - tl, tail) != 0) {
		fails++;
		printf("FAIL %s:%d: snprintf(%s) -> %d/%d bytes \"%.32s...\", want %d\n",
		       __FILE__, line, what, r, l, hbuf, len);
	}
}
#define HUGE_ENDS(len, head, tail, ...) do { \
	int r_ = snprintf(hbuf, sizeof hbuf, __VA_ARGS__); \
	huge_ends(__LINE__, #__VA_ARGS__, r_, len, head, tail); \
} while (0)

static void test_printf_huge(void)
{
	char small[8];
	char *p;
	int dp, sign, r;

	/* DBL_MAX has 309 integer digits: "%f" of it is 316 bytes */
	HUGE_ENDS(316, "17976931348623", ".000000", "%f", DBL_MAX);
	HUGE_ENDS(317, "-17976931348623", ".000000", "%f", -DBL_MAX);
	HUGE_ENDS(309, "17976931348623", "4026184124858368", "%.0f", DBL_MAX);
	HUGE_ENDS(910, "17976931348623", "0000", "%.600f", DBL_MAX);
	HUGE_ENDS(330, "              1797693134", ".000000", "%330f", DBL_MAX);
	HUGE_ENDS(330, "+000000000000000000001797", "4026184124858368", "%+0330.0f", DBL_MAX);
	/* the short conversions of the same value still fit */
	FMT("1.797693e+308", "%e", DBL_MAX);
	FMT("1.79769e+308", "%g", DBL_MAX);

	/* a precision way past anything a double can carry: the tail is
	 * zeros, and the length is exactly what was asked for */
	HUGE_FMT("1.", 300, "", "%.300f", 1.0);
	HUGE_FMT("0.", 300, "", "%.300f", 0.0);
	HUGE_FMT("1.", 400, "e+00", "%.400e", 1.0);
	HUGE_FMT("0.", 400, "e+00", "%.400e", 0.0);
	HUGE_FMT("0.5", 399, "", "%.400f", 0.5);
	HUGE_FMT("1", 0, "", "%.400g", 1.0);          /* %g strips them all */
	HUGE_FMT("1.", 399, "", "%#.400g", 1.0);      /* ... unless '#' */
	HUGE_FMT("1.5", 999, "", "%.1000f", 1.5);
	HUGE_FMT("1.5", 999, "e+00", "%.1000e", 1.5);
	HUGE_FMT("1.5", 998, "", "%#.1000g", 1.5);
	HUGE_FMT("-0.", 1000, "", "%.1000f", -0.0);
	HUGE_FMT("0x1.", 400, "p+0", "%.400a", 1.0);
	HUGE_FMT("0x1.921f9f01b866e", 587, "p+1", "%.600a", 3.14159);
	/* either side of the point where the formatting switches to a
	 * streamed run of zeros */
	HUGE_FMT("1.5", 510, "", "%.511f", 1.5);
	HUGE_FMT("1.5", 511, "", "%.512f", 1.5);
	HUGE_FMT("1.5", 512, "", "%.513f", 1.5);
	HUGE_FMT("1.5", 513, "", "%.514f", 1.5);
	HUGE_FMT("1.5", 512, "e+00", "%.513e", 1.5);
	HUGE_FMT("1.5", 511, "", "%#.513g", 1.5);
	/* a runtime precision, including one that is no precision at all */
	HUGE_FMT("1.5", 699, "", "%.*f", 700, 1.5);
	HUGE_FMT("1.500000", 0, "", "%.*f", -3, 1.5);

	/* denormals: their leading zeros reach the 323rd place */
	HUGE_FMT("0.", 100, "", "%.100f", 5e-324);
	HUGE_ENDS(332, "0.00000000000000000000", "4940656", "%.330f", 5e-324);
	HUGE_ENDS(352, "0.00000000000000000000", "1246544176568793", "%.350f", 5e-324);
	HUGE_ENDS(337, "4.94065645841246", "e-324", "%.330e", 5e-324);
	HUGE_ENDS(332, "0.00000000000000000000", "8585072013830902", "%.330f", DBL_MIN);
	FMT("0", "%.0f", 5e-324);
	FMT("0.000000", "%f", DBL_MIN);

	/* width and a huge precision together, in each alignment */
	CHECK(snprintf(hbuf, sizeof hbuf, "%1200.1000f", 1.5) == 1200);
	CHECK(strlen(hbuf) == 1200 && allof(hbuf, 198, ' ') &&
	      !strncmp(hbuf + 198, "1.5", 3) && allof(hbuf + 201, 999, '0'));
	CHECK(snprintf(hbuf, sizeof hbuf, "%-1200.1000f", 1.5) == 1200);
	CHECK(strlen(hbuf) == 1200 && !strncmp(hbuf, "1.5", 3) &&
	      allof(hbuf + 3, 999, '0') && allof(hbuf + 1002, 198, ' '));
	CHECK(snprintf(hbuf, sizeof hbuf, "%+01200.1000f", 1.5) == 1200);
	CHECK(strlen(hbuf) == 1200 && hbuf[0] == '+' && allof(hbuf + 1, 197, '0') &&
	      !strncmp(hbuf + 198, "1.5", 3) && allof(hbuf + 201, 999, '0'));

	/* an integer precision is a minimum digit count, also unbounded */
	FMT("0000000000000000000000000000000000000001", "%.40d", 1);
	FMT("-0000000000000000000000000000000000000007", "%.40d", -7);
	FMT("0x00000000000000000000000000000000000000ff", "%#.40x", 255);
	FMT("0000000000000000000000000000000000000010", "%#.40o", 8);
	FMT("0000000000000000000018446744073709551615", "%.40llu", ULLONG_MAX);
	FMT("0000000000000000000000000000000000000003            ", "%-52.40d", 3);
	FMT("0", "%#.0o", 0);
	FMT("010", "%#o", 8);
	FMT("", "%.0d", 0);

	/* snprintf must truncate into the caller's buffer, not overrun it,
	 * and still report the length the whole conversion would have */
	memset(small, '@', sizeof small);
	r = snprintf(small, sizeof small, "%.400f", 1.0);
	CHECK(r == 402 && !strcmp(small, "1.00000"));
	r = snprintf(small, sizeof small, "%f", DBL_MAX);
	CHECK(r == 316 && !strcmp(small, "1797693"));
	r = snprintf(small, 1, "%f", DBL_MAX);
	CHECK(r == 316 && small[0] == 0);
	small[0] = '@';
	r = snprintf(small, 0, "%.300e", 1.0);
	CHECK(r == 306 && small[0] == '@');   /* nothing written at all */
	r = snprintf(small, sizeof small, "%.400a", 1.0);
	CHECK(r == 407 && !strcmp(small, "0x1.000"));

	/* ecvt/fcvt/gcvt run on the same formatter */
	p = fcvt(DBL_MAX, 5, &dp, &sign);
	CHECK(p && dp == 309 && sign == 0 && strlen(p) > 0 && !strchr(p, '.'));
	p = fcvt(-DBL_MAX, 5, &dp, &sign);
	CHECK(p && dp == 309 && sign == 1);
	p = fcvt(5e-324, 40, &dp, &sign);
	CHECK(p && sign == 0 && strlen(p) <= 60);
	p = ecvt(DBL_MAX, 17, &dp, &sign);
	CHECK(p && dp == 309 && sign == 0 && strlen(p) == 17);
	gcvt(1234.5678, 8, hbuf);
	CHECK(!strcmp(hbuf, "1234.5678"));
}

static void test_scanf(void)
{
	int a, b, c, r;
	unsigned u;
	long l;
	long long ll;
	unsigned long long ull;
	short s;
	signed char sc;
	char str[64], str2[64];
	char ch;
	float fl;
	double d;
	void *p;

	/* ints */
	a = b = 0;
	CHECK(sscanf("12 34", "%d %d", &a, &b) == 2);
	CHECK(a == 12 && b == 34);
	CHECK(sscanf("-5", "%d", &a) == 1 && a == -5);
	CHECK(sscanf("+7", "%d", &a) == 1 && a == 7);
	CHECK(sscanf("   42", "%d", &a) == 1 && a == 42);
	CHECK(sscanf("\t\n 42", "%d", &a) == 1 && a == 42);
	CHECK(sscanf("2147483647", "%d", &a) == 1 && a == INT_MAX);
	CHECK(sscanf("-2147483648", "%d", &a) == 1 && a == INT_MIN);
	CHECK(sscanf("4294967295", "%u", &u) == 1 && u == 4294967295u);
	CHECK(sscanf("-1", "%u", &u) == 1 && u == 4294967295u);
	CHECK(sscanf("9223372036854775807", "%lld", &ll) == 1 && ll == LLONG_MAX);
	CHECK(sscanf("-9223372036854775808", "%lld", &ll) == 1 && ll == LLONG_MIN);
	CHECK(sscanf("18446744073709551615", "%llu", &ull) == 1 && ull == ULLONG_MAX);
	CHECK(sscanf("-123", "%ld", &l) == 1 && l == -123);
	CHECK(sscanf("300", "%hd", &s) == 1 && s == 300);
	CHECK(sscanf("-3", "%hhd", &sc) == 1 && sc == -3);
	CHECK(sscanf("12abc", "%d", &a) == 1 && a == 12);
	CHECK(sscanf("12345", "%2d%3d", &a, &b) == 2 && a == 12 && b == 345);
	CHECK(sscanf("1,2,3", "%d,%d,%d", &a, &b, &c) == 3 && a == 1 && b == 2 && c == 3);
	CHECK(sscanf("1 2 3", "%d %*d %d", &a, &b) == 2 && a == 1 && b == 3);

	/* failures and partial matches */
	CHECK(sscanf("abc", "%d", &a) == 0);
	CHECK(sscanf("", "%d", &a) == EOF);
	CHECK(sscanf("   ", "%d", &a) == EOF);
	CHECK(sscanf("", "%s", str) == EOF);
	CHECK(sscanf("", "%c", &ch) == EOF);
	CHECK(sscanf("12 abc", "%d %d", &a, &b) == 1);
	CHECK(sscanf("12", "%d %d", &a, &b) == 1);
	a = 99;
	CHECK(sscanf("-", "%d", &a) == 0 && a == 99);
	CHECK(sscanf("1:2", "%d,%d", &a, &b) == 1 && a == 1);
	CHECK(sscanf("abc", "abc") == 0);
	CHECK(sscanf("abd", "abc%d", &a) == 0);
	CHECK(sscanf("abc 5", "abc %d", &a) == 1 && a == 5);
	CHECK(sscanf("100%", "%d%%", &a) == 1 && a == 100);
	CHECK(sscanf("100", "%d%%", &a) == 1 && a == 100);

	/* hex, octal, %i */
	CHECK(sscanf("ff", "%x", &u) == 1 && u == 255);
	CHECK(sscanf("FF", "%x", &u) == 1 && u == 255);
	CHECK(sscanf("0xff", "%x", &u) == 1 && u == 255);
	CHECK(sscanf("0XfF", "%X", &u) == 1 && u == 255);
	CHECK(sscanf("ffg", "%x", &u) == 1 && u == 255);
	CHECK(sscanf("17", "%o", &u) == 1 && u == 15);
	CHECK(sscanf("179", "%o", &u) == 1 && u == 15);
	CHECK(sscanf("0x1A", "%i", &a) == 1 && a == 26);
	CHECK(sscanf("017", "%i", &a) == 1 && a == 15);
	CHECK(sscanf("17", "%i", &a) == 1 && a == 17);
	CHECK(sscanf("0", "%i", &a) == 1 && a == 0);
	CHECK(sscanf("-0x10", "%i", &a) == 1 && a == -16);
	CHECK(sscanf("deadbeef", "%llx", &ull) == 1 && ull == 0xdeadbeefull);
	CHECK(sscanf("0x10", "%p", &p) == 1 && p == (void *)0x10);

	/* %s, %c */
	CHECK(sscanf("hello world", "%s %s", str, str2) == 2);
	CHECK(strcmp(str, "hello") == 0 && strcmp(str2, "world") == 0);
	CHECK(sscanf("   hello", "%s", str) == 1 && strcmp(str, "hello") == 0);
	CHECK(sscanf("hello", "%3s", str) == 1 && strcmp(str, "hel") == 0);
	CHECK(sscanf("hello", "%3s%s", str, str2) == 2 && strcmp(str, "hel") == 0 && strcmp(str2, "lo") == 0);
	CHECK(sscanf("x", "%c", &ch) == 1 && ch == 'x');
	CHECK(sscanf(" x", "%c", &ch) == 1 && ch == ' ');
	CHECK(sscanf(" x", " %c", &ch) == 1 && ch == 'x');
	memset(str, 0, sizeof str);
	CHECK(sscanf("abcdef", "%3c", str) == 1 && strcmp(str, "abc") == 0);
	CHECK(sscanf("a b", "%c %c", &ch, str) == 2 && ch == 'a' && str[0] == 'b');
	CHECK(sscanf("key=value", "%[^=]=%s", str, str2) == 2);
	CHECK(strcmp(str, "key") == 0 && strcmp(str2, "value") == 0);

	/* %[...] scansets */
	CHECK(sscanf("abc123", "%[a-z]%d", str, &a) == 2 && strcmp(str, "abc") == 0 && a == 123);
	CHECK(sscanf("abc123", "%[^0-9]", str) == 1 && strcmp(str, "abc") == 0);
	CHECK(sscanf("123abc", "%[a-z]", str) == 0);
	CHECK(sscanf("hello, world", "%[^,], %s", str, str2) == 2 && strcmp(str, "hello") == 0 && strcmp(str2, "world") == 0);
	CHECK(sscanf("aabbcc", "%[ab]", str) == 1 && strcmp(str, "aabb") == 0);
	CHECK(sscanf("a]b", "%[]a]", str) == 1 && strcmp(str, "a]") == 0);
	CHECK(sscanf("xyz", "%[^]]", str) == 1 && strcmp(str, "xyz") == 0);
	CHECK(sscanf("hello world", "%[a-z ]", str) == 1 && strcmp(str, "hello world") == 0);
	CHECK(sscanf("hello", "%2[a-z]", str) == 1 && strcmp(str, "he") == 0);
	CHECK(sscanf("a-b", "%[a-]", str) == 1 && strcmp(str, "a-") == 0);
	CHECK(sscanf("", "%[a-z]", str) == EOF);

	/* floats */
	CHECK(sscanf("3.5", "%f", &fl) == 1 && fl == 3.5f);
	CHECK(sscanf("-2.25", "%lf", &d) == 1 && d == -2.25);
	CHECK(sscanf("1e3", "%lf", &d) == 1 && d == 1000.0);
	CHECK(sscanf("1.5e-1x", "%lf", &d) == 1 && d == 0.15);
	CHECK(sscanf("  .5", "%lf", &d) == 1 && d == 0.5);
	CHECK(sscanf("7", "%lf", &d) == 1 && d == 7.0);
	CHECK(sscanf("1.5 2.5", "%lf %lf", &d, (double *)&sbuf) == 2);
	CHECK(sscanf("abc", "%lf", &d) == 0);
	CHECK(sscanf("1.5,2", "%lf,%d", &d, &a) == 2 && d == 1.5 && a == 2);
	CHECK(sscanf("inf", "%lf", &d) == 1 && d > 1e300);

	/* %n */
	a = -1;
	CHECK(sscanf("12345", "%d%n", &b, &a) == 1 && a == 5);
	a = -1;
	CHECK(sscanf("ab cd", "%s %s%n", str, str2, &a) == 2 && a == 5);
	/* %n does not count as an assignment */
	CHECK(sscanf("5", "%d%n", &a, &b) == 1);
	/* leading whitespace skipped by %d counts; pushed-back look-ahead does not */
	a = -1;
	CHECK(sscanf("  12 abc", "%d%n", &b, &a) == 1 && b == 12 && a == 4);
	a = -1;
	CHECK(sscanf("0x1fz", "%i%n", &b, &a) == 1 && b == 0x1f && a == 4);
	a = -1;
	CHECK(sscanf("1.5e+x", "%lf%n", &d, &a) == 1 && d == 1.5 && a == 3);
	/* %n at the very start, and after a literal match */
	a = -1;
	CHECK(sscanf("xyz", "%n", &a) == 0 && a == 0);
	a = -1;
	CHECK(sscanf("ab:cd", "ab:%n", &a) == 0 && a == 3);
	/* a failed conversion stops before %n: n is untouched */
	a = -1;
	CHECK(sscanf("abc", "%d%n", &b, &a) == 0 && a == -1);
	/* length modifiers */
	{
		signed char hh = -1; short h = -1; long l = -1; long long ll = -1;
		size_t z = (size_t)-1; ptrdiff_t t = -1; intmax_t j = -1;
		CHECK(sscanf("1234567", "%d%hhn", &b, &hh) == 1 && hh == 7);
		CHECK(sscanf("1234567", "%d%hn", &b, &h) == 1 && h == 7);
		CHECK(sscanf("1234567", "%d%ln", &b, &l) == 1 && l == 7);
		CHECK(sscanf("1234567", "%d%lln", &b, &ll) == 1 && ll == 7);
		CHECK(sscanf("1234567", "%d%zn", &b, &z) == 1 && z == 7);
		CHECK(sscanf("1234567", "%d%tn", &b, &t) == 1 && t == 7);
		CHECK(sscanf("1234567", "%d%jn", &b, &j) == 1 && j == 7);
	}

	/* vsscanf via sscanf already; a few more realistic lines */
	{
		int y, mo, dd, hh, mm;
		r = sscanf("2026-08-21 13:45", "%d-%d-%d %d:%d", &y, &mo, &dd, &hh, &mm);
		CHECK(r == 5 && y == 2026 && mo == 8 && dd == 21 && hh == 13 && mm == 45);
		r = sscanf("name: Bob age: 30", "name: %s age: %d", str, &a);
		CHECK(r == 2 && strcmp(str, "Bob") == 0 && a == 30);
		r = sscanf("0x1f 0777 99", "%i %i %i", &a, &b, &c);
		CHECK(r == 3 && a == 31 && b == 0777 && c == 99);
	}
}

static void test_file_io(void)
{
	char *name = make_tmp("stdiotest-XXXXXX");
	FILE *f;
	char buf[128];
	int i, c;
	fpos_t pos;
	static const char text[] = "line one\nline two\nline three\n";

	CHECK(name != 0);
	if (!name) return;

	/* fopen/fwrite/fputs/fputc/fclose */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		CHECK(fwrite("line ", 1, 5, f) == 5);
		CHECK(fputs("one\n", f) == 0);
		CHECK(fwrite("line two\n", 3, 3, f) == 3);
		CHECK(fputc('l', f) == 'l');
		CHECK(fputs("ine three\n", f) == 0);
		CHECK(fwrite("x", 0, 5, f) == 0);
		CHECK(fwrite("x", 5, 0, f) == 0);
		CHECK(ferror(f) == 0);
		CHECK(fflush(f) == 0);
		CHECK(fclose(f) == 0);
	}

	/* fopen-read/fread round trip */
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		size_t n;
		memset(buf, 0, sizeof buf);
		n = fread(buf, 1, sizeof buf, f);
		CHECK(n == strlen(text));
		CHECK(strcmp(buf, text) == 0);
		CHECK(feof(f));
		CHECK(!ferror(f));
		CHECK(fread(buf, 1, 1, f) == 0);
		CHECK(fgetc(f) == EOF);
		clearerr(f);
		CHECK(!feof(f));
		CHECK(fclose(f) == 0);
	}

	/* writing to a read-only stream fails with ferror set */
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		CHECK(fputc('x', f) == EOF);
		CHECK(ferror(f));
		CHECK(fwrite("x", 1, 1, f) == 0);
		clearerr(f);
		CHECK(!ferror(f));
		CHECK(fclose(f) == 0);
	}
	/* reading from a write-only stream fails */
	f = fopen(name, "a");
	CHECK(f != 0);
	if (f) {
		CHECK(fgetc(f) == EOF);
		CHECK(ferror(f));
		CHECK(fclose(f) == 0);
	}

	/* fgets, fgetc, ungetc, feof */
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "line one\n") == 0);
		CHECK(fgets(buf, 5, f) == buf);
		CHECK(strcmp(buf, "line") == 0);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, " two\n") == 0);
		c = fgetc(f);
		CHECK(c == 'l');
		CHECK(ungetc(c, f) == 'l');
		CHECK(fgetc(f) == 'l');
		CHECK(ungetc('Q', f) == 'Q');
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "Qine three\n") == 0);
		CHECK(!feof(f));
		CHECK(fgets(buf, sizeof buf, f) == 0);
		CHECK(feof(f));
		/* ungetc at EOF clears the EOF indicator */
		CHECK(ungetc('z', f) == 'z');
		CHECK(!feof(f));
		CHECK(fgetc(f) == 'z');
		CHECK(fgetc(f) == EOF);
		CHECK(feof(f));
		CHECK(ungetc(EOF, f) == EOF);
		CHECK(fgets(buf, 0, f) == 0);
		CHECK(fclose(f) == 0);
	}

	/* getc/getline/getdelim */
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		char *line = 0;
		size_t cap = 0;
		CHECK(getline(&line, &cap, f) == 9);
		CHECK(line && strcmp(line, "line one\n") == 0 && cap >= 10);
		CHECK(getdelim(&line, &cap, ' ', f) == 5);
		CHECK(strcmp(line, "line ") == 0);
		CHECK(getline(&line, &cap, f) == 4);
		CHECK(strcmp(line, "two\n") == 0);
		CHECK(getline(&line, &cap, f) == 11);
		CHECK(getline(&line, &cap, f) == -1);
		CHECK(feof(f));
		free(line);
		CHECK(fclose(f) == 0);
	}

	/* fseek/ftell/rewind/fgetpos/fsetpos on a read stream */
	f = fopen(name, "rb");
	CHECK(f != 0);
	if (f) {
		CHECK(ftell(f) == 0);
		CHECK(fgetc(f) == 'l');
		CHECK(ftell(f) == 1);
		CHECK(fseek(f, 5, SEEK_SET) == 0);
		CHECK(ftell(f) == 5);
		CHECK(fgetc(f) == 'o');
		CHECK(fseek(f, 2, SEEK_CUR) == 0);
		CHECK(ftell(f) == 8);
		CHECK(fgetc(f) == '\n');
		CHECK(fseek(f, -2, SEEK_CUR) == 0);
		CHECK(ftell(f) == 7);
		CHECK(fgetc(f) == 'e');
		CHECK(fseek(f, 0, SEEK_END) == 0);
		CHECK(ftell(f) == (long)strlen(text));
		CHECK(fgetc(f) == EOF);
		CHECK(feof(f));
		CHECK(fseek(f, -1, SEEK_END) == 0);
		CHECK(!feof(f));
		CHECK(fgetc(f) == '\n');
		rewind(f);
		CHECK(ftell(f) == 0);
		CHECK(fgetc(f) == 'l');
		CHECK(fgetpos(f, &pos) == 0);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "ine one\n") == 0);
		CHECK(fsetpos(f, &pos) == 0);
		CHECK(ftell(f) == 1);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "ine one\n") == 0);
		/* ungetc is accounted for by ftell and discarded by fseek */
		CHECK(ungetc('X', f) == 'X');
		CHECK(ftell(f) == 8);
		CHECK(fseek(f, 0, SEEK_CUR) == 0);
		CHECK(ftell(f) == 8);
		CHECK(fgetc(f) == '\n');
		CHECK(fseek(f, -1, SEEK_SET) != 0);
		CHECK(fclose(f) == 0);
	}

	/* ftell/fseek while writing */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		CHECK(ftell(f) == 0);
		CHECK(fputs("hello", f) == 0);
		CHECK(ftell(f) == 5);
		CHECK(fseek(f, 0, SEEK_CUR) == 0);
		CHECK(ftell(f) == 5);
		CHECK(fputs(" world", f) == 0);
		CHECK(ftell(f) == 11);
		CHECK(fseek(f, 0, SEEK_SET) == 0);
		CHECK(fputs("HELLO", f) == 0);
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "HELLO world") == 0);
		CHECK(fclose(f) == 0);
	}

	/* update mode: w+ then read back, r+ then overwrite, a append */
	f = fopen(name, "w+");
	CHECK(f != 0);
	if (f) {
		CHECK(fputs("0123456789", f) == 0);
		CHECK(ftell(f) == 10);
		CHECK(fseek(f, 0, SEEK_SET) == 0);
		CHECK(ftell(f) == 0);
		memset(buf, 0, sizeof buf);
		CHECK(fread(buf, 1, 4, f) == 4);
		CHECK(strcmp(buf, "0123") == 0);
		CHECK(ftell(f) == 4);
		/* switching read -> write seeks back over read-ahead */
		CHECK(fputs("ab", f) == 0);
		CHECK(fseek(f, 0, SEEK_SET) == 0);
		memset(buf, 0, sizeof buf);
		CHECK(fread(buf, 1, 10, f) == 10);
		CHECK(strcmp(buf, "0123ab6789") == 0);
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "r+");
	CHECK(f != 0);
	if (f) {
		CHECK(fgetc(f) == '0');
		CHECK(fseek(f, 5, SEEK_SET) == 0);
		CHECK(fputc('Z', f) == 'Z');
		CHECK(fflush(f) == 0);
		CHECK(ftell(f) == 6);
		CHECK(fgetc(f) == '6');
		rewind(f);
		memset(buf, 0, sizeof buf);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "0123aZ6789") == 0);
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "a");
	CHECK(f != 0);
	if (f) {
		CHECK(fputs("!!", f) == 0);
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "a+");
	CHECK(f != 0);
	if (f) {
		CHECK(fputs("?", f) == 0);
		CHECK(fflush(f) == 0);
		rewind(f);
		memset(buf, 0, sizeof buf);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "0123aZ6789!!?") == 0);
		CHECK(fclose(f) == 0);
	}

	/* fopen failure modes */
	errno = 0;
	CHECK(fopen("stdiotest-does-not-exist-XXXXXX.nope", "r") == 0);
	CHECK(errno == ENOENT);
	errno = 0;
	CHECK(fopen(name, "q") == 0);
	CHECK(errno == EINVAL);
	errno = 0;
	CHECK(fopen(name, "wx") == 0);
	CHECK(errno == EEXIST);

	/* binary data: every byte value, sizes larger than BUFSIZ, fread item
	 * size > 1 with a short tail */
	f = fopen(name, "wb");
	CHECK(f != 0);
	if (f) {
		static unsigned char big[3 * BUFSIZ + 7];
		for (i = 0; i < (int)sizeof big; i++) big[i] = (unsigned char)(i * 7 + 3);
		CHECK(fwrite(big, 1, sizeof big, f) == sizeof big);
		CHECK(fclose(f) == 0);
		f = fopen(name, "rb");
		CHECK(f != 0);
		if (f) {
			static unsigned char back[sizeof big];
			memset(back, 0, sizeof back);
			CHECK(fread(back, 4, sizeof back / 4, f) == sizeof back / 4);
			CHECK(memcmp(big, back, sizeof back - 3) == 0);
			CHECK(fread(back + sizeof back - 3, 1, 10, f) == 3);
			CHECK(memcmp(big, back, sizeof back) == 0);
			CHECK(feof(f));
			CHECK(fseek(f, 0, SEEK_END) == 0);
			CHECK(ftell(f) == (long)sizeof big);
			CHECK(fseek(f, BUFSIZ + 1, SEEK_SET) == 0);
			CHECK(fgetc(f) == big[BUFSIZ + 1]);
			/* byte-at-a-time over the whole thing */
			rewind(f);
			for (i = 0; i < (int)sizeof big; i++) if (fgetc(f) != big[i]) break;
			CHECK(i == (int)sizeof big);
			CHECK(fgetc(f) == EOF);
			CHECK(fclose(f) == 0);
		}
	}

	/* fprintf / fscanf through a real file */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		CHECK(fprintf(f, "%d %s %x\n", 42, "word", 255) == 11);   /* "42 word ff\n" */
		CHECK(fprintf(f, "%5.2f|%-4d|\n", 2.5, 7) == 12);       /* " 2.50|7   |\n" */
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		int a = 0, b = 0;
		double d = 0;
		memset(buf, 0, sizeof buf);
		CHECK(fscanf(f, "%d %s %x", &a, buf, &b) == 3);
		CHECK(a == 42 && strcmp(buf, "word") == 0 && b == 255);
		CHECK(fscanf(f, "%lf|%d |", &d, &a) == 2);   /* the %-4d left "7   " */
		CHECK(d == 2.5 && a == 7);
		CHECK(fscanf(f, "%d", &a) == EOF);
		CHECK(feof(f));
		CHECK(fclose(f) == 0);
	}

	/* setvbuf modes */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		static char ubuf[64];
		CHECK(setvbuf(f, ubuf, _IOFBF, sizeof ubuf) == 0);
		CHECK(fputs("buffered", f) == 0);
		CHECK(memcmp(ubuf, "buffered", 8) == 0);   /* still in the user buffer */
		CHECK(fwrite("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 1, 64, f) == 64);
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		memset(buf, 0, sizeof buf);
		CHECK(fread(buf, 1, sizeof buf, f) == 72);
		CHECK(memcmp(buf, "buffered0123456789abcdef", 24) == 0);
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		FILE *g;
		CHECK(setvbuf(f, 0, _IONBF, 0) == 0);
		CHECK(fputs("unbuf", f) == 0);
		/* unbuffered: already visible to another reader */
		g = fopen(name, "r");
		CHECK(g != 0);
		if (g) {
			memset(buf, 0, sizeof buf);
			CHECK(fread(buf, 1, sizeof buf, g) == 5);
			CHECK(strcmp(buf, "unbuf") == 0);
			CHECK(fclose(g) == 0);
		}
		CHECK(fputc('!', f) == '!');
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		FILE *g;
		CHECK(setvbuf(f, 0, _IOLBF, 0) == 0);
		CHECK(fputs("first\nsecond", f) == 0);
		/* line buffered: at least the complete line is out without a
		 * flush (the implementation may write the whole block that
		 * contained the newline, which the standard permits) */
		g = fopen(name, "r");
		CHECK(g != 0);
		if (g) {
			memset(buf, 0, sizeof buf);
			CHECK(fread(buf, 1, sizeof buf, g) >= 6);
			CHECK(strncmp(buf, "first\n", 6) == 0);
			CHECK(fclose(g) == 0);
		}
		CHECK(fflush(f) == 0);
		g = fopen(name, "r");
		CHECK(g != 0);
		if (g) {
			memset(buf, 0, sizeof buf);
			CHECK(fread(buf, 1, sizeof buf, g) == 12);
			CHECK(strcmp(buf, "first\nsecond") == 0);
			CHECK(fclose(g) == 0);
		}
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		setbuf(f, 0);
		CHECK(fputs("setbuf", f) == 0);
		CHECK(fclose(f) == 0);
	}
	f = fopen(name, "r");
	CHECK(f != 0);
	if (f) {
		static char small[4];
		CHECK(setvbuf(f, small, _IOFBF, sizeof small) == 0);
		memset(buf, 0, sizeof buf);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "setbuf") == 0);
		CHECK(fclose(f) == 0);
	}

	/* fflush(NULL) flushes every open stream */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		FILE *g;
		CHECK(fputs("flushall", f) == 0);
		CHECK(fflush(0) == 0);
		g = fopen(name, "r");
		CHECK(g != 0);
		if (g) {
			memset(buf, 0, sizeof buf);
			CHECK(fread(buf, 1, sizeof buf, g) == 8);
			CHECK(strcmp(buf, "flushall") == 0);
			CHECK(fclose(g) == 0);
		}
		CHECK(fclose(f) == 0);
	}

	/* freopen: same FILE, new file and mode */
	{
		char *name2 = make_tmp("stdiotest-XXXXXX");
		CHECK(name2 != 0);
		f = fopen(name, "w");
		CHECK(f != 0);
		if (f && name2) {
			FILE *g;
			CHECK(fputs("in first", f) == 0);
			g = freopen(name2, "w", f);
			CHECK(g == f);
			CHECK(fputs("in second", f) == 0);
			g = freopen(name2, "r", f);
			CHECK(g == f);
			memset(buf, 0, sizeof buf);
			CHECK(fgets(buf, sizeof buf, f) == buf);
			CHECK(strcmp(buf, "in second") == 0);
			CHECK(fclose(f) == 0);
			/* the first file was flushed before being closed */
			f = fopen(name, "r");
			CHECK(f != 0);
			if (f) {
				memset(buf, 0, sizeof buf);
				CHECK(fgets(buf, sizeof buf, f) == buf);
				CHECK(strcmp(buf, "in first") == 0);
				CHECK(fclose(f) == 0);
			}
			/* freopen of a missing file fails and the stream is gone */
			f = fopen(name2, "r");
			CHECK(f != 0);
			if (f) CHECK(freopen("stdiotest-does-not-exist-XXXXXX.nope", "r", f) == 0);
		}
		if (name2) { CHECK(remove(name2) == 0); free(name2); }
	}

	/* fileno / fdopen */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		int fd = fileno(f);
		CHECK(fd >= 0);
		CHECK(fputs("via fdopen\n", f) == 0);
		CHECK(fclose(f) == 0);
		fd = open(name, O_RDONLY);
		CHECK(fd >= 0);
		f = fdopen(fd, "r");
		CHECK(f != 0);
		if (f) {
			CHECK(fileno(f) == fd);
			memset(buf, 0, sizeof buf);
			CHECK(fgets(buf, sizeof buf, f) == buf);
			CHECK(strcmp(buf, "via fdopen\n") == 0);
			CHECK(fclose(f) == 0);
		}
	}

	/* rename / remove */
	{
		char *name2 = make_tmp("stdiotest-XXXXXX");
		CHECK(name2 != 0);
		if (name2) {
			CHECK(remove(name2) == 0);
			CHECK(fopen(name2, "r") == 0);
			CHECK(rename(name, name2) == 0);
			CHECK(fopen(name, "r") == 0);
			f = fopen(name2, "r");
			CHECK(f != 0);
			if (f) {
				memset(buf, 0, sizeof buf);
				CHECK(fgets(buf, sizeof buf, f) == buf);
				CHECK(strcmp(buf, "via fdopen\n") == 0);
				CHECK(fclose(f) == 0);
			}
			CHECK(rename(name2, name) == 0);
			CHECK(fopen(name2, "r") == 0);
			errno = 0;
			CHECK(rename("stdiotest-does-not-exist-XXXXXX.nope", name2) == -1);
			CHECK(errno == ENOENT);
			errno = 0;
			CHECK(remove("stdiotest-does-not-exist-XXXXXX.nope") == -1);
			CHECK(errno == ENOENT);
			free(name2);
		}
	}

	CHECK(remove(name) == 0);
	CHECK(fopen(name, "r") == 0);
	free(name);
}

static void test_tmpfile(void)
{
	FILE *f = tmpfile();
	char buf[64];

	CHECK(f != 0);
	if (f) {
		CHECK(fputs("temporary\n", f) == 0);
		CHECK(fprintf(f, "%d\n", 99) == 3);
		rewind(f);
		memset(buf, 0, sizeof buf);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "temporary\n") == 0);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "99\n") == 0);
		CHECK(fgets(buf, sizeof buf, f) == 0);
		CHECK(feof(f));
		CHECK(fclose(f) == 0);
	}

}

/* tmpnam: a usable, unique name that fits a char[L_tmpnam] however long
 * $TMP is (it once built "$TMP/tXXXXXX" and overflowed the caller's
 * buffer).  Runs in a spawned child (--tmpnam-child) so a regression
 * there cannot take the parent's other results with it; the parent
 * CHECKs the child's exit status. */
static int test_tmpnam_child(void)
{
	FILE *f;
	char nbuf[L_tmpnam];
	char *nm;

	nm = tmpnam(nbuf);
	CHECK(nm == nbuf);
	if (nm) {
		char other[L_tmpnam];
		CHECK(strlen(nm) > 0 && strlen(nm) < L_tmpnam);
		CHECK(tmpnam(other) == other);
		CHECK(strcmp(nm, other) != 0);
		f = fopen(nm, "w");
		CHECK(f != 0);
		if (f) { CHECK(fputs("x", f) == 0); CHECK(fclose(f) == 0); }
		CHECK(remove(nm) == 0);
		CHECK(remove(other) == 0);
	}
	nm = tmpnam(0);
	CHECK(nm != 0);
	if (nm) CHECK(remove(nm) == 0);
	fflush(stdout);
	return fails != 0;
}

static void test_tmpnam(const char *self)
{
	char *argv[3];
	int pid, status = -1;
	argv[0] = (char *)self;
	argv[1] = (char *)"--tmpnam-child";
	argv[2] = NULL;
	fflush(stdout);
	pid = __spawn(self, argv, environ);
	CHECK(pid > 0);
	if (pid <= 0) return;
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_mem_streams(void)
{
	FILE *f;
	char buf[32];
	char *out;
	size_t outsz;
	int a, b;

	/* fmemopen for reading */
	f = fmemopen((void *)"10 20 rest", 10, "r");
	CHECK(f != 0);
	if (f) {
		CHECK(fscanf(f, "%d %d", &a, &b) == 2 && a == 10 && b == 20);
		CHECK(fgetc(f) == ' ');
		CHECK(ftell(f) == 6);
		memset(buf, 0, sizeof buf);
		CHECK(fread(buf, 1, sizeof buf, f) == 4);
		CHECK(strcmp(buf, "rest") == 0);
		CHECK(feof(f));
		CHECK(fseek(f, 0, SEEK_SET) == 0);
		CHECK(!feof(f));
		CHECK(fgetc(f) == '1');
		CHECK(fseek(f, -2, SEEK_END) == 0);
		CHECK(fgetc(f) == 's');
		CHECK(fputc('x', f) == EOF);   /* read-only */
		CHECK(fileno(f) == -1);
		CHECK(fclose(f) == 0);
	}
	/* with an explicit short size */
	f = fmemopen((void *)"abcdef", 3, "r");
	CHECK(f != 0);
	if (f) {
		memset(buf, 0, sizeof buf);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "abc") == 0);
		CHECK(fclose(f) == 0);
	}

	/* fmemopen for writing: truncates at the buffer and NUL-terminates */
	memset(buf, 'x', sizeof buf);
	f = fmemopen(buf, 8, "w");
	CHECK(f != 0);
	if (f) {
		CHECK(fprintf(f, "%d,%d", 12, 34) == 5);
		CHECK(fflush(f) == 0);
		CHECK(memcmp(buf, "12,34", 5) == 0 && buf[5] == 0);
		CHECK(ftell(f) == 5);
		fputs("56789", f);   /* only 3 fit; the flush at fclose reports the short write */
		CHECK(fclose(f) == EOF);
		CHECK(memcmp(buf, "12,34567", 8) == 0);
		CHECK(buf[8] == 'x');
	}
	/* w+: write then read back */
	memset(buf, 0, sizeof buf);
	f = fmemopen(buf, sizeof buf, "w+");
	CHECK(f != 0);
	if (f) {
		CHECK(fputs("hello memory", f) == 0);
		rewind(f);
		CHECK(fgetc(f) == 'h');
		memset(buf + 20, 0, 4);
		{
			char tmp[32];
			memset(tmp, 0, sizeof tmp);
			CHECK(fgets(tmp, sizeof tmp, f) == tmp);
			CHECK(strcmp(tmp, "ello memory") == 0);
		}
		CHECK(fclose(f) == 0);
		CHECK(strcmp(buf, "hello memory") == 0);
	}
	/* a: appends after the existing string */
	strcpy(buf, "abc");
	f = fmemopen(buf, 16, "a");
	CHECK(f != 0);
	if (f) {
		CHECK(ftell(f) == 3);
		CHECK(fputs("def", f) == 0);
		CHECK(fclose(f) == 0);
		CHECK(strcmp(buf, "abcdef") == 0);
	}
	/* NULL buffer: the library owns one */
	f = fmemopen(0, 16, "w+");
	CHECK(f != 0);
	if (f) {
		CHECK(fputs("owned", f) == 0);
		rewind(f);
		memset(buf, 0, sizeof buf);
		CHECK(fgets(buf, sizeof buf, f) == buf);
		CHECK(strcmp(buf, "owned") == 0);
		CHECK(fclose(f) == 0);
	}
	errno = 0;
	CHECK(fmemopen(buf, 0, "r") == 0);
	CHECK(errno == EINVAL);
	errno = 0;
	CHECK(fmemopen(buf, 4, "z") == 0);
	CHECK(errno == EINVAL);

	/* open_memstream */
	out = 0; outsz = 99;
	f = open_memstream(&out, &outsz);
	CHECK(f != 0);
	if (f) {
		int i;
		CHECK(out != 0 && outsz == 0);
		CHECK(fprintf(f, "n=%d", 5) == 3);
		CHECK(fflush(f) == 0);
		CHECK(outsz == 3 && strcmp(out, "n=5") == 0);
		/* grows well past the initial allocation */
		for (i = 0; i < 1000; i++) CHECK(fputs("0123456789", f) == 0);
		CHECK(ftell(f) == 10003);
		CHECK(fclose(f) == 0);
		CHECK(outsz == 10003);
		CHECK(out != 0 && strlen(out) == 10003);
		CHECK(memcmp(out, "n=50123456789", 13) == 0);
		CHECK(memcmp(out + 9993, "0123456789", 10) == 0);
		free(out);
	}
	CHECK(open_memstream(0, &outsz) == 0);
	CHECK(open_memstream(&out, 0) == 0);

	/* asprintf */
	out = 0;
	CHECK(asprintf(&out, "%s=%d", "x", 42) == 4);
	CHECK(out && strcmp(out, "x=42") == 0);
	free(out);
	CHECK(asprintf(&out, "") == 0);
	CHECK(out && out[0] == 0);
	free(out);
	CHECK(asprintf(&out, "%200d", 1) == 200);
	CHECK(out && strlen(out) == 200 && out[199] == '1');
	free(out);
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--tmpnam-child")) return test_tmpnam_child();

	test_printf();
	test_printf_huge();
	test_scanf();
	test_file_io();
	test_tmpfile();
	test_mem_streams();

	/* stdout itself works for printing and reports its bytes */
	CHECK(printf("") == 0);
	CHECK(fflush(stdout) == 0);
	CHECK(!ferror(stdout));

	/* last: the child may crash, so everything above is already reported */
	test_tmpnam(argv[0]);

	if (!fails) printf("stdio: all tests passed\n");
	return fails != 0;
}
