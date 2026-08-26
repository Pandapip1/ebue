/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The calendar half of src/time/: tzset.c, mktime.c, timegm.c,
 * gmtime.c, localtime.c, asctime.c and ctime.c.  strftime and strptime
 * already have harnesses (fuzz_strftime, fuzz_strptime); everything
 * they call into did not.
 *
 * WHY THIS SUBSYSTEM.  tzset() parses $TZ, and $TZ is untrusted input
 * in the ordinary case, not the exotic one: it is an environment
 * variable, so it arrives from whoever started the process, and it is
 * read implicitly -- localtime(), ctime() and mktime() all call tzset()
 * themselves, so a program that never mentions timezones at all still
 * runs this parser over whatever $TZ contained.  The rest of the module
 * is integer arithmetic on caller-supplied `struct tm` fields that
 * mktime() and timegm() are REQUIRED to accept out of range and
 * normalize (tm_mon == 13, tm_mday == 0, negative tm_sec), which is
 * exactly the shape of code that overflows.
 *
 * INPUT LAYOUT.  Byte 0 is flags; bytes 1..24 are six little-endian
 * int32 struct-tm fields; bytes 25..32 are a little-endian time_t.
 * Everything after that is the $TZ string.  A short input is
 * zero-padded rather than rejected, so the empty input and the
 * one-byte input both run the whole harness with zeroes -- which is
 * also the trivial-input smoke test.
 *
 * WHAT IS ASSERTED, and what deliberately is not.
 *
 *   - timegm() AND gmtime_r() ARE ORACLED against glibc's.  They are
 *     the two functions here whose answer depends on nothing but their
 *     arguments -- pure proleptic-Gregorian arithmetic on UTC, no zone
 *     database, no $TZ, no "now" -- so a disagreement is a calendar
 *     bug in one of the two and cannot be a configuration difference.
 *     See the block comment above host_timegm() in fuzz/host_oracle.c.
 *
 *     mktime() and localtime() are NOT oracled, for the same reason
 *     stated positively: glibc reads /usr/share/zoneinfo and applies
 *     POSIX DST rules, and src/time/tzset.c documents that this target
 *     has neither and that `daylight` is always 0.  A differential
 *     check there would report that documented difference over and
 *     over and would be evidence about nothing.
 *
 *   - THE ORACLE RUNS ON RESTRICTED FIELDS.  The comparison is made
 *     with fields folded into ranges (year +-4000 or so, month +-24,
 *     day +-60, ...) that are far out of range enough to drive every
 *     normalization branch, but nowhere near overflowing either
 *     library's arithmetic.  This is not the oracle being spared the
 *     hard cases: at the extremes the two libraries make different and
 *     both-defensible choices about a result POSIX only says "shall
 *     return (time_t)-1 ... [EOVERFLOW]" about, so a mismatch there
 *     would not be evidence of a defect.  The extremes are still
 *     driven, by the unrestricted pass below -- just by ASan and UBSan
 *     rather than by comparison.
 *
 *   - mktime() IS IDEMPOTENT.  mktime() normalizes *tm in place and
 *     returns the instant; feeding the normalized struct back in must
 *     give the same instant.  That is a property of the POSIX contract
 *     ("the original values of the other components are not restricted
 *     to the ranges described in <time.h>", and the returned tm is the
 *     normalized one), not of any particular implementation, so it is
 *     checkable without an oracle.  It is checked only when `timezone`
 *     is small enough that the shift cannot itself overflow -- an
 *     overflow there is a separate finding, and UBSan reports it
 *     directly rather than as a failed idempotence.
 *
 *   - asctime_r() FITS 26 BYTES.  asctime.html: "shall place the
 *     result in a user-supplied buffer of at least 26 bytes".  The
 *     buffer is therefore malloc'd at exactly 26 so ASan sees a 27th
 *     byte, and it is used only when the struct tm really is a valid
 *     time with a four-digit year -- the case the 26-byte guarantee is
 *     about.  Every other tm gets a 64-byte buffer, so an overrun
 *     beyond even that is still caught without the harness claiming a
 *     guarantee POSIX does not make.
 *
 *   - tzname[] IS A VALID PAIR.  Both entries non-NULL, NUL-terminated
 *     within the 32-byte buffer tzset.c allocates for them, after any
 *     $TZ at all.
 *
 * WHAT IS NOT HERE.  getdate() (src/time/getdate.c) is deliberately
 * left out even though it is the most parser-shaped function in the
 * module: it calls time(0) and seeds the working struct tm with
 * localtime_r()'s idea of "now", so the same input does not give the
 * same behaviour twice, and a crash it found might not reproduce.  A
 * harness for it needs a seam that makes "now" fixed, which is a change
 * to fuzz/ntstubs.c's clock, and that is its own piece of work.
 */
