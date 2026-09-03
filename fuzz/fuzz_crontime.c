/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __crontime_parse() -- src/util/crontime.c, crontab(5)'s five-field
 * time expression (minute hour day-of-month month day-of-week), shared
 * by src/util/crontab.c and src/util/crond.c. Not a __util_*_main() entry
 * point: crontime.h declares it (and __crontime_parse_field()/
 * __crontime_matches()) with real external linkage, included directly.
 *
 * Grammar (crontime.h's own header comment): each field is "*", a
 * number, a name (month: jan-dec; day-of-week: sun-sat, matched
 * case-insensitively on the first three letters), a hyphen range of
 * either, an optional "/step" on either an asterisk or a range, and any
 * of the above comma-separated. Field ranges: minute 0-59, hour 0-23,
 * day-of-month 1-31, month 1-12, day-of-week 0-7 (0 and 7 both Sunday).
 *
 * Byte 0 is the option byte (below); the rest is split on NUL into up to
 * 5 fields -- min/hour/dom/month/dow, in that order, matching how
 * crond.c and crontab.c actually split a crontab line before calling
 * __crontime_parse(). A field never supplied defaults to "*" (itself a
 * valid, always-true field) rather than an empty string, so whichever
 * fields the fuzzer did write stay the ones deciding the parse outcome.
 * Each field is capped at FIELD_CAP bytes.
 *
 * Bits 0-1 of the option byte select one of four fixed `struct tm`
 * instants (nowtab below) that a successful parse is checked against via
 * __crontime_matches() -- not derived from the fuzz input, since the
 * grammar under test is the field syntax; nowtab exists only to give
 * __crontime_matches() something concrete to answer about.
 *
 * Checked: __crontime_parse() returns 0 or -1, no third outcome.
 * __crontime_matches(), on a successful parse, is a boolean over arrays
 * whose every entry crontime.c's own memset()+`out[v] = 1` pattern can
 * only ever set to 0 or 1 -- a value outside that set means one of those
 * arrays picked up memory outside what crontime.c itself owns.
 *
 * No oracle: this file's documented, exact scope ("@reboot"-style
 * nicknames and MAILTO= lines a deliberate gap) is itself the
 * specification under test, not a narrower guess a host crontab would
 * disagree with by design.
 *
 * No spawn risk, no stdout/stderr output: crontime.c (read in full)
 * calls no exec/system/popen and writes nothing to any stream -- a
 * malformed field is reported purely through the -1 return.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/util/crontime.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define NFIELDS 5
#define FIELD_CAP 16

/* Spread across the full range of each field, so between them every one
 * of minute/hour/day-of-month/month/day-of-week visits a value nowhere
 * near any other entry's value for that same field. */
static const struct tm nowtab[4] = {
	{ .tm_min = 30, .tm_hour = 12, .tm_mday = 1,  .tm_mon = 8,  .tm_wday = 2 },
	{ .tm_min = 0,  .tm_hour = 0,  .tm_mday = 25, .tm_mon = 11, .tm_wday = 5 },
	{ .tm_min = 59, .tm_hour = 23, .tm_mday = 29, .tm_mon = 1,  .tm_wday = 0 },
	{ .tm_min = 15, .tm_hour = 6,  .tm_mday = 1,  .tm_mon = 0,  .tm_wday = 5 },
};

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opts;
	char buf[NFIELDS][FIELD_CAP + 1];
	const char *fields[NFIELDS];
	size_t fi, wi, si;
	struct crontime out;
	int rc;

	if (size < 1) return 0;
	opts = data[0];
	data++; size--;

	for (fi = 0; fi < NFIELDS; fi++) fields[fi] = "*";

	fi = 0; wi = 0;
	for (si = 0; si < size && fi < NFIELDS; si++) {
		if (data[si] == 0) {
			buf[fi][wi] = 0;
			fields[fi] = buf[fi];
			fi++; wi = 0;
			continue;
		}
		if (wi < FIELD_CAP) buf[fi][wi++] = (char)data[si];
	}
	if (fi < NFIELDS && wi > 0) {
		buf[fi][wi] = 0;
		fields[fi] = buf[fi];
	}

	rc = __crontime_parse(fields[0], fields[1], fields[2], fields[3], fields[4], &out);
	if (rc != 0 && rc != -1)
		oracle_mismatch_i("__crontime_parse returned outside {0,-1}", fields[0], rc, 0);

	if (rc == 0) {
		int m = __crontime_matches(&out, &nowtab[opts & 0x03]);
		if (m != 0 && m != 1)
			oracle_mismatch_i("__crontime_matches returned outside {0,1}", fields[0], m, 1);
	}

	return 0;
}
