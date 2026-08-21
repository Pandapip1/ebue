/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct known {
	long long t;
	int year, mon, mday, hour, min, sec, wday, yday;
};

/* Fixed epochs with independently known broken-down values. */
static const struct known known[] = {
	{ 0LL,            1970,  0,  1,  0,  0,  0, 4,   0 },  /* Thu 1970-01-01 */
	{ 951782400LL,    2000,  1, 29,  0,  0,  0, 2,  59 },  /* Tue 2000-02-29 (leap) */
	{ 1078012800LL,   2004,  1, 29,  0,  0,  0, 0,  59 },  /* Sun 2004-02-29 */
	{ 2147483647LL,   2038,  0, 19,  3, 14,  7, 2,  18 },  /* Tue 2038-01-19 */
	{ -1LL,           1969, 11, 31, 23, 59, 59, 3, 364 },  /* Wed 1969-12-31 */
	{ -86400LL,       1969, 11, 31,  0,  0,  0, 3, 364 },
	{ -2208988800LL,  1900,  0,  1,  0,  0,  0, 1,   0 },  /* Mon 1900-01-01 */
	{ 946684799LL,    1999, 11, 31, 23, 59, 59, 5, 364 },  /* Fri */
	{ 946684800LL,    2000,  0,  1,  0,  0,  0, 6,   0 },  /* Sat */
	{ 978307199LL,    2000, 11, 31, 23, 59, 59, 0, 365 },  /* Sun, leap year has 366 days */
	{ 978307200LL,    2001,  0,  1,  0,  0,  0, 1,   0 },  /* Mon */
	{ 1709164800LL,   2024,  1, 29,  0,  0,  0, 4,  59 },  /* Thu 2024-02-29 */
	{ 4102444800LL,   2100,  0,  1,  0,  0,  0, 5,   0 },  /* Fri 2100-01-01 */
	{ 4107456000LL,   2100,  1, 28,  0,  0,  0, 0,  58 },  /* 2100 not a leap year */
	{ 4107542400LL,   2100,  2,  1,  0,  0,  0, 1,  59 },
};

static void check_tm(const struct tm *tm, const struct known *k)
{
	CHECK(tm != NULL);
	if (!tm) return;
	CHECK(tm->tm_year + 1900 == k->year);
	CHECK(tm->tm_mon == k->mon);
	CHECK(tm->tm_mday == k->mday);
	CHECK(tm->tm_hour == k->hour);
	CHECK(tm->tm_min == k->min);
	CHECK(tm->tm_sec == k->sec);
	CHECK(tm->tm_wday == k->wday);
	CHECK(tm->tm_yday == k->yday);
	CHECK(tm->tm_isdst == 0);
}

static void fill(struct tm *tm, int y, int mo, int d, int h, int mi, int s)
{
	memset(tm, 0, sizeof *tm);
	tm->tm_year = y - 1900;
	tm->tm_mon = mo;
	tm->tm_mday = d;
	tm->tm_hour = h;
	tm->tm_min = mi;
	tm->tm_sec = s;
}