/* timegm() is a BSD/GNU extension, and include/time.h declares it only
 * under _BSD_SOURCE or _GNU_SOURCE.  The Makefile compiles every harness
 * with -D_XOPEN_SOURCE=700, which does not include it -- so ask for it
 * here rather than widening the flags for every other harness. */
#define _BSD_SOURCE
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);
extern long long host_timegm(const int in[6], int *ok);
extern int host_gmtime(long long t, int out[8]);

#define BIN 32

static int rd32(const unsigned char *p)
{
	unsigned u = (unsigned)p[0] | ((unsigned)p[1] << 8) |
	             ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
	return (int)u;
}

static long long rd64(const unsigned char *p)
{
	unsigned long long u = 0;
	int i;
	for (i = 7; i >= 0; i--) u = (u << 8) | p[i];
	return (long long)u;
}

/* Fold v into [lo, hi] without a modulus that could be by zero, and
 * without depending on C's sign rules for %: the span is at most a few
 * thousand here, so an unsigned remainder is exact and total. */
static int fold(int v, int lo, int hi)
{
	unsigned span = (unsigned)(hi - lo) + 1u;
	return lo + (int)((unsigned)v % span);
}

/* The harness's own decimal formatter.  Not ntlibc's snprintf, which is
 * itself under test, and not the host's, which this file cannot reach
 * (only fuzz/host_oracle.c is compiled against the host headers). */
static char *put_i(char *p, char *end, long long v)
{
	char tmp[24];
	int n = 0;
	unsigned long long u = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
	if (v < 0 && p < end) *p++ = '-';
	do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
	while (n-- > 0 && p < end) *p++ = tmp[n];
	return p;
}

static void describe(char *buf, size_t cap, const int f[6], const char *tz)
{
	char *p = buf, *end = buf + cap - 1;
	static const char *const names[6] = { "y=", " mon=", " mday=", " hour=", " min=", " sec=" };
	int i;
	for (i = 0; i < 6; i++) {
		const char *n = names[i];
		while (*n && p < end) *p++ = *n++;
		p = put_i(p, end, f[i]);
	}
	if (p < end) *p++ = ' ';
	if (p < end) *p++ = 'T';
	if (p < end) *p++ = 'Z';
	if (p < end) *p++ = '=';
	while (*tz && p < end) *p++ = *tz++;
	*p = 0;
}

