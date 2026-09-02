/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __attime_parse() -- src/util/attime.c, at(1p)'s TIME clause
 * `timespec` grammar, backing src/util/atbatch.c (`at`/`atd`/`batch`).
 * NOT a __util_*_main() entry point -- src/util/attime.h declares it
 * (`int __attime_parse(char *const *words, int n, time_t now, time_t
 * *out)`) with real external linkage, so, exactly as fuzz_modeparse.c's
 * and fuzz_crontime.c's own header comments explain for their targets,
 * this needs no #include of the .c file and no un-static-ing: attime.h
 * is included directly below.
 *
 * GRAMMAR. attime.h's own header comment (read in full first) gives
 * the whole grammar and its DEVIATIONS section: `timespec := nowspec |
 * time [date] [increment]`, `time` any of 24-hour or wallclock-with-
 * am/pm, glued or colon-separated, plus "noon"/"midnight"; `date` a
 * month name + day (+ year), a day-of-week, "today"/"tomorrow", or
 * this implementation's own CCYY-MM-DD ISO extension; `increment` a
 * '+' count and period, or "next" period. timezone_name is a
 * documented, deliberate gap (parsed as far as `time`, then refused).
 *
 * WORDS, NOT A STRING. __attime_parse() takes an already
 * whitespace-split `words[]` array (exactly at(1p)'s own argv words,
 * per attime.h's own comment) rather than a raw string -- the same
 * "no separate lexer, the caller already did the word-splitting" shape
 * fuzz_test.c's and fuzz_crontime.c's own header comments give their
 * own targets, for the identical reason. The fuzz buffer, after byte 0
 * (OPTION BYTE, below), is split on NUL into up to CAP_WORDS words of
 * up to CAP_SCRATCH-1 bytes each, scratch-owned and NUL-terminated,
 * the same tokenizing fuzz_test.c's own header comment describes in
 * more detail (an embedded NUL is the delimiter, two consecutive ones
 * legitimately produce an empty-string word, which is itself
 * meaningful input here: parse_clocktime()'s and parse_date()'s own
 * `wlen == 0` / `strlen(w) < 3` guards are what an empty word exists
 * to reach).
 *
 * OPTION BYTE. Bits 0-1 select one of four fixed `now` instants
 * (NOWTAB below), spread roughly a year apart so successive calls see
 * different weekdays, different days-in-month and both sides of a
 * year boundary -- relevant because __attime_parse()'s own "roll
 * forward" rules (attime.h's ROLLING A TIME THAT HAS ALREADY PASSED
 * FORWARD section) compare the parsed result against `now` itself, so
 * varying `now` against the same fuzzer-found words reaches both the
 * "already passed, roll forward" and "still in the future, don't"
 * branches of DATE_NONE/DATE_WEEKDAY/DATE_MONTHDAY without needing the
 * fuzzer to somehow encode a matching date itself. Not derived from
 * the fuzz input, for the same reason od's fixed fixture and
 * crontime's fixed NOWTAB are not: what varies here is `now`, held
 * fixed relative to the words under test, the same relationship
 * fuzz_getdate.c's frozen clock has to the date string it fuzzes
 * (though here there is no live clock to freeze -- __attime_parse()
 * takes `now` as a plain argument, never calling time() itself, so no
 * freezing seam is needed at all).
 *
 * WHAT IS CHECKED. attime.h's own documented contract, the same "the
 * file's own contract, not a looser guess" range assertion
 * fuzz_od.c's, fuzz_modeparse.c's and fuzz_crontime.c's own headers
 * give theirs:
 *
 *   - Return is -1 (malformed/empty timespec) or in [1, n] (words
 *     consumed) -- "always >= 1, never > n" is attime.h's own wording,
 *     checked verbatim.  n == 0 must fail: __attime_parse()'s own
 *     `if (n <= 0) return -1;` is the first line of the function.
 *   - On success, *out must actually have been written: poisoned to a
 *     sentinel before the call, checked to have changed afterwards.
 *   - On failure, *out must be UNCHANGED -- attime.h's own wording,
 *     "leaves *out untouched" -- checked by confirming the sentinel
 *     survives a -1 return.
 *
 * NO ORACLE. Like fuzz_modeparse.c's and fuzz_crontime.c's own headers
 * reason for their targets: this implementation's documented scope,
 * DEVIATIONS section included (no timezone_name, first-three-letters
 * name matching, the ISO extension and its own bare-date default-to-
 * midnight convention), is itself the specification under test, not a
 * narrower guess a host `at` would disagree with by design.
 *
 * NO SPAWN RISK, NO STDOUT/STDERR OUTPUT: attime.c (read in full)
 * calls no exec/system/popen and writes nothing to any stream -- like
 * crontime.c, a malformed timespec is reported purely through the -1
 * return, so no redirection is needed here at all.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/util/attime.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_WORDS 12
#define CAP_SCRATCH 256

/* Four fixed instants, ~47/133/289 days apart from an arbitrary,
 * deliberately non-round start (the same 2021-03-04T05:06:07Z instant
 * fuzz_getdate.c uses, for no reason beyond it already being a known
 * "not accidentally special" value) -- see OPTION BYTE above for why
 * these are fixed rather than fuzzer-derived. */
#define BASE_NOW ((time_t)1614834367LL)
static const time_t nowtab[4] = {
	BASE_NOW,
	BASE_NOW + 47  * 86400,
	BASE_NOW + 133 * 86400,
	BASE_NOW + 289 * 86400,
};

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char scratch[CAP_SCRATCH];
	char *words[CAP_WORDS];
	int nwords = 0;
	size_t si = 0, wi = 0;
	unsigned opts;
	time_t now, out;
	int rc;

	if (size < 1) return 0;
	opts = data[0];
	data++; size--;

	while (si < size && nwords < CAP_WORDS && wi < CAP_SCRATCH - 1) {
		size_t start = wi;

		while (si < size && data[si] != 0 && wi < CAP_SCRATCH - 1)
			scratch[wi++] = (char)data[si++];
		scratch[wi++] = 0;
		words[nwords++] = &scratch[start];

		if (si < size && data[si] == 0) si++;   /* consume the delimiter itself */
	}

	now = nowtab[opts & 0x03];
	out = (time_t)-2;   /* poison, distinct from the documented -1 failure sentinel */

	rc = __attime_parse(words, nwords, now, &out);

	if (rc == -1) {
		if (out != (time_t)-2)
			oracle_mismatch_i("__attime_parse wrote *out on a failed parse",
			                  nwords ? words[0] : "", (long long)out, -2);
	} else {
		if (rc < 1 || rc > nwords)
			oracle_mismatch_i("__attime_parse's consumed-word count escaped [1,n]",
			                  nwords ? words[0] : "", rc, nwords);
		if (out == (time_t)-2)
			oracle_mismatch_i("__attime_parse reported success without writing *out",
			                  nwords ? words[0] : "", (long long)out, -2);
	}

	return 0;
}
