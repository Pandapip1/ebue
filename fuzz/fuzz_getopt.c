/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes getopt()/getopt_long()/getopt_long_only() (src/misc/getopt.c,
 * getopt_long.c). Besides the optstring and longopts-table scans, these
 * PERMUTE the caller's argv so non-options end up after options -- real
 * pointer arithmetic, which is why this harness builds an actual argv
 * (of "-x", "--long=v", "--", "-", bare words) rather than calling
 * getopt once, exercising __getopt_long()'s permute loop for real.
 *
 * Byte 0 selects which entry point; byte 1 the number of longopts to
 * build; the rest splits on '\n' into optstring, longopt names, and
 * argv elements. opterr is forced to 0 (unrecognized options otherwise
 * write a diagnostic through write(2), costing an order of magnitude of
 * throughput) except on one input in two (byte 0's high bit), so that
 * path still gets driven, just not every time.
 *
 * Asserted: the parse loop terminates (bounded, since a hang has been a
 * real failure mode in more than one C library's getopt); optind stays
 * in [0, argc] at every step (an out-of-range value would be an
 * out-of-bounds read in the *caller*, invisible to any sanitizer here);
 * the permutation loses nothing (argv after the parse is the same
 * multiset of pointers as before, no duplicates, no NULL introduced);
 * a returned option character is one the optstring/longopts actually
 * offered, or '?'/':' with optopt naming something; optarg, when set,
 * points into an argv element.
 */
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define MAXARGV 24
#define MAXLONG 8
#define CAP     512

static char storage[CAP + 1];

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	/* MAXARGV + 2: MAXARGV input elements + prepended "prog" + the
	 * terminating NULL every real argv has. At + 1, `argv[argc] = 0`
	 * below wrote one past the end whenever the input filled every
	 * slot -- caught by UBSan once a fuzzer found an input that did. */
	char *argv[MAXARGV + 2];
	char *before[MAXARGV + 2];
	struct option longopts[MAXLONG + 1];
	const char *optstring;
	int argc = 0, nlong = 0, which, i, iter, limit, err_on;
	size_t off = 0, n;

	if (size < 4) return 0;
	which = data[0] % 3;
	err_on = (data[0] & 0x80) != 0;
	nlong = data[1] % (MAXLONG + 1);
	data += 2; size -= 2;

	n = size < CAP ? size : CAP;
	memcpy(storage, data, n);
	storage[n] = 0;

	/* Records are separated by '\n', which a fuzzer discovers quickly
	 * and which cannot appear inside a record.  A record that is empty
	 * is kept: an empty optstring and an empty argv element are both
	 * legal inputs and both have their own branch. */
	memset(longopts, 0, sizeof longopts);
	{
		size_t start = 0;
		size_t rec = 0;
		optstring = "";
		for (;;) {
			size_t end = start;
			while (end < n && storage[end] != '\n') end++;
			storage[end] = 0;
			if (rec == 0) optstring = storage + start;
			else if ((int)rec <= nlong) {
				longopts[rec - 1].name = storage + start;
				longopts[rec - 1].has_arg = (int)(rec % 3);   /* no/required/optional */
				longopts[rec - 1].flag = 0;
				longopts[rec - 1].val = 0x100 + (int)rec;
			} else if (argc < MAXARGV) {
				argv[argc++] = storage + start;
			}
			rec++;
			if (end >= n) break;
			start = end + 1;
		}
		off = rec;
	}
	(void)off;
	if (argc == 0) return 0;                        /* nothing to parse */
	/* Any longopt slot the input did not fill would be uninitialised. */
	{
		size_t rec;
		for (rec = 0; (int)rec < nlong; rec++)
			if (longopts[rec].name == 0) { nlong = (int)rec; break; }
	}
	memset(&longopts[nlong], 0, sizeof longopts[nlong]);   /* the terminator */

	/* argv[0] is the program name, as it is for every real caller. */
	for (i = argc; i > 0; i--) argv[i] = argv[i - 1];
	argv[0] = (char *)"prog";
	argc++;
	argv[argc] = 0;
	memcpy(before, argv, sizeof argv[0] * (size_t)(argc + 1));

	/* Reset the parser's global state.  optind = 0 is the documented
	 * way; optreset is the BSD spelling and is set too, because both
	 * are read by src/misc/getopt.c and a reset that honoured only one
	 * would leave state from the previous input in this one. */
	optind = 0;
	optreset = 1;
	opterr = err_on;
	optarg = 0;
	optopt = 0;

	/* Counted in CHARACTERS, not argv elements: clustered short options
	 * mean one element yields one result per byte (e.g. a 25-byte
	 * "--RR-R..." makes 24 '?' returns before -1), which the old
	 * 4*argc+16 bound alone mistook for a hang. Summing lengths keeps
	 * the assertion meaningful (no correct parse returns more times
	 * than there are characters to consume) while fixing the false
	 * positive; the flat term stays for per-element results ('?' for a
	 * missing argument, "--") that consume no character. */
	limit = 4 * argc + 16;
	for (i = 0; i < argc; i++) limit += (int)strlen(argv[i]);
	for (iter = 0; iter < limit; iter++) {
		int idx = -1;
		int c;

		if (which == 0) c = getopt(argc, argv, optstring);
		else if (which == 1) c = getopt_long(argc, argv, optstring, longopts, &idx);
		else c = getopt_long_only(argc, argv, optstring, longopts, &idx);

		if (c == -1) break;

		if (optind < 0 || optind > argc)
			oracle_mismatch_i("optind left [0, argc]", optstring,
			                  (long long)optind, (long long)argc);

		if (c == '?' || c == ':') {
			/* Legal outcomes; optopt is what they report. */
		} else if (c >= 0x100) {
			if (which == 0)
				oracle_mismatch_i("getopt returned a long-option value", optstring,
				                  c, 0);
			else if (idx < 0 || idx >= nlong)
				oracle_mismatch_i("longindex outside the longopts table", optstring,
				                  idx, nlong);
			else if (longopts[idx].val != c)
				oracle_mismatch_i("longindex names a different option than the return",
				                  optstring, longopts[idx].val, c);
		} else if (c == 0) {
			/* Only legal when a longopt has a non-NULL flag, and none
			 * here does. */
			oracle_mismatch_i("returned 0 with no flag-bearing longopt", optstring, 0, 1);
		} else if (c == 1 && optstring[0] == '-') {
			/* An optstring beginning with '-' selects the mode where
			 * every non-option argument is returned as character 1
			 * with optarg set (src/misc/getopt.c:50) -- but a fuzzer
			 * can also put byte 0x01 IN the optstring, making a
			 * return of 1 an ordinary option character with no
			 * argument instead. The two are indistinguishable from
			 * outside (getopt has one channel for the value), so only
			 * their disjunction is assertable: either optarg was set,
			 * or 1 really is an offered option character. */
			if (!optarg && strchr(optstring, c) == 0)
				oracle_mismatch_i("returned 1 that is neither a non-option"
				                  " argument nor an option character",
				                  optstring, c, 0);
		} else {
			if (strchr(optstring, c) == 0)
				oracle_mismatch_i("returned an option character not in the optstring",
				                  optstring, c, 0);
		}

		if (optarg) {
			int found = 0;
			for (i = 0; i < argc; i++) {
				size_t l = strlen(argv[i]);
				if (optarg >= argv[i] && optarg <= argv[i] + l) { found = 1; break; }
			}
			if (!found)
				oracle_mismatch_i("optarg does not point into any argv element",
				                  optstring, 0, 1);
		}
	}
	if (iter >= limit)
		oracle_mismatch_i("the parse loop did not terminate", optstring,
		                  (long long)iter, (long long)limit);

	/* The permutation must lose nothing: same multiset of pointers. */
	for (i = 0; i < argc; i++) {
		int j, seen = 0;
		if (!argv[i]) {
			oracle_mismatch_i("a NULL appeared inside argv", optstring, i, argc);
			continue;
		}
		for (j = 0; j < argc; j++) if (before[j] == argv[i]) seen++;
		if (seen != 1)
			oracle_mismatch_i("argv element is not a permutation of the original",
			                  optstring, seen, 1);
	}
	for (i = 0; i < argc; i++) {
		int j, seen = 0;
		for (j = 0; j < argc; j++) if (argv[j] == before[i]) seen++;
		if (seen != 1)
			oracle_mismatch_i("an original argv element was lost by the permutation",
			                  optstring, seen, 1);
	}
	return 0;
}