static void load(struct tm *tm, const int f[6])
{
	memset(tm, 0, sizeof *tm);
	tm->tm_year = f[0];
	tm->tm_mon  = f[1];
	tm->tm_mday = f[2];
	tm->tm_hour = f[3];
	tm->tm_min  = f[4];
	tm->tm_sec  = f[5];
	tm->tm_isdst = 0;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned char bin[BIN];
	char tz[192], desc[320];
	int raw[6], f[6], i;
	unsigned flags;
	long long tval;
	size_t n;
	struct tm tm, g;
	time_t t;

	memset(bin, 0, sizeof bin);
	n = size < BIN ? size : BIN;
	memcpy(bin, data, n);
	if (size > BIN) { data += BIN; size -= BIN; } else size = 0;

	flags = bin[0];
	for (i = 0; i < 6; i++) raw[i] = rd32(bin + 1 + 4 * i);
	tval = rd64(bin + 24);        /* bytes 24..31; 1..24 were the six fields */

	/* The $TZ record.  Embedded NULs would truncate it at the first
	 * one and waste the rest of the input, so they become '_': every
	 * byte the fuzzer supplies then reaches the parser. */
	n = size < sizeof tz - 1 ? size : sizeof tz - 1;
	memcpy(tz, data, n);
	tz[n] = 0;
	for (i = 0; i < (int)n; i++) if (!tz[i]) tz[i] = '_';

	/* ------------------------------------------------ tzset / $TZ */
	if (flags & 1) unsetenv("TZ");
	else setenv("TZ", tz, 1);
	tzset();
	if (!tzname[0] || !tzname[1])
		oracle_mismatch_i("tzset left a NULL in tzname", tz, 0, 1);
	else if (strlen(tzname[0]) > 31 || strlen(tzname[1]) > 31)
		oracle_mismatch_i("tzset produced an over-long tzname", tz,
		                  (long long)strlen(tzname[0]), 31);

	/* ------------------------------------- timegm/gmtime, oracled */
	f[0] = fold(raw[0], -5900, 4100);      /* tm_year: years -4000..6000 */
	f[1] = fold(raw[1], -24, 35);
	f[2] = fold(raw[2], -60, 90);
	f[3] = fold(raw[3], -48, 71);
	f[4] = fold(raw[4], -120, 180);
	f[5] = fold(raw[5], -120, 180);
	describe(desc, sizeof desc, f, tz);

	{
		int ok = 0;
		long long want = host_timegm(f, &ok);
		load(&tm, f);
		t = timegm(&tm);
		if (ok && (long long)t != want)
			oracle_mismatch_i("timegm", desc, (long long)t, want);

		/* timegm() normalizes *tm on the way out, so the fields it
		 * left behind must describe the very instant it returned:
		 * feeding them back in is the round trip. */
		{
			int back[6];
			struct tm again;
			back[0] = tm.tm_year; back[1] = tm.tm_mon; back[2] = tm.tm_mday;
			back[3] = tm.tm_hour; back[4] = tm.tm_min; back[5] = tm.tm_sec;
			long long re;
			load(&again, back);
			re = (long long)timegm(&again);
			if (re != (long long)t)
				oracle_mismatch_i("timegm is not idempotent", desc, re, (long long)t);
		}

		/* gmtime_r on the instant timegm just produced. */
		{
			int hg[8];
			if (host_gmtime((long long)t, hg) && gmtime_r(&t, &g)) {
				if (g.tm_year != hg[0]) oracle_mismatch_i("gmtime tm_year", desc, g.tm_year, hg[0]);
				if (g.tm_mon  != hg[1]) oracle_mismatch_i("gmtime tm_mon",  desc, g.tm_mon,  hg[1]);
				if (g.tm_mday != hg[2]) oracle_mismatch_i("gmtime tm_mday", desc, g.tm_mday, hg[2]);
				if (g.tm_hour != hg[3]) oracle_mismatch_i("gmtime tm_hour", desc, g.tm_hour, hg[3]);
				if (g.tm_min  != hg[4]) oracle_mismatch_i("gmtime tm_min",  desc, g.tm_min,  hg[4]);
				if (g.tm_sec  != hg[5]) oracle_mismatch_i("gmtime tm_sec",  desc, g.tm_sec,  hg[5]);
				if (g.tm_wday != hg[6]) oracle_mismatch_i("gmtime tm_wday", desc, g.tm_wday, hg[6]);
				if (g.tm_yday != hg[7]) oracle_mismatch_i("gmtime tm_yday", desc, g.tm_yday, hg[7]);
			}
		}
	}

	/* gmtime_r on the input's own time_t, folded to the range glibc
	 * will also answer for, so the two can be compared there too. */
	{
		long long tt = (long long)fold((int)tval, -2000000000, 2000000000);
		time_t t2 = (time_t)tt;
		int hg[8];
		if (host_gmtime(tt, hg) && gmtime_r(&t2, &g)) {
			if (g.tm_year != hg[0] || g.tm_mon != hg[1] || g.tm_mday != hg[2] ||
			    g.tm_hour != hg[3] || g.tm_min != hg[4] || g.tm_sec != hg[5] ||
			    g.tm_wday != hg[6] || g.tm_yday != hg[7])
				oracle_mismatch_i("gmtime_r of a raw time_t", desc,
				                  (long long)g.tm_year, (long long)hg[0]);
			/* And back: gmtime_r's fields describe that instant. */
			{
				struct tm r = g;
				if ((long long)timegm(&r) != tt)
					oracle_mismatch_i("gmtime_r -> timegm round trip", desc,
					                  (long long)timegm(&r), tt);
			}
		}
	}

	/* -------------------------------- mktime, localtime, idempotence */
	if (timezone > -1000000L && timezone < 1000000L) {
		struct tm m1, m2;
		time_t a, b;
		load(&m1, f);
		a = mktime(&m1);
		if (a != (time_t)-1) {
			m2 = m1;
			b = mktime(&m2);
			if (b != a)
				oracle_mismatch_i("mktime is not idempotent", desc,
				                  (long long)b, (long long)a);
			/* localtime_r of the instant mktime returned must be the
			 * struct mktime left behind: they are defined to be the
			 * same operation (mktime.c ends by calling it). */
			if (localtime_r(&a, &m2)) {
				if (m2.tm_year != m1.tm_year || m2.tm_mon != m1.tm_mon ||
				    m2.tm_mday != m1.tm_mday || m2.tm_hour != m1.tm_hour ||
				    m2.tm_min != m1.tm_min || m2.tm_sec != m1.tm_sec)
					oracle_mismatch_i("mktime's tm disagrees with localtime_r", desc,
					                  m2.tm_year, m1.tm_year);
			}
		}
	}

	/* -------------------------------------------- asctime_r / ctime_r
	 *
	 * The 26-byte buffer is the POSIX guarantee and is used only where
	 * POSIX makes it: a valid time whose year has four digits.  ASan
	 * owns the 27th byte. */
	{
		struct tm v;
		load(&v, f);
		timegm(&v);         /* normalize, so tm_wday/tm_yday are set */
		{
			int y = v.tm_year + 1900;
			int fits = y >= 1000 && y <= 9999 &&
			           (unsigned)v.tm_wday < 7 && (unsigned)v.tm_mon < 12 &&
			           v.tm_mday >= 1 && v.tm_mday <= 31 &&
			           (unsigned)v.tm_hour < 24 && (unsigned)v.tm_min < 60 &&
			           (unsigned)v.tm_sec < 61;
			size_t cap = fits ? 26 : 64;
			char *buf = malloc(cap);
			if (buf) {
				asctime_r(&v, buf);
				if (strlen(buf) >= cap)
					oracle_mismatch_i("asctime_r overran its buffer", desc,
					                  (long long)strlen(buf), (long long)cap);
				free(buf);
			}
		}
	}
	{
		time_t ct = (time_t)fold((int)tval, -2000000000, 2000000000);
		struct tm lt;
		if (localtime_r(&ct, &lt)) {
			int y = lt.tm_year + 1900;
			size_t cap = (y >= 1000 && y <= 9999) ? 26 : 64;
			char *buf = malloc(cap);
			if (buf) {
				if (ctime_r(&ct, buf) && strlen(buf) >= cap)
					oracle_mismatch_i("ctime_r overran its buffer", desc,
					                  (long long)strlen(buf), (long long)cap);
				free(buf);
			}
		}
	}

	/* ------------------------------- the unrestricted pass, no oracle
	 *
	 * Whole-int32 fields straight from the input: the arithmetic
	 * mktime.c and timegm.c widen to `long long` precisely so that a
	 * caller's extreme value cannot overflow an `int`.  Nothing is
	 * compared here -- ASan and UBSan are the assertion. */
	if (flags & 2) {
		struct tm w;
		load(&w, raw);
		timegm(&w);
		load(&w, raw);
		mktime(&w);
	}
	return 0;
}
