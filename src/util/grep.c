/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * grep(1p): `grep [-E|-F] [-c|-l|-q] [-insvx] -e pattern_list
 * [-e pattern_list]... [-f pattern_file]... [file...]` (and the two
 * sibling SYNOPSIS forms XCU grep(1p) gives -- one -f with no -e, or a
 * single bare pattern_list operand -- all three collapse to the same
 * pattern-collection logic below).
 *
 * PATTERN SOURCES: "If neither the -e nor the -f option is specified,
 * the first operand ... shall be used as the pattern_list."  -e's own
 * text and pattern_list's OPERANDS text both say a pattern_list is one
 * or more patterns "separated by newlines" -- so a single -e argument
 * (or the bare operand) is itself split on embedded '\n' bytes into
 * however many patterns it contains (split_patterns() below), and each
 * -f pattern_file contributes one pattern per line ("Patterns in
 * pattern_file shall be terminated by a <newline>" -- read via
 * getline(), the same "handles a missing final newline and an
 * arbitrarily long line" tool every other line-oriented utility in
 * this tree uses).  "Multiple -e and -f options shall be accepted...
 * the order of evaluation is unspecified": this file resolves that
 * freedom by appending every pattern to one flat list in argv
 * encounter order, so a real invocation still gets a deterministic
 * (if unspecified-by-the-standard) result rather than a random one.
 *
 * -e/-f's pattern_file argument follow this project's established
 * "-xVALUE or -x VALUE, never -x -- VALUE" attachment convention (see
 * src/util/uniq.c's -f/-s) -- not the getopt(3) convention this
 * project deliberately does not use anywhere (see util.h's own header
 * comment on why: no file in this tree calls getopt()).  A real,
 * deliberate scope narrowing that follows from the same choice: no
 * option here is ever *bundled* with another (no `-in` for `-i -n`),
 * even though XCU's Utility Syntax Guideline 5 permits it and grep's
 * own SYNOPSIS groups `-insvx` suggestively -- every other utility in
 * this tree already made the same choice (checked before writing this
 * file: no src/util/*.c parses bundled short options), and grep does
 * not break that consistency.
 *
 * -E/-F and -c/-l/-q are each read as genuinely mutually exclusive,
 * the same "a bracketed alternation in the SYNOPSIS means exactly
 * one" reading src/util/uniq.c's own header comment already applies to
 * its own -c|-d|-u.
 *
 * -x ("match an entire line") is implemented by checking that the
 * match regexec() actually finds spans the whole line
 * (rm_so == 0 && (size_t)rm_eo == line length) rather than by
 * rewriting the pattern with explicit anchors -- src/regex/regex.c's
 * own matcher is leftmost-longest at whatever the leftmost matching
 * start position is (see that file's header), so if a whole-line match
 * exists it is necessarily the one found: any match starting at
 * offset 0 is leftmost by definition, and among matches starting at 0
 * the longest is kept, and a match spanning the whole line cannot be
 * shorter than any other match starting at 0.  Because -x needs the
 * match offsets, patterns are compiled without REG_NOSUB exactly when
 * -x is in effect (regexec() only fills the pmatch array back in when
 * REG_NOSUB was *not* given at regcomp() time -- src/regex/regex.c's
 * own regexec()); every other invocation compiles with REG_NOSUB,
 * skipping capture-slot bookkeeping this file never reads.
 *
 * A regexec() failure that is not REG_NOMATCH -- this regex engine's
 * own documented MAX_STEPS/MAX_BACKTRACK budgets, src/regex/regex.c's
 * header -- is treated as "this pattern did not match this line" (the
 * remaining patterns, and the remaining lines, are still tried) plus a
 * one-time diagnostic and a forced error exit status, rather than
 * aborting the run outright: the same "a diagnostic is not the primary
 * failure" posture util.h's own __util_diagf() documents, applied to a
 * per-line rather than a per-invocation failure.  -s does NOT suppress
 * that diagnostic -- -s's own OPTIONS text scopes it to "the error
 * messages ordinarily written for nonexistent or unreadable files",
 * not to a pattern-matching failure.
 *
 * STDIN / "-": both an absent file operand and an explicit "-" operand
 * read standard input (STDIN section / OPERANDS' file description).
 * XCU does not specify what -l/-c/-n print as that source's name (its
 * own text always says "file", i.e. a real pathname); this
 * implementation prints "(standard input)", the same string every
 * other real grep uses, which is a real, deliberate choice filling a
 * genuine gap rather than reconstructing unwritten standard text.
 *
 * The "file:" prefix (STDOUT) is added exactly when more than one file
 * *operand* was given on the command line -- not "more than one
 * source was actually read" -- matching the STDOUT section's own
 * "more than one file argument appears" wording literally: `grep foo`
 * (zero operands, reads stdin) and `grep foo onefile` (one operand)
 * both get no prefix, `grep foo a b` gets one on every selected line
 * from either.
 *
 * LOCALE: src/misc/locale.c -- this library is C/POSIX-locale-only, so
 * -i's case-folding is plain byte-value toupper/tolower (regcomp()'s
 * REG_ICASE, or strcasecmp(3)/strcasestr(3) for -F) -- nothing here
 * claims a stronger collation or character class than that locale
 * actually has, the same caveat src/util/sort.c's own header spells
 * out for its own byte-value-only comparison.
 *
 * INPUT FILES "shall be text files" (INPUT FILES) -- a line is read
 * with getline() and, like every other line-oriented utility in this
 * tree, may contain embedded NUL bytes only by accident; matching
 * against such a line is not specially guarded against (strstr()/
 * strcasestr() for -F, or regexec()'s own NUL-terminated-string
 * contract for BRE/ERE) since a real text file does not produce one.
 * A missing final newline on the last line of a file is preserved
 * exactly on output (the "%s" STDOUT format is never followed by an
 * unconditionally-added '\n' -- only the '\n' getline() itself read,
 * if any, is ever re-emitted) rather than silently added or dropped.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <regex.h>
#include "util.h"

/* ==== pattern collection ================================================== */

struct pat_list {
	char **v;
	size_t n, cap;
};

static int pl_add(struct pat_list *pl, const char *s, size_t len)
{
	char *dup;

	if (pl->n >= pl->cap) {
		size_t newcap;
		char **g;
		if (!__util_array_capacity(pl->cap, pl->n, 1, 8, sizeof *pl->v, &newcap)) return -1;
		g = __util_reallocarray(pl->v, newcap, sizeof *pl->v);
		if (!g) return -1;
		pl->v = g;
		pl->cap = newcap;
	}
	dup = strndup(s, len);
	if (!dup) return -1;
	pl->v[pl->n++] = dup;
	return 0;
}

/* One -e argument (or the bare pattern_list operand) is itself zero or
 * more patterns separated by '\n'; see this file's own header. */
static int split_patterns(struct pat_list *pl, const char *s)
{
	const char *start = s, *nl;

	if (!*s) return pl_add(pl, s, 0);
	while (*start) {
		nl = strchr(start, '\n');
		if (nl) {
			if (pl_add(pl, start, (size_t)(nl - start)) < 0) return -1;
			start = nl + 1;
		} else {
			if (pl_add(pl, start, strlen(start)) < 0) return -1;
			break;
		}
	}
	return 0;
}

/* Each line of pattern_file is one pattern -- unlike split_patterns(),
 * no further splitting on embedded '\n' happens here (a line read by
 * getline() cannot itself contain one). */
static int add_pattern_file(struct pat_list *pl, const char *path)
{
	FILE *f;
	char *line = 0;
	size_t cap = 0;
	ssize_t len;
	int rc = 0;

	f = fopen(path, "r");
	if (!f) return -1;
	while ((len = getline(&line, &cap, f)) >= 0) {
		if (len && line[len - 1] == '\n') line[--len] = 0;
		if (pl_add(pl, line, (size_t)len) < 0) { rc = -1; break; }
	}
	free(line);
	fclose(f);
	return rc;
}

static void pl_free(struct pat_list *pl)
{
	size_t i;
	for (i = 0; i < pl->n; i++) free(pl->v[i]);
	free(pl->v);
}

/* ==== options ============================================================== */

struct grep_opts {
	int extended, fixed, cflag, iflag, lflag, nflag, qflag, sflag, vflag, xflag;
};

/* ==== matching ============================================================= */

/* Tries every pattern against one line (already stripped of its own
 * trailing '\n', if it had one); returns 1 on a match, 0 otherwise.
 * *err is set (never cleared) on a regexec() failure that is not
 * REG_NOMATCH -- see this file's header for what that means. */
static int line_matches(const struct grep_opts *o, const struct pat_list *pl,
	const regex_t *res, const char *line, size_t len, int *err)
{
	size_t k;

	for (k = 0; k < pl->n; k++) {
		if (o->fixed) {
			if (o->xflag) {
				size_t plen = strlen(pl->v[k]);
				if (plen != len) continue;
				if (len == 0) return 1;
				if (o->iflag ? !strncasecmp(line, pl->v[k], len)
				             : !memcmp(line, pl->v[k], len)) return 1;
			} else {
				if (o->iflag ? (strcasestr(line, pl->v[k]) != 0)
				             : (strstr(line, pl->v[k]) != 0)) return 1;
			}
		} else {
			regmatch_t m;
			int rc = regexec(&res[k], line, 1, &m, 0);
			if (rc == 0) {
				if (!o->xflag) return 1;
				if (m.rm_so == 0 && (size_t)m.rm_eo == len) return 1;
			} else if (rc != REG_NOMATCH) {
				*err = 1;
			}
		}
	}
	return 0;
}

/* ==== one file ============================================================= */

/* Reads and matches every line of one already-open source, printing
 * per-line output for the default/-n modes as it goes (default/-n
 * cannot be deferred to end-of-file the way -c's single count line and
 * -l's single name line can).  Returns 0 on success, -1 on a write
 * error to stdout.  *matched is set to 1 if any line was selected;
 * *count is the number of lines selected; *regexec_failed is set (never
 * cleared) the same way line_matches()'s *err is. */
static int scan_one(const struct grep_opts *o, const struct pat_list *pl,
	const regex_t *res, FILE *f, const char *disp, int multi,
	int *matched, long *count, int *regexec_failed, int *quiet_hit)
{
	char *line = 0;
	size_t cap = 0;
	ssize_t len;
	long lineno = 1;
	int werr = 0;

	while ((len = getline(&line, &cap, f)) >= 0) {
		int had_nl = (len > 0 && line[(size_t)len - 1] == '\n');
		size_t mlen = had_nl ? (size_t)len - 1 : (size_t)len;
		int m, selected;

		if (had_nl) line[mlen] = 0;
		m = line_matches(o, pl, res, line, mlen, regexec_failed);
		selected = o->vflag ? !m : m;

		if (selected) {
			*matched = 1;
			(*count)++;
			if (o->qflag) { *quiet_hit = 1; break; }
			if (o->lflag) break;
			if (!o->cflag) {
				if (multi) { if (fprintf(stdout, "%s:", disp) < 0) werr = 1; }
				if (o->nflag) { if (fprintf(stdout, "%ld:", lineno) < 0) werr = 1; }
				if (fputs(line, stdout) < 0) werr = 1;
				if (had_nl && fputc('\n', stdout) == EOF) werr = 1;
			}
		}
		lineno++;
	}
	free(line);
	return werr ? -1 : 0;
}

/* ==== entry point =========================================================== */

int __util_grep_main(int argc, char **argv)
{
	struct grep_opts o;
	struct pat_list pl;
	regex_t *res = 0;
	int have_e_or_f = 0;
	int i, k;
	int had_error = 0, had_match = 0, regexec_failed = 0, quiet_hit = 0;
	int nfiles, multi;
	char **files;

	memset(&o, 0, sizeof o);
	memset(&pl, 0, sizeof pl);

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!strcmp(arg, "--")) { i++; break; }
		if (arg[0] != '-' || arg[1] == 0) break;

		if (!strcmp(arg, "-E")) { o.extended = 1; continue; }
		if (!strcmp(arg, "-F")) { o.fixed = 1; continue; }
		if (!strcmp(arg, "-c")) { o.cflag = 1; continue; }
		if (!strcmp(arg, "-i")) { o.iflag = 1; continue; }
		if (!strcmp(arg, "-l")) { o.lflag = 1; continue; }
		if (!strcmp(arg, "-n")) { o.nflag = 1; continue; }
		if (!strcmp(arg, "-q")) { o.qflag = 1; continue; }
		if (!strcmp(arg, "-s")) { o.sflag = 1; continue; }
		if (!strcmp(arg, "-v")) { o.vflag = 1; continue; }
		if (!strcmp(arg, "-x")) { o.xflag = 1; continue; }
		if (!strcmp(arg, "-e") || !strncmp(arg, "-e", 2)) {
			const char *val;
			if (arg[2]) val = arg + 2;
			else { if (++i >= argc) { __util_diagf("grep: -e: option requires an argument\n"); pl_free(&pl); return 2; } val = argv[i]; }
			if (split_patterns(&pl, val) < 0) { __util_diagf("grep: out of memory\n"); pl_free(&pl); return 2; }
			have_e_or_f = 1;
			continue;
		}
		if (!strcmp(arg, "-f") || !strncmp(arg, "-f", 2)) {
			const char *val;
			if (arg[2]) val = arg + 2;
			else { if (++i >= argc) { __util_diagf("grep: -f: option requires an argument\n"); pl_free(&pl); return 2; } val = argv[i]; }
			if (add_pattern_file(&pl, val) < 0) {
				__util_diagf("grep: %s: %s\n", val, strerror(errno));
				pl_free(&pl);
				return 2;
			}
			have_e_or_f = 1;
			continue;
		}
		__util_diagf("grep: %s: invalid option\n", arg);
		pl_free(&pl);
		return 2;
	}

	if (o.extended && o.fixed) {
		__util_diagf("grep: -E and -F are mutually exclusive\n");
		pl_free(&pl);
		return 2;
	}
	if (o.cflag + o.lflag + o.qflag > 1) {
		__util_diagf("grep: -c, -l and -q are mutually exclusive\n");
		pl_free(&pl);
		return 2;
	}

	if (!have_e_or_f) {
		if (i >= argc) {
			__util_diagf("grep: missing pattern operand\n");
			pl_free(&pl);
			return 2;
		}
		if (split_patterns(&pl, argv[i]) < 0) { __util_diagf("grep: out of memory\n"); pl_free(&pl); return 2; }
		i++;
	}

	nfiles = argc - i;
	files = argv + i;
	multi = nfiles > 1;

	if (!o.fixed) {
		int cflags = (o.extended ? REG_EXTENDED : 0) | (o.iflag ? REG_ICASE : 0) |
			(o.xflag ? 0 : REG_NOSUB);
		res = __util_mallocarray(pl.n ? pl.n : 1, sizeof *res);
		if (!res) { __util_diagf("grep: out of memory\n"); pl_free(&pl); return 2; }
		for (k = 0; k < (int)pl.n; k++) {
			int rc = regcomp(&res[k], pl.v[k], cflags);
			if (rc) {
				char errbuf[128];
				int j;
				regerror(rc, 0, errbuf, sizeof errbuf);
				__util_diagf("grep: %s: %s\n", pl.v[k], errbuf);
				for (j = 0; j < k; j++) regfree(&res[j]);
				free(res);
				pl_free(&pl);
				return 2;
			}
		}
	}

	for (i = 0; !quiet_hit && i < (nfiles ? nfiles : 1); i++) {
		const char *path = nfiles ? files[i] : 0;
		int is_stdin = (!path || !strcmp(path, "-"));
		const char *disp = is_stdin ? "(standard input)" : path;
		FILE *f;
		int file_matched = 0;
		long count = 0;

		if (is_stdin) f = stdin;
		else {
			f = fopen(path, "r");
			if (!f) {
				if (!o.sflag) __util_diagf("grep: %s: %s\n", path, strerror(errno));
				had_error = 1;
				continue;
			}
		}

		if (scan_one(&o, &pl, res, f, disp, multi, &file_matched, &count,
		    &regexec_failed, &quiet_hit) != 0) had_error = 1;

		if (!is_stdin && fclose(f) != 0) had_error = 1;

		if (file_matched) had_match = 1;

		if (!quiet_hit) {
			if (o.lflag) {
				if (file_matched) { if (printf("%s\n", disp) < 0) had_error = 1; }
			} else if (o.cflag) {
				int r = multi ? printf("%s:%ld\n", disp, count) : printf("%ld\n", count);
				if (r < 0) had_error = 1;
			}
		}
	}

	if (quiet_hit) had_match = 1;

	if (regexec_failed) {
		__util_diagf("grep: a pattern match ran out of budget on some input line; treated as no match there\n");
		had_error = 1;
	}

	if (!o.fixed) {
		for (k = 0; k < (int)pl.n; k++) regfree(&res[k]);
		free(res);
	}
	pl_free(&pl);

	/* "the exit status shall be zero if an input line is selected,
	 * even if an error was detected" -- -q's own OPTIONS text, and
	 * the only documented exception to "an error outranks a match"
	 * this file otherwise applies (see this file's header). */
	if (o.qflag) return had_match ? 0 : (had_error ? 2 : 1);
	if (had_error) return 2;
	return had_match ? 0 : 1;
}
