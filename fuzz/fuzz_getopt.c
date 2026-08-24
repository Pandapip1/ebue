/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getopt(), getopt_long() and getopt_long_only() -- src/misc/getopt.c
 * and src/misc/getopt_long.c.  A command-line parser is the one piece
 * of a C library whose input is, by definition, whatever the user
 * typed, and this one does three things that reward hostile input:
 * it walks an optstring looking for a character and for the ':' and
 * '::' that follow it, it walks a longopts table matching a prefix and
 * an optional "=value", and -- the part with real pointer arithmetic --
 * it PERMUTES the caller's argv so that non-option arguments end up
 * after the options.
 *
 * The permutation is why this harness builds a real argv rather than
 * calling getopt once.  src/misc/getopt_long.c's __getopt_long() moves
 * elements with a `permute` helper in a loop whose bound is computed
 * from three indices (skipped, resumed, optind); a fuzzer that supplies
 * an arbitrary mix of "-x", "--long=v", "--", "-" and bare words
 * explores that arithmetic in a way a fixed argv cannot.
 *
 * INPUT LAYOUT.  Byte 0 selects which of the three entry points is
 * driven; byte 1 is the number of longopts to build.  The rest is split
 * on NUL-free record boundaries into an optstring, the longopt names,
 * and the argv elements.  argv[0] is always a fixed program name, as
 * every real caller's is, so optind's initial value of 1 means what it
 * means everywhere else.
 *
 * opterr IS FORCED TO 0.  With it set, every unrecognised option writes
 * a diagnostic to fd 2 through __getopt_msg -> write(2, ...), which in
 * this build goes into fuzz/ntstubs.c's simulated volume and costs a
 * syscall shim per character class.  It is not the code under test
 * here, and leaving it on cuts throughput by an order of magnitude.
 * The message path is not lost: byte 0's high bit turns opterr back on
 * for one input in two, so __getopt_msg and its write(2, ...) are still
 * driven -- just not on every single input.
 *
 * WHAT IS ASSERTED.
 *
 *   - IT TERMINATES.  A parse loop that never returns -1 is a hang, and
 *     a hang is the failure mode this family of function has had in
 *     more than one C library.  The loop is bounded at 4*argc+16
 *     iterations and reports if it is still going.
 *   - optind stays within [0, argc] at every step.  It is the index the
 *     caller will use to read argv, so an out-of-range value is an
 *     out-of-bounds read in the *caller*, which no sanitizer here would
 *     see -- it has to be checked positively.
 *   - THE PERMUTATION LOSES NOTHING.  argv's elements after the parse
 *     must be a permutation of the elements before it: same multiset of
 *     pointers, no duplicate, no NULL introduced before argc.  That is
 *     the single strongest property of the permuting path and the one a
 *     miscomputed loop bound breaks.
 *   - A returned option character is one the optstring or longopts
 *     actually offered, or '?' or ':'; and when it is '?' or ':',
 *     optopt names something.
 *   - optarg, when set, points either into an argv element or is NULL.
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
	char *argv[MAXARGV + 1];
	char *before[MAXARGV + 1];
	struct option longopts[MAXLONG + 1];
	const char *optstring;
	int argc = 0, nlong = 0, which, i, iter, limit, err_on;
	size_t off = 0, n;

	if (size < 4) return 0;
	which = data[0] % 3;
	err_on = (data[0] & 0x80) != 0;         /* see the banner: opterr */
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

	limit = 4 * argc + 16;
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
