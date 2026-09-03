/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __attime_parse() -- src/util/attime.c, at(1p)'s TIME clause `timespec`
 * grammar, backing src/util/atbatch.c (`at`/`atd`/`batch`). Not a
 * __util_*_main() entry point: attime.h declares it with real external
 * linkage, included directly below.
 *
 * Grammar (attime.h's own header comment, DEVIATIONS section included):
 * `timespec := nowspec | time [date] [increment]`, `time` any of 24-hour
 * or wallclock-with-am/pm, glued or colon-separated, plus "noon"/
 * "midnight"; `date` a month name + day (+ year), a day-of-week,
 * "today"/"tomorrow", or this implementation's own CCYY-MM-DD ISO
 * extension; `increment` a '+' count and period, or "next" period.
 * timezone_name is a documented, deliberate gap (parsed as far as
 * `time`, then refused).
 *
 * __attime_parse() takes an already whitespace-split `words[]` array
 * (exactly at(1p)'s own argv words) rather than a raw string, so there's
 * no separate lexer to fuzz here. The fuzz buffer, after byte 0 (option
 * byte, below), is split on NUL into up to CAP_WORDS words of up to
 * CAP_SCRATCH-1 bytes each. An embedded NUL is the delimiter; two
 * consecutive ones legitimately produce an empty-string word, itself
 * meaningful input since parse_clocktime()'s and parse_date()'s own
 * `wlen == 0` / `strlen(w) < 3` guards are what an empty word reaches.
 *
 * Bits 0-1 of the option byte select one of four fixed `now` instants
 * (nowtab below), spread roughly a year apart so successive calls see
 * different weekdays, different days-in-month and both sides of a year
 * boundary. This matters because __attime_parse()'s "roll forward" rules
 * compare the parsed result against `now` itself, so varying `now`
 * against the same fuzzer-found words reaches both the "already passed,
 * roll forward" and "still in the future, don't" branches without the
 * fuzzer needing to encode a matching date. __attime_parse() takes `now`
 * as a plain argument and never calls time() itself, so no clock-freezing
 * seam is needed.
 *
 * Checked, per attime.h's documented contract:
 *
 *   - Return is -1 (malformed/empty timespec) or in [1, n] (words
 *     consumed), never > n. n == 0 must fail: `if (n <= 0) return -1;`
 *     is the function's first line.
 *   - On success, *out must actually have been written: poisoned to a
 *     sentinel before the call, checked to have changed afterwards.
 *   - On failure, *out must be UNCHANGED ("leaves *out untouched"),
 *     checked by confirming the sentinel survives a -1 return.
 *
 * No oracle: this implementation's documented scope, DEVIATIONS section
 * included, is itself the specification under test, not a narrower
 * guess a host `at` would disagree with by design.
 *
 * No spawn risk, no stdout/stderr output: attime.c (read in full) calls
 * no exec/system/popen and writes nothing to any stream -- a malformed
 * timespec is reported purely through the -1 return.
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
 * fuzz_getdate.c uses). */
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
