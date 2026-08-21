/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <getopt.h>
#include <libgen.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>

static int fails;
static void wr(const char *s) { const char *e = s; while (*e) e++; write(2, s, e - s); }
static void wrnum(int n) { char b[16]; int i = 15; b[i] = 0; if (n < 0) { wr("-"); n = -n; } do b[--i] = '0' + n % 10; while (n /= 10); wr(b + i); }
#define CHECK(x) do { if (!(x)) { fails++; wr(__FILE__ ":"); wrnum(__LINE__); wr(": FAIL: " #x "\n"); } } while (0)

static int streq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static void reset(void) { optind = 1; optreset = 1; opterr = 0; }

int main(void)
{
	char buf[64];
	int c, idx, flag;
	static const struct option lo[] = {
		{ "alpha", no_argument, 0, 'a' },
		{ "beta", required_argument, 0, 'b' },
		{ "gamma", optional_argument, 0, 'g' },
		{ "flag", no_argument, 0, 1 },
		{ 0, 0, 0, 0 }
	};
	char *av1[] = { "prog", "-a", "-bval", "-c", "arg", "rest", 0 };
	char *av2[] = { "prog", "-ab", "x", "--", "-c", 0 };
	char *av3[] = { "prog", "-x", "-b", 0 };
	char *av4[] = { "prog", "--alpha", "--beta=1", "--beta", "2", "--gam", "--gamma=3", "--fl", "pos", 0 };
	char *av5[] = { "prog", "pos1", "-a", "pos2", "--beta", "v", "pos3", 0 };
	char *av6[] = { "prog", "--bogus", "--beta", 0 };
	char *av7[] = { "prog", "-alpha", "-a", "-beta", "v", 0 };

	/* getopt */
	reset();
	c = getopt(6, av1, "ab:c:"); CHECK(c == 'a');
	c = getopt(6, av1, "ab:c:"); CHECK(c == 'b' && streq(optarg, "val"));
	c = getopt(6, av1, "ab:c:"); CHECK(c == 'c' && streq(optarg, "arg"));
	c = getopt(6, av1, "ab:c:"); CHECK(c == -1 && optind == 5);

	reset();
	c = getopt(5, av2, "ab:c:"); CHECK(c == 'a');
	c = getopt(5, av2, "ab:c:"); CHECK(c == 'b' && streq(optarg, "x"));
	c = getopt(5, av2, "ab:c:"); CHECK(c == -1 && optind == 4);

	reset();
	c = getopt(3, av3, "ab:"); CHECK(c == '?' && optopt == 'x');
	c = getopt(3, av3, "ab:"); CHECK(c == '?' && optopt == 'b');
	reset();
	c = getopt(3, av3, ":ab:"); CHECK(c == '?' && optopt == 'x');
	c = getopt(3, av3, ":ab:"); CHECK(c == ':' && optopt == 'b');

	/* getopt_long */
	reset(); flag = 0;
	{
		struct option lo2[5];
		memcpy(lo2, lo, sizeof lo2);
		lo2[3].flag = &flag;
		c = getopt_long(9, av4, "ab:g::", lo2, &idx); CHECK(c == 'a' && idx == 0);
		c = getopt_long(9, av4, "ab:g::", lo2, &idx); CHECK(c == 'b' && streq(optarg, "1"));
		c = getopt_long(9, av4, "ab:g::", lo2, &idx); CHECK(c == 'b' && streq(optarg, "2"));
		c = getopt_long(9, av4, "ab:g::", lo2, &idx); CHECK(c == 'g' && optarg == 0 && idx == 2);
		c = getopt_long(9, av4, "ab:g::", lo2, &idx); CHECK(c == 'g' && streq(optarg, "3"));
		c = getopt_long(9, av4, "ab:g::", lo2, &idx); CHECK(c == 0 && flag == 1 && idx == 3);
		c = getopt_long(9, av4, "ab:g::", lo2, &idx); CHECK(c == -1 && optind == 8 && streq(av4[8], "pos"));
	}

	/* permutation */
	reset();
	c = getopt_long(7, av5, "ab:", lo, &idx); CHECK(c == 'a');
	c = getopt_long(7, av5, "ab:", lo, &idx); CHECK(c == 'b' && streq(optarg, "v"));
	c = getopt_long(7, av5, "ab:", lo, &idx); CHECK(c == -1 && optind == 4);
	CHECK(streq(av5[4], "pos1") && streq(av5[5], "pos2") && streq(av5[6], "pos3"));

	/* errors */
	reset();
	c = getopt_long(3, av6, "ab:", lo, &idx); CHECK(c == '?');
	c = getopt_long(3, av6, "ab:", lo, &idx); CHECK(c == '?' && optopt == 'b');
	reset();
	c = getopt_long(3, av6, ":ab:", lo, &idx); CHECK(c == '?');
	c = getopt_long(3, av6, ":ab:", lo, &idx); CHECK(c == ':');

	/* long only */
	reset();
	c = getopt_long_only(5, av7, "ab:", lo, &idx); CHECK(c == 'a' && idx == 0);
	c = getopt_long_only(5, av7, "ab:", lo, &idx); CHECK(c == 'a');
	c = getopt_long_only(5, av7, "ab:", lo, &idx); CHECK(c == 'b' && streq(optarg, "v"));
	c = getopt_long_only(5, av7, "ab:", lo, &idx); CHECK(c == -1);

	/* basename */
#define B(in, out) do { strcpy(buf, in); CHECK(streq(basename(buf), out)); } while (0)
#define D(in, out) do { strcpy(buf, in); CHECK(streq(dirname(buf), out)); } while (0)
	B("/usr/lib", "lib"); B("/usr/", "usr"); B("usr", "usr"); B("/", "/"); B(".", "."); B("..", "..");
	B("", "."); B("///", "/"); B("a/b//", "b"); B("C:/", "/"); B("C:\\foo", "foo"); B("C:/foo/bar", "bar");
	B("C:\\", "\\"); B("C:", "."); B("C:foo", "foo"); B("\\foo\\bar\\", "bar"); B("a\\b/c", "c");
	CHECK(streq(basename(0), "."));
	D("/usr/lib", "/usr"); D("/usr/", "/"); D("usr", "."); D("/", "/"); D(".", "."); D("..", ".");
	D("", "."); D("///", "/"); D("a/b//", "a"); D("a//b", "a"); D("/a", "/"); D("//a", "/");
	D("C:\\foo", "C:\\"); D("C:/foo/bar", "C:/foo"); D("C:/", "C:/"); D("C:\\", "C:\\"); D("C:", "C:");
	D("C:foo", "C:"); D("C:foo\\bar", "C:foo"); D("\\foo\\bar\\", "\\foo"); D("a\\b/c", "a\\b");
	CHECK(streq(dirname(0), "."));

	/* locale */
	CHECK(streq(setlocale(LC_ALL, "C"), "C") && streq(setlocale(LC_ALL, ""), "C") && streq(setlocale(LC_CTYPE, "POSIX"), "C"));
	CHECK(streq(setlocale(LC_ALL, 0), "C") && setlocale(LC_ALL, "en_US.UTF-8") == 0);
	CHECK(streq(localeconv()->decimal_point, ".") && streq(localeconv()->thousands_sep, "") && localeconv()->frac_digits == 127);
	{
		locale_t l = newlocale(LC_ALL_MASK, "C", 0);
		CHECK(l != 0 && duplocale(l) != 0 && newlocale(LC_ALL_MASK, "fr_FR", 0) == 0);
		uselocale(l); freelocale(l);
	}

	if (fails) { wr("getopt: failures: "); wrnum(fails); wr("\n"); return 1; }
	wr("getopt: all ok\n");
	return 0;
}