int main(void)
{
	size_t i;
	char buf[128];

	CHECK(setenv("TZ", "UTC0", 1) == 0);
	tzset();
	CHECK(timezone == 0);
	CHECK(daylight == 0);
	CHECK(!strcmp(tzname[0], "UTC"));

	/* gmtime / localtime (TZ=UTC0) against known epochs */
	for (i = 0; i < sizeof known / sizeof *known; i++) {
		time_t t = (time_t)known[i].t;
		struct tm tm, *p;
		p = gmtime(&t);
		check_tm(p, &known[i]);
		CHECK(gmtime_r(&t, &tm) == &tm);
		check_tm(&tm, &known[i]);
		p = localtime(&t);
		check_tm(p, &known[i]);
		CHECK(localtime_r(&t, &tm) == &tm);
		check_tm(&tm, &known[i]);
		/* round-trips */
		gmtime_r(&t, &tm);
		CHECK(timegm(&tm) == t);
		check_tm(&tm, &known[i]);
		localtime_r(&t, &tm);
		CHECK(mktime(&tm) == t);
		check_tm(&tm, &known[i]);
	}

	/* yday over a whole leap year and non-leap year is consecutive */
	{
		time_t t;
		struct tm tm;
		int expect = 0;
		for (t = 946684800; t < 978307200; t += 86400) {   /* 2000 */
			gmtime_r(&t, &tm);
			CHECK(tm.tm_yday == expect++);
		}
		CHECK(expect == 366);
		expect = 0;
		for (t = 978307200; t < 1009843200; t += 86400) {  /* 2001 */
			gmtime_r(&t, &tm);
			CHECK(tm.tm_yday == expect++);
		}
		CHECK(expect == 365);
	}

	/* mktime / timegm normalisation */
	{
		struct tm tm;

		fill(&tm, 2000, 13, 1, 0, 0, 0);        /* month 13 -> Feb 2001 */
		CHECK(timegm(&tm) == 980985600);
		CHECK(tm.tm_year == 101 && tm.tm_mon == 1 && tm.tm_mday == 1);

		fill(&tm, 2000, 2, 0, 0, 0, 0);         /* day 0 of March -> Feb 29 */
		CHECK(timegm(&tm) == 951782400);
		CHECK(tm.tm_mon == 1 && tm.tm_mday == 29 && tm.tm_wday == 2 && tm.tm_yday == 59);

		fill(&tm, 1999, 11, 31, 25, 0, 0);      /* hour 25 -> Jan 1 01:00 */
		CHECK(timegm(&tm) == 946688400);
		CHECK(tm.tm_year == 100 && tm.tm_mon == 0 && tm.tm_mday == 1 && tm.tm_hour == 1);

		fill(&tm, 1970, 0, 1, 0, 0, -1);        /* sec -1 -> 1969-12-31 23:59:59 */
		CHECK(timegm(&tm) == -1);
		CHECK(tm.tm_year == 69 && tm.tm_mon == 11 && tm.tm_mday == 31 && tm.tm_hour == 23 && tm.tm_min == 59 && tm.tm_sec == 59);

		fill(&tm, 1970, -1, 1, 0, 0, 0);        /* month -1 -> Dec 1969 */
		CHECK(timegm(&tm) == -2678400);
		CHECK(tm.tm_year == 69 && tm.tm_mon == 11);

		fill(&tm, 1970, 0, 1, 0, 0, 0);
		tm.tm_mon = 24; tm.tm_mday = 32; tm.tm_min = 61;
		CHECK(timegm(&tm) == timegm(&tm));
		CHECK(tm.tm_year == 72 && tm.tm_mon == 1 && tm.tm_mday == 1 && tm.tm_hour == 1 && tm.tm_min == 1);

		/* same through mktime under UTC */
		fill(&tm, 2000, 13, 1, 0, 0, 0);
		CHECK(mktime(&tm) == 980985600);
		fill(&tm, 2000, 2, 0, 0, 0, 0);
		CHECK(mktime(&tm) == 951782400);
		fill(&tm, 1999, 11, 31, 25, 0, 0);
		CHECK(mktime(&tm) == 946688400);
		fill(&tm, 1970, 0, 1, 0, 0, -1);
		CHECK(mktime(&tm) == -1);
		CHECK(tm.tm_wday == 3 && tm.tm_yday == 364);
	}

	/* strftime */
	{
		struct tm tm;
		time_t t = 951782400 + 13 * 3600 + 5 * 60 + 9;   /* 2000-02-29 13:05:09 Tue */
		size_t n;

		gmtime_r(&t, &tm);
#define FMT(f, expect) do { \
		memset(buf, 'x', sizeof buf); \
		n = strftime(buf, sizeof buf, f, &tm); \
		CHECK(n == strlen(expect)); \
		CHECK(!strcmp(buf, expect)); \
		if (strcmp(buf, expect)) printf("  %s -> \"%s\" (want \"%s\")\n", f, buf, expect); \
	} while (0)
		FMT("%Y", "2000");
		FMT("%m", "02");
		FMT("%d", "29");
		FMT("%H", "13");
		FMT("%M", "05");
		FMT("%S", "09");
		FMT("%j", "060");
		FMT("%a", "Tue");
		FMT("%A", "Tuesday");
		FMT("%b", "Feb");
		FMT("%h", "Feb");
		FMT("%B", "February");
		FMT("%y", "00");
		FMT("%C", "20");
		FMT("%e", "29");
		FMT("%p", "PM");
		FMT("%I", "01");
		FMT("%Z", "UTC");
		FMT("%z", "+0000");
		FMT("%%", "%");
		FMT("%F", "2000-02-29");
		FMT("%T", "13:05:09");
		FMT("%D", "02/29/00");
		FMT("%R", "13:05");
		FMT("%r", "01:05:09 PM");
		FMT("%x", "02/29/00");
		FMT("%X", "13:05:09");
		FMT("%c", "Tue Feb 29 13:05:09 2000");
		FMT("%u", "2");
		FMT("%w", "2");
		FMT("%n%t", "\n\t");
		FMT("", "");
		FMT("plain text", "plain text");
		FMT("%Y-%m-%dT%H:%M:%SZ", "2000-02-29T13:05:09Z");

		/* Sunday: %u is 7, %w is 0; single-digit %e is space padded */
		t = 1078012800;   /* Sun 2004-02-29 */
		gmtime_r(&t, &tm);
		FMT("%u", "7");
		FMT("%w", "0");
		FMT("%a %A", "Sun Sunday");
		t = 0;
		gmtime_r(&t, &tm);
		FMT("%e", " 1");
		FMT("%c", "Thu Jan  1 00:00:00 1970");
		FMT("%I %p", "12 AM");
		FMT("%r", "12:00:00 AM");
		FMT("%j", "001");
		tm.tm_hour = 12;
		FMT("%I %p", "12 PM");
		tm.tm_hour = 23;
		FMT("%I %p", "11 PM");
		t = -2208988800LL;   /* 1900 */
		gmtime_r(&t, &tm);
		FMT("%Y %y %C", "1900 00 19");

		/* The week-number family is documented as unimplemented in
		 * src/time/strftime.c; unknown conversions pass through
		 * literally, so just pin that behaviour rather than the
		 * (absent) week numbers. */
		t = 951782400;
		gmtime_r(&t, &tm);
		memset(buf, 0, sizeof buf);
		n = strftime(buf, sizeof buf, "%U", &tm);
		CHECK(n == 2);
		CHECK(!strcmp(buf, "%U") || !strcmp(buf, "09"));
		n = strftime(buf, sizeof buf, "%W", &tm);
		CHECK(!strcmp(buf, "%W") || !strcmp(buf, "09"));
		n = strftime(buf, sizeof buf, "%V", &tm);
		CHECK(!strcmp(buf, "%V") || !strcmp(buf, "09"));
		n = strftime(buf, sizeof buf, "%G", &tm);
		CHECK(!strcmp(buf, "%G") || !strcmp(buf, "2000"));
		n = strftime(buf, sizeof buf, "%s", &tm);
		CHECK(!strcmp(buf, "%s") || !strcmp(buf, "951782400"));

		/* width-limited buffers: too small returns 0, exact fit works */
		n = strftime(buf, 4, "%Y", &tm);
		CHECK(n == 0);
		n = strftime(buf, 5, "%Y", &tm);
		CHECK(n == 4 && !strcmp(buf, "2000"));
		n = strftime(buf, 1, "%Y", &tm);
		CHECK(n == 0);
		n = strftime(buf, 0, "%Y", &tm);
		CHECK(n == 0);
		n = strftime(buf, 1, "", &tm);
		CHECK(n == 0 && buf[0] == 0);
		n = strftime(buf, 10, "%F", &tm);
		CHECK(n == 0);
		n = strftime(buf, 11, "%F", &tm);
		CHECK(n == 10);
		n = strftime(buf, 3, "abcdef", &tm);
		CHECK(n == 0);

		/* strftime_l with a null locale behaves like strftime */
		n = strftime_l(buf, sizeof buf, "%F", &tm, (locale_t)0);
		CHECK(n == 10 && !strcmp(buf, "2000-02-29"));

		/* %z with a non-zero gmtoff */
		tm.__tm_gmtoff = -5 * 3600;
		FMT("%z", "-0500");
		tm.__tm_gmtoff = 5 * 3600 + 30 * 60;
		FMT("%z", "+0530");
		tm.__tm_zone = "EST";
		FMT("%Z", "EST");
#undef FMT
	}

	/* strptime parses back what strftime wrote */
	{
		struct tm tm, out;
		time_t t = 2147483647;   /* 2038-01-19 03:14:07 Tue */
		char *end;
		static const char *const fmts[] = {
			"%Y-%m-%d %H:%M:%S",
			"%a %b %e %H:%M:%S %Y",
			"%A, %B %d, %Y %I:%M:%S %p",
			"%d/%m/%y %H:%M",
			"%j %Y %H:%M:%S",
			"%H%M%S %d%m%Y",
			"%F %T",
			"%j %Y %T",
			"%c",
			"%D %R",
/* BUG (live, expected to FAIL): src/time/strptime.c:70 %Y reads up to 10 digits, so in a
       * format with no separator after %Y it swallows the following
       * fields ("20380119031407" with "%Y%m%d%H%M%S" fails); POSIX/glibc
       * limit %Y to 4 digits. */
			"%Y%m%d%H%M%S",
		};

		gmtime_r(&t, &tm);
		for (i = 0; i < sizeof fmts / sizeof *fmts; i++) {
			size_t n = strftime(buf, sizeof buf, fmts[i], &tm);
			CHECK(n > 0);
			memset(&out, 0, sizeof out);
			out.tm_mday = 1;
			end = strptime(buf, fmts[i], &out);
			CHECK(end != NULL);
			if (!end) { printf("  strptime(\"%s\", \"%s\") failed\n", buf, fmts[i]); continue; }
			CHECK(*end == 0);
			CHECK(out.tm_year == tm.tm_year);
			if (strstr(fmts[i], "%j") == NULL) {
				CHECK(out.tm_mon == tm.tm_mon);
				CHECK(out.tm_mday == tm.tm_mday);
			} else {
				CHECK(out.tm_yday == tm.tm_yday);
			}
			CHECK(out.tm_hour == tm.tm_hour);
			CHECK(out.tm_min == tm.tm_min);
			if (strstr(fmts[i], "%S") || strstr(fmts[i], "%T") || strstr(fmts[i], "%c"))
				CHECK(out.tm_sec == tm.tm_sec);
			if (strstr(fmts[i], "%a") || strstr(fmts[i], "%A") || strstr(fmts[i], "%c"))
				CHECK(out.tm_wday == tm.tm_wday);
		}

		/* explicit values */
		memset(&out, 0, sizeof out);
		end = strptime("1970-01-01 00:00:00", "%Y-%m-%d %H:%M:%S", &out);
		CHECK(end && *end == 0);
		CHECK(out.tm_year == 70 && out.tm_mon == 0 && out.tm_mday == 1 && out.tm_hour == 0);
		CHECK(timegm(&out) == 0);

		memset(&out, 0, sizeof out);
		end = strptime("Thursday January 1 1970", "%A %B %d %Y", &out);
		CHECK(end && *end == 0 && out.tm_wday == 4 && out.tm_mon == 0);

		/* case-insensitive names, abbreviation vs full */
		memset(&out, 0, sizeof out);
		end = strptime("tue FEB 29", "%a %b %d", &out);
		CHECK(end && *end == 0 && out.tm_wday == 2 && out.tm_mon == 1 && out.tm_mday == 29);

		/* %p handling */
		memset(&out, 0, sizeof out);
		end = strptime("12:30 AM", "%I:%M %p", &out);
		CHECK(end && out.tm_hour == 0 && out.tm_min == 30);
		memset(&out, 0, sizeof out);
		end = strptime("12:30 PM", "%I:%M %p", &out);
		CHECK(end && out.tm_hour == 12);
		memset(&out, 0, sizeof out);
		end = strptime("1:30 PM", "%I:%M %p", &out);
		CHECK(end && out.tm_hour == 13);

		/* %y pivot: 69..99 -> 19xx, 00..68 -> 20xx */
		memset(&out, 0, sizeof out);
		CHECK(strptime("69", "%y", &out) && out.tm_year == 69);
		CHECK(strptime("68", "%y", &out) && out.tm_year == 168);
		CHECK(strptime("99", "%y", &out) && out.tm_year == 99);
		CHECK(strptime("00", "%y", &out) && out.tm_year == 100);

		/* %z */
		memset(&out, 0, sizeof out);
		CHECK(strptime("-0500", "%z", &out) && out.__tm_gmtoff == -18000);
		CHECK(strptime("+05:30", "%z", &out) && out.__tm_gmtoff == 19800);
		CHECK(strptime("Z", "%z", &out) && out.__tm_gmtoff == 0);

		/* leftover input is returned, not an error */
		memset(&out, 0, sizeof out);
		end = strptime("2000-02-29 trailing", "%Y-%m-%d", &out);
		CHECK(end && !strcmp(end, " trailing"));

		/* whitespace in the format matches any amount (incl. none) of input space */
		memset(&out, 0, sizeof out);
		end = strptime("2000   02", "%Y %m", &out);
		CHECK(end && *end == 0 && out.tm_mon == 1);

		/* %% */
		memset(&out, 0, sizeof out);
		end = strptime("50%", "%S%%", &out);
		CHECK(end && *end == 0 && out.tm_sec == 50);

		/* garbage is rejected */
		memset(&out, 0, sizeof out);
		CHECK(strptime("garbage", "%Y-%m-%d", &out) == NULL);
		CHECK(strptime("2000-xx-29", "%Y-%m-%d", &out) == NULL);
		CHECK(strptime("2000/02/29", "%Y-%m-%d", &out) == NULL);
		CHECK(strptime("Xyzday", "%A", &out) == NULL);
		CHECK(strptime("Foo", "%b", &out) == NULL);
		CHECK(strptime("13:00 XM", "%H:%M %p", &out) == NULL);
		CHECK(strptime("", "%Y", &out) == NULL);
		CHECK(strptime("abc", "%Q", &out) == NULL);
		CHECK(strptime("2000", "%Y%", &out) == NULL);
		CHECK(strptime("5", "%%", &out) == NULL);
		CHECK(strptime("x", "%z", &out) == NULL);
	}

	/* asctime / ctime */
	{
		struct tm tm;
		time_t t = 0;
		char abuf[32];

		gmtime_r(&t, &tm);
		CHECK(!strcmp(asctime(&tm), "Thu Jan  1 00:00:00 1970\n"));
		CHECK(asctime_r(&tm, abuf) == abuf);
		CHECK(!strcmp(abuf, "Thu Jan  1 00:00:00 1970\n"));
		CHECK(strlen(abuf) == 25);
		CHECK(!strcmp(ctime(&t), "Thu Jan  1 00:00:00 1970\n"));
		CHECK(ctime_r(&t, abuf) == abuf);
		CHECK(!strcmp(abuf, "Thu Jan  1 00:00:00 1970\n"));

		t = 951782400 + 13 * 3600 + 5 * 60 + 9;
		CHECK(!strcmp(ctime(&t), "Tue Feb 29 13:05:09 2000\n"));
		t = 2147483647;
		CHECK(!strcmp(ctime(&t), "Tue Jan 19 03:14:07 2038\n"));
		t = -1;
		CHECK(!strcmp(ctime(&t), "Wed Dec 31 23:59:59 1969\n"));
		t = 1078012800;
		CHECK(!strcmp(ctime(&t), "Sun Feb 29 00:00:00 2004\n"));

		/* asctime and strftime %c agree */
		gmtime_r(&t, &tm);
		strftime(buf, sizeof buf, "%c\n", &tm);
		CHECK(!strcmp(buf, asctime(&tm)));
	}

	/* difftime */
	{
		CHECK(difftime(10, 3) == 7.0);
		CHECK(difftime(3, 10) == -7.0);
		CHECK(difftime(0, 0) == 0.0);
		CHECK(difftime(2147483647, -1) == 2147483648.0);
		CHECK(difftime(-5, -10) == 5.0);
		CHECK(difftime((time_t)1LL << 40, 0) == 1099511627776.0);
	}

	/* fixed-offset timezones */
	{
		struct tm tm;
		time_t t = 0, r;

		CHECK(setenv("TZ", "EST5", 1) == 0);
		tzset();
		CHECK(timezone == 5 * 3600);
		CHECK(daylight == 0);
		CHECK(!strcmp(tzname[0], "EST"));

		CHECK(localtime_r(&t, &tm) == &tm);
		CHECK(tm.tm_year == 69 && tm.tm_mon == 11 && tm.tm_mday == 31);
		CHECK(tm.tm_hour == 19 && tm.tm_min == 0 && tm.tm_sec == 0);
		CHECK(tm.tm_wday == 3 && tm.tm_yday == 364);
		CHECK(tm.tm_isdst == 0);
		CHECK(tm.__tm_gmtoff == -5 * 3600);
		CHECK(tm.__tm_zone && !strcmp(tm.__tm_zone, "EST"));
		strftime(buf, sizeof buf, "%Z %z", &tm);
		CHECK(!strcmp(buf, "EST -0500"));
		CHECK(!strcmp(ctime(&t), "Wed Dec 31 19:00:00 1969\n"));

		/* mktime interprets fields as local -> UTC epoch */
		r = mktime(&tm);
		CHECK(r == 0);
		CHECK(tm.tm_hour == 19);
		fill(&tm, 1970, 0, 1, 0, 0, 0);
		CHECK(mktime(&tm) == 5 * 3600);
		/* gmtime is unaffected by TZ; timegm too */
		CHECK(gmtime_r(&t, &tm)->tm_hour == 0);
		fill(&tm, 1970, 0, 1, 0, 0, 0);
		CHECK(timegm(&tm) == 0);

		/* positive (east) offset, with minutes */
		CHECK(setenv("TZ", "IST-5:30", 1) == 0);
		tzset();
		CHECK(timezone == -(5 * 3600 + 30 * 60));
		CHECK(!strcmp(tzname[0], "IST"));
		localtime_r(&t, &tm);
		CHECK(tm.tm_mday == 1 && tm.tm_hour == 5 && tm.tm_min == 30);
		CHECK(tm.__tm_gmtoff == 5 * 3600 + 30 * 60);
		strftime(buf, sizeof buf, "%z", &tm);
		CHECK(!strcmp(buf, "+0530"));
		CHECK(mktime(&tm) == 0);

		/* angle-bracket-quoted name */
		CHECK(setenv("TZ", "<+03>-3", 1) == 0);
		tzset();
		CHECK(timezone == -3 * 3600);
		CHECK(!strcmp(tzname[0], "+03"));

		/* DST rule suffix is ignored, but the base offset still parsed */
		CHECK(setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1) == 0);
		tzset();
		CHECK(timezone == 8 * 3600);
		CHECK(!strcmp(tzname[0], "PST"));
		CHECK(daylight == 0);

		/* unset / empty -> UTC */
		CHECK(setenv("TZ", "", 1) == 0);
		tzset();
		CHECK(timezone == 0 && !strcmp(tzname[0], "UTC"));
		CHECK(unsetenv("TZ") == 0);
		tzset();
		CHECK(timezone == 0 && !strcmp(tzname[0], "UTC"));
		localtime_r(&t, &tm);
		CHECK(tm.tm_hour == 0 && tm.tm_mday == 1);

		CHECK(setenv("TZ", "UTC0", 1) == 0);
		tzset();
	}

	/* getdate */
	{
		struct tm *p;

		p = getdate("2000-02-29 13:05:09");
		CHECK(p != NULL);
		if (p) {
			CHECK(p->tm_year == 100 && p->tm_mon == 1 && p->tm_mday == 29);
			CHECK(p->tm_hour == 13 && p->tm_min == 5 && p->tm_sec == 9);
			CHECK(p->tm_wday == 2 && p->tm_yday == 59);
		}
		p = getdate("02/29/2000");
		CHECK(p && p->tm_year == 100 && p->tm_mon == 1 && p->tm_mday == 29 && p->tm_wday == 2);
		p = getdate("29 February 2000");
		CHECK(p && p->tm_mon == 1 && p->tm_mday == 29 && p->tm_year == 100);
		p = getdate("February 29, 2000");
		CHECK(p && p->tm_mon == 1 && p->tm_mday == 29 && p->tm_year == 100);
		p = getdate("13:05");
		CHECK(p && p->tm_hour == 13 && p->tm_min == 5);

		getdate_err = 0;
		CHECK(getdate("not a date at all") == NULL);
		CHECK(getdate_err == 7);
		getdate_err = 0;
		CHECK(getdate("") == NULL);
		CHECK(getdate_err == 1);
		getdate_err = 0;
		CHECK(getdate(NULL) == NULL);
		CHECK(getdate_err == 1);
	}

	/* time / clock_gettime / timespec_get / clock */
	{
		time_t t1, t2, t3;
		struct timespec ts, ts2, tg, mono1, mono2, cpu;
		int k;

		t1 = time(NULL);
		CHECK(time(&t2) == t2);
		CHECK(t1 >= 1600000000);
		CHECK(t2 >= t1 && t2 - t1 <= 5);

		CHECK(clock_gettime(CLOCK_REALTIME, &ts) == 0);
		CHECK(ts.tv_sec >= 1600000000);
		CHECK(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000);
		CHECK(ts.tv_sec >= t1 && ts.tv_sec - t1 <= 5);
		CHECK(clock_gettime(CLOCK_REALTIME_COARSE, &ts2) == 0);
		CHECK(ts2.tv_sec >= ts.tv_sec && ts2.tv_sec - ts.tv_sec <= 5);

		CHECK(timespec_get(&tg, TIME_UTC) == TIME_UTC);
		CHECK(tg.tv_sec >= ts.tv_sec && tg.tv_sec - ts.tv_sec <= 5);
		CHECK(tg.tv_nsec >= 0 && tg.tv_nsec < 1000000000);
		CHECK(timespec_get(&tg, 12345) == 0);

		t3 = time(NULL);
		CHECK(t3 >= t1);

		/* monotonic: non-decreasing across many calls */
		CHECK(clock_gettime(CLOCK_MONOTONIC, &mono1) == 0);
		CHECK(mono1.tv_nsec >= 0 && mono1.tv_nsec < 1000000000);
		for (k = 0; k < 1000; k++) {
			CHECK(clock_gettime(CLOCK_MONOTONIC, &mono2) == 0);
			CHECK(mono2.tv_sec > mono1.tv_sec || (mono2.tv_sec == mono1.tv_sec && mono2.tv_nsec >= mono1.tv_nsec));
			mono1 = mono2;
		}
		CHECK(clock_gettime(CLOCK_MONOTONIC_RAW, &mono2) == 0);
		CHECK(clock_gettime(CLOCK_BOOTTIME, &mono2) == 0);
		CHECK(mono2.tv_sec > mono1.tv_sec || (mono2.tv_sec == mono1.tv_sec && mono2.tv_nsec >= mono1.tv_nsec));

		CHECK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu) == 0);
		CHECK(cpu.tv_sec >= 0 && cpu.tv_nsec >= 0 && cpu.tv_nsec < 1000000000);
		CHECK(cpu.tv_sec < 60);
		CHECK(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu) == 0);

		errno = 0;
		CHECK(clock_gettime(999, &ts) == -1);
		CHECK(errno == EINVAL);

		CHECK(clock_getres(CLOCK_REALTIME, &ts) == 0);
		CHECK(ts.tv_sec == 0 && ts.tv_nsec > 0);
		CHECK(clock_getres(CLOCK_MONOTONIC, &ts) == 0);
		CHECK(ts.tv_sec == 0 && ts.tv_nsec > 0);
		errno = 0;
		CHECK(clock_getres(999, &ts) == -1 && errno == EINVAL);

		{
			clockid_t id = -1;
			CHECK(clock_getcpuclockid(0, &id) == 0);
			CHECK(id == CLOCK_PROCESS_CPUTIME_ID);
			errno = 0;
			CHECK(clock_getcpuclockid(1234567, &id) == -1 && errno == ESRCH);
		}

		/* clock(): CPU time, non-decreasing, and in CLOCKS_PER_SEC units */
		{
			clock_t c1 = clock(), c2;
			volatile unsigned long sink = 0;
			unsigned long j;
			CHECK(c1 != (clock_t)-1);
			CHECK(c1 >= 0);
			for (j = 0; j < 5000000; j++) sink += j ^ (j >> 3);
			c2 = clock();
			CHECK(c2 >= c1);
			CHECK((c2 - c1) < 60 * CLOCKS_PER_SEC);
			CHECK(CLOCKS_PER_SEC == 1000000);
		}

		/* stime/clock_settime: an unprivileged process normally gets
		 * EPERM; set the clock to "now" so that even if it succeeds the
		 * host clock is left where it was */
		{
			time_t before = time(NULL), after, target = before;
			int r;
			errno = 0;
			r = stime(&target);
			after = time(NULL);
			CHECK(r == 0 || (r == -1 && errno != 0));
			CHECK(after >= before - 1 && after - before <= 5);
			errno = 0;
			CHECK(clock_settime(CLOCK_MONOTONIC, &ts) == -1 && errno == EINVAL);
		}
	}

	if (!fails) printf("time: all tests passed\n");
	return fails != 0;
}
