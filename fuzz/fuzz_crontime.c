/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __crontime_parse() -- src/util/crontime.c, crontab(5)'s five-field
 * time expression (minute hour day-of-month month day-of-week), shared
 * by src/util/crontab.c and src/util/crond.c. NOT a __util_*_main()
 * entry point -- src/util/crontime.h declares it (and the two
 * functions it is built from, __crontime_parse_field() and
 * __crontime_matches()) with real external linkage, so, exactly as
 * fuzz_modeparse.c's own header comment explains for its target, this
 * needs no #include of the .c file and no un-static-ing: crontime.h is
 * included directly below.
 *
 * GRAMMAR. crontime.h's own header comment (read in full first) gives
 * the whole grammar: each field is "*", a number, a name (month field:
 * jan-dec; day-of-week field: sun-sat, matched case-insensitively on
 * the first three letters), a hyphen range of either, an optional
 * "/step" on either an asterisk or a range, and any of the above
 * comma-separated. Field ranges: minute 0-59, hour 0-23, day-of-month
 * 1-31, month 1-12, day-of-week 0-7 (0 and 7 both Sunday).
 *
 * INPUT LAYOUT. Byte 0 is OPTION BYTE (below); the rest is split on
 * NUL into up to 5 fields -- min/hour/dom/month/dow, in that order,
 * the same "no separate lexer, the caller already did the
 * word-splitting" shape fuzz_test.c's own header comment gives its
 * own argv tokenizing, for the identical underlying reason: crond.c
 * and crontab.c both split a crontab line on <blank> before ever
 * calling __crontime_parse(), so this is genuinely what a real caller
 * hands it, not an invented shortcut. A field never supplied (fewer
 * than 4 NUL bytes in the input) defaults to "*" -- itself a valid,
 * always-true field -- rather than an empty string, a deliberate
 * choice documented here rather than left implicit: it keeps whichever
 * fields the fuzzer DID write the ones actually deciding the parse's
 * outcome, the same reasoning fuzz_od.c's own header gives for using a
 * fixed fixture instead of fuzzer-derived file content -- the grammar
 * under test is each field's own syntax, not "how many fields were
 * present", and defaulting the rest to always-valid keeps that the
 * deciding factor five times out of five rather than the parse failing
 * on a missing field before ever reaching the interesting one. Each
 * field is capped at FIELD_CAP bytes.
 *
 * OPTION BYTE. Bits 0-1 select one of four fixed `struct tm` instants
 * (NOWTAB below) that a successful parse is then checked against via
 * __crontime_matches() -- see WHAT IS CHECKED. Not derived from the
 * fuzz input, for the same reason od's fixed fixture is not: the
 * grammar under test is the field syntax, and NOWTAB exists only to
 * give __crontime_matches() something concrete to answer about, with
 * four fixed instants chosen to spread across the full range of each
 * of minute/hour/day-of-month/month/day-of-week rather than clustering
 * near one corner of all five at once.
 *
 * WHAT IS CHECKED. crontime.h's own documented contract for both
 * functions under test here, narrowed the same "the file's own
 * contract, not a looser guess" way fuzz_od.c's and fuzz_modeparse.c's
 * own headers narrow theirs:
 *
 *   __crontime_parse() returns 0 on success or -1 on any malformed
 *   field -- no third outcome, checked directly.
 *
 *   __crontime_matches(), on a successful parse, is a boolean over
 *   arrays whose every entry crontime.c's own memset()+`out[v] = 1`
 *   pattern (in __crontime_parse_field()) can only ever set to 0 or 1
 *   -- so its return value must be exactly 0 or 1. A value outside
 *   that set means one of those arrays picked up a value neither
 *   memset() nor the assignment ever wrote, i.e. memory outside the
 *   fields crontime.c itself allocates and owns.
 *
 * NO ORACLE. Like fuzz_modeparse.c's own header gives for chmod(1p)'s
 * mode grammar: this file's documented, exact scope (the real
 * crontab(5) grammar as crontime.h's header describes fetching it,
 * with "@reboot"-style nicknames and MAILTO= lines a documented,
 * deliberate gap) is itself the specification under test, not a
 * narrower guess a host crontab would disagree with by design.
 *
 * NO SPAWN RISK, NO STDOUT/STDERR OUTPUT: crontime.c (read in full)
 * calls no exec/system/popen and writes nothing to any stream --
 * unlike modeparse.c's __util_diagf() or od.c's diagnostics, a
 * malformed field is reported purely through the -1 return, so no
 * redirection is needed here at all.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/util/crontime.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define NFIELDS 5
#define FIELD_CAP 16

/* Four fixed instants -- see OPTION BYTE above for why these are not
 * derived from the fuzz input. Spread across the full range of each
 * field: an ordinary Tuesday noon-ish minute, a Friday midnight
 * year-end, a leap-day Sunday just before midnight, and a Friday
 * just after New Year's -- between them every one of minute/hour/
 * day-of-month/month/day-of-week visits a value nowhere near any
 * other entry's value for that same field. */
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
