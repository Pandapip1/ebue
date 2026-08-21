/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <wchar.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
	int i;

	/* abs/div */
	CHECK(abs(-5) == 5 && abs(5) == 5 && abs(0) == 0);
	CHECK(labs(-100000L) == 100000L);
	CHECK(llabs(-5000000000LL) == 5000000000LL);
	CHECK(imaxabs(-7) == 7);
	{ div_t d = div(7, 3); CHECK(d.quot == 2 && d.rem == 1); }
	{ div_t d = div(-7, 3); CHECK(d.quot == -2 && d.rem == -1); }
	{ ldiv_t d = ldiv(100000L, 7L); CHECK(d.quot == 14285 && d.rem == 5); }
	{ lldiv_t d = lldiv(10000000000LL, 3LL); CHECK(d.quot == 3333333333LL && d.rem == 1); }
	{ imaxdiv_t d = imaxdiv(-9, 4); CHECK(d.quot == -2 && d.rem == -1); }

	/* rand */
	srand(1);
	{
		int a = rand(), b = rand();
		CHECK(a >= 0 && a <= RAND_MAX && b >= 0);
		srand(1);
		CHECK(rand() == a && rand() == b);
	}
	{
		unsigned s = 42;
		int a = rand_r(&s);
		unsigned s2 = 42;
		CHECK(rand_r(&s2) == a && a >= 0);
	}

	/* random */
	srandom(1);
	{
		long a = random(), b = random();
		CHECK(a >= 0 && b >= 0);
		srandom(1);
		CHECK(random() == a && random() == b);
	}

	/* rand48 */
	srand48(123);
	{
		double d = drand48();
		long l;
		CHECK(d >= 0.0 && d < 1.0);
		l = lrand48();
		CHECK(l >= 0 && l <= 0x7fffffffL);
		l = mrand48();
		CHECK(l >= -2147483647L - 1 && l <= 2147483647L);
		srand48(123);
		CHECK(drand48() == d);
	}
	{
		unsigned short s[3] = { 1, 2, 3 };
		unsigned short s2[3] = { 1, 2, 3 };
		CHECK(erand48(s) == erand48(s2));
		CHECK(nrand48(s) == nrand48(s2));
	}

	/* multibyte: UTF-8 <-> UTF-16 */
	CHECK(MB_CUR_MAX == 4);
	CHECK(mblen("A", 1) == 1);
	CHECK(mblen("\xc3\xa9", 2) == 2);          /* e-acute */
	CHECK(mblen("\xe2\x82\xac", 3) == 3);      /* euro sign */
	CHECK(mblen("", 1) == 0);
	CHECK(mblen("\xff", 1) == -1);
	CHECK(mblen("\xc0\xaf", 2) == -1);         /* overlong */
	{
		wchar_t wc = 0;
		CHECK(mbtowc(&wc, "\xe2\x82\xac", 3) == 3 && wc == 0x20ac);
		CHECK(mbtowc(&wc, "A", 1) == 1 && wc == 'A');
	}
	{
		char buf[8];
		CHECK(wctomb(buf, 0x20ac) == 3 && !memcmp(buf, "\xe2\x82\xac", 3));
		CHECK(wctomb(buf, 'A') == 1 && buf[0] == 'A');
	}
	{
		/* non-BMP: U+1F600 = F0 9F 98 80 = D83D DE00 */
		mbstate_t st;
		wchar_t wc = 0;
		size_t r;
		memset(&st, 0, sizeof st);
		r = mbrtowc(&wc, "\xf0\x9f\x98\x80", 4, &st);
		CHECK(r == 4 && wc == 0xd83d);
		CHECK(!mbsinit(&st));
		r = mbrtowc(&wc, "x", 1, &st);
		CHECK(r == (size_t)-3 && wc == 0xde00);
		CHECK(mbsinit(&st));
	}
	{
		/* partial sequences held in state */
		mbstate_t st;
		wchar_t wc = 0;
		memset(&st, 0, sizeof st);
		CHECK(mbrtowc(&wc, "\xe2", 1, &st) == (size_t)-2);
		CHECK(mbrtowc(&wc, "\x82", 1, &st) == (size_t)-2);
		CHECK(mbrtowc(&wc, "\xac", 1, &st) == 1 && wc == 0x20ac);
	}
	{
		/* wcrtomb surrogate pair */
		mbstate_t st;
		char buf[8];
		memset(&st, 0, sizeof st);
		CHECK(wcrtomb(buf, 0xd83d, &st) == 0);
		CHECK(wcrtomb(buf, 0xde00, &st) == 4 && !memcmp(buf, "\xf0\x9f\x98\x80", 4));
	}
	{
		wchar_t ws[16];
		char mb[32];
		size_t r = mbstowcs(ws, "a\xc3\xa9\xe2\x82\xac", 16);
		CHECK(r == 3 && ws[0] == 'a' && ws[1] == 0xe9 && ws[2] == 0x20ac && ws[3] == 0);
		r = wcstombs(mb, ws, 32);
		CHECK(r == 6 && !strcmp(mb, "a\xc3\xa9\xe2\x82\xac"));
		/* surrogate pair through mbstowcs/wcstombs */
		r = mbstowcs(ws, "\xf0\x9f\x98\x80!", 16);
		CHECK(r == 3 && ws[0] == 0xd83d && ws[1] == 0xde00 && ws[2] == '!');
		r = wcstombs(mb, ws, 32);
		CHECK(r == 5 && !strcmp(mb, "\xf0\x9f\x98\x80!"));
		/* counting mode */
		CHECK(mbstowcs(0, "\xf0\x9f\x98\x80!", 0) == 3);
		CHECK(wcstombs(0, ws, 0) == 5);
	}
	CHECK(btowc('A') == 'A');
	CHECK(btowc(0x80 | 0) == WEOF);
	CHECK(wctob('z') == 'z');
	CHECK(wctob(0x20ac) == -1);

	/* mkstemp / mkdtemp */
	{
		char t[] = "stdlibtest-XXXXXX";
		int fd = mkstemp(t);
		CHECK(fd >= 0);
		CHECK(strcmp(t, "stdlibtest-XXXXXX") != 0);
		if (fd >= 0) {
			CHECK(write(fd, "hi", 2) == 2);
			close(fd);
			unlink(t);
		}
	}
	{
		char t[] = "stdlibtest-XXXXXX.txt";
		int fd = mkstemps(t, 4);
		CHECK(fd >= 0);
		CHECK(strstr(t, ".txt") != 0);
		if (fd >= 0) { close(fd); unlink(t); }
	}
	{
		char t[] = "bad-template";
		errno = 0;
		CHECK(mkstemp(t) == -1 && errno == EINVAL);
	}
	{
		char t[] = "stdlibdir-XXXXXX";
		char *r = mkdtemp(t);
		CHECK(r == t);
		if (r) rmdir(t);
	}

	/* realpath */
	{
		char t[] = "rp-XXXXXX";
		int fd = mkstemp(t);
		CHECK(fd >= 0);
		if (fd >= 0) {
			char *p = realpath(t, 0);
			CHECK(p != 0);
			if (p) {
				CHECK(!strchr(p, '\\'));
				CHECK(strstr(p, "rp-") != 0);
				/* absolute: drive letter and colon */
				CHECK(p[1] == ':' && p[2] == '/');
				free(p);
			}
			close(fd);
			unlink(t);
		}
		errno = 0;
		CHECK(realpath("definitely-not-there-12345", 0) == 0 && errno == ENOENT);
	}

	/* getsubopt */
	{
		char buf[] = "ro,size=10,unknown";
		char *tokens[] = { "ro", "rw", "size", 0 };
		char *subopts = buf, *val;
		CHECK(getsubopt(&subopts, tokens, &val) == 0 && val == 0);
		CHECK(getsubopt(&subopts, tokens, &val) == 2 && val && !strcmp(val, "10"));
		CHECK(getsubopt(&subopts, tokens, &val) == -1 && !strcmp(val, "unknown"));
		CHECK(*subopts == 0);
	}

	/* a64l / l64a */
	CHECK(a64l("") == 0);
	CHECK(l64a(0)[0] == 0);
	CHECK(a64l(l64a(123456L)) == 123456L);
	CHECK(a64l(l64a(0x7fffffffL)) == 0x7fffffffL);
	CHECK(a64l("/") == 1);
	CHECK(!strcmp(l64a(1), "/"));

	/* getloadavg */
	{ double lav[3]; CHECK(getloadavg(lav, 3) == -1); }

	/* ecvt/fcvt/gcvt */
	{
		int dp, sg;
		char *p = ecvt(3.1415, 5, &dp, &sg);
		CHECK(!strcmp(p, "31415") && dp == 1 && sg == 0);
		p = ecvt(-0.0025, 2, &dp, &sg);
		CHECK(!strcmp(p, "25") && dp == -2 && sg == 1);
		p = fcvt(3.1415, 2, &dp, &sg);
		CHECK(!strcmp(p, "314") && dp == 1 && sg == 0);
		p = fcvt(-0.0025, 4, &dp, &sg);
		CHECK(!strcmp(p, "25") && dp == -2 && sg == 1);
	}
	{
		char buf[32];
		CHECK(!strcmp(gcvt(1.5, 6, buf), "1.5"));
		CHECK(!strcmp(gcvt(1e10, 6, buf), "1e+10"));
	}

	for (i = 0; i < 0; i++);
	if (!fails) printf("stdlib: all tests passed\n");
	return fails != 0;
}
