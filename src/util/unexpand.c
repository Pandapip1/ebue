/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * unexpand(1p): `unexpand [-a | -t tablist] [file...]`.  DESCRIPTION:
 * "copy files or standard input to standard output, converting <blank>
 * characters at the beginning of each line into the maximum number of
 * <tab> characters followed by the minimum number of <space> characters
 * needed to fill the same column positions" -- that conversion always
 * runs on each line's leading run of <blank> (space/tab) characters,
 * with no minimum run length.
 *
 * -a: "In addition to translating <blank> characters at the beginning
 * of each line, translate all sequences of two or more <blank>
 * characters immediately preceding a tab stop" -- everywhere else in
 * the line too, but *only* runs of two or more; a single mid-line blank
 * is left alone even if it happens to land exactly on a tab stop.
 *
 * -t tablist (src/util/tablist.c/.h, shared with src/util/expand.c's own
 * -t): "When -t is specified, the presence or absence of the -a option
 * shall be ignored; conversion shall not be limited to the processing
 * of leading <blank> characters" -- -t alone therefore behaves like
 * `-a -t tablist`, not like plain -t with -a's "two or more" restriction
 * un-lifted; effective_a below is opt_a || have_t for exactly that
 * reason. This build accepts -a and -t together too (not just as
 * alternatives, despite the SYNOPSIS's "|"): the standard's own words
 * for what -t does already say -a's presence "shall be ignored", so
 * accepting both together produces the identical behaviour -t alone
 * does, and refusing the combination outright would only make a
 * harmless, self-consistent invocation an error for no behavioural
 * reason.
 *
 * DESCRIPTION on backspace: "Each <backspace> shall be copied to the
 * output, and shall cause the column position count for tab
 * calculations to be decremented; the count shall never be decremented
 * to a value less than one." -- same `col` convention as
 * src/util/expand.c (1-based, next character's column), floor 1 here
 * matches that sentence directly (expand.c's own floor-zero sentence is
 * the same physical rule under expand.c's own col-minus-one framing;
 * see that file's header).
 *
 * A run being "converted" always re-derives its own end column by
 * walking its original characters (tabs advance to their own next
 * stop, spaces by one) before emitting the maximal-tabs/minimal-spaces
 * replacement -- so a run that already produces no visible change (an
 * insufficient run that cannot reach a further stop) really does come
 * back out unchanged, matching DESCRIPTION's "insufficient blanks
 * remain as spaces" without a separate code path for that case.
 *
 * EXIT STATUS: "0 Successful completion. >0 An error occurred."
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "util.h"
#include "tablist.h"

static int is_blank(char c) { return c == ' ' || c == '\t'; }

/* Walks `len` original blank characters starting at column `*col`,
 * emits the maximal-tabs/minimal-spaces replacement for the columns
 * they span, and advances `*col` to the run's end column. */
static void emit_converted_run(const char *chars, size_t len, long *col, const struct tablist *tl)
{
	long start = *col, end = start;
	size_t k;

	for (k = 0; k < len; k++) {
		if (chars[k] == '\t') {
			long next = __util_tablist_next_stop(tl, end);
			end = next ? next : end + 1;
		} else {
			end++;
		}
	}

	{
		long c = start;
		for (;;) {
			long next = __util_tablist_next_stop(tl, c);
			if (next && next <= end) { fputc('\t', stdout); c = next; }
			else break;
		}
		while (c < end) { fputc(' ', stdout); c++; }
	}
	*col = end;
}

static void unexpand_stream(FILE *f, const struct tablist *tl, int effective_a)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	while ((len = getline(&line, &cap, f)) >= 0) {
		int had_nl = (len > 0 && line[len - 1] == '\n');
		size_t n = had_nl ? (size_t)(len - 1) : (size_t)len;
		long col = 1;
		size_t i = 0, j;

		/* Leading run: always converted, no minimum length. */
		j = i;
		while (j < n && is_blank(line[j])) j++;
		if (j > i) { emit_converted_run(line + i, j - i, &col, tl); i = j; }

		if (effective_a) {
			while (i < n) {
				if (is_blank(line[i])) {
					j = i;
					while (j < n && is_blank(line[j])) j++;
					if (j - i >= 2) {
						emit_converted_run(line + i, j - i, &col, tl);
					} else {
						fputc(line[i], stdout);
						col++;
					}
					i = j;
				} else if (line[i] == '\b') {
					fputc('\b', stdout);
					if (col > 1) col--;
					i++;
				} else {
					fputc(line[i], stdout);
					col++;
					i++;
				}
			}
		} else {
			fwrite(line + i, 1, n - i, stdout);
		}
		if (had_nl) fputc('\n', stdout);
	}
	free(line);
}

int __util_unexpand_main(int argc, char **argv)
{
	struct tablist tl;
	int opt_a = 0, have_t = 0;
	int i = 1;
	int had_error = 0;

	for (; i < argc; i++) {
		char *a = argv[i];

		if (a[0] != '-' || a[1] == 0) break;
		if (!strcmp(a, "--")) { i++; break; }
		if (!strcmp(a, "-a")) { opt_a = 1; continue; }
		if (!strcmp(a, "-t") || (a[1] == 't' && a[2])) {
			const char *spec = a[2] ? a + 2 : NULL;
			if (!spec) {
				if (i + 1 >= argc) { fprintf(stderr, "unexpand: -t: option requires an argument\n"); return 2; }
				spec = argv[++i];
			}
			if (__util_tablist_parse(spec, &tl) < 0) {
				fprintf(stderr, "unexpand: %s: invalid tablist\n", spec);
				return 2;
			}
			have_t = 1;
			continue;
		}
		fprintf(stderr, "unexpand: invalid option -- '%s'\n", a);
		return 2;
	}
	if (!have_t) { tl.interval = 8; tl.stops = NULL; tl.nstops = 0; }

	{
		int effective_a = opt_a || have_t;

		if (i >= argc) {
			unexpand_stream(stdin, &tl, effective_a);
		} else {
			for (; i < argc; i++) {
				FILE *f = !strcmp(argv[i], "-") ? stdin : fopen(argv[i], "r");
				if (!f) {
					fprintf(stderr, "unexpand: %s: %s\n", argv[i], strerror(errno));
					had_error = 1;
					continue;
				}
				unexpand_stream(f, &tl, effective_a);
				if (f != stdin) fclose(f);
			}
		}
	}

	__util_tablist_free(&tl);
	return had_error ? 1 : 0;
}
