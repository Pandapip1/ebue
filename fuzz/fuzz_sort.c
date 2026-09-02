/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_sort_main() -- src/util/sort.c's own `-k keydef` parser
 * (parse_keydef(), static to that file: `field_start[type][,field_end
 * [type]]`, field_start/field_end each `F[.C]`, type zero or more of
 * "bdfinr" with no separator -- read that file's header comment in full
 * for the exact grammar and for the deliberate "'b' applies symmetrically
 * to both endpoints of a key" simplification this build makes) plus the
 * comparison machinery a parsed key drives: key_start_off()/key_end_off()
 * resolving a field/char position against a real split_fields() result,
 * and compare_by_key()/compare_range() applying whichever of -b/-d/-f/-i/
 * -n/-r (global, or the key's own attached modifiers -- has_mod's
 * "modifiers anywhere in one spec replace ALL global options for that
 * key" rule) actually won.
 *
 * WHAT IS FUZZED, AND HOW.  Same tokenized-argv shape as fuzz_find.c's
 * harness (read that file's own header comment for the general
 * reasoning): the fuzz buffer, after one leading options byte, is split
 * on NUL bytes into up to CAP_TOKENS scratch-owned tokens, and EACH token
 * becomes the value of its OWN "-k" option -- i.e. this harness always
 * fuzzes `sort -k TOK1 -k TOK2 ...`, never a single key.  That is
 * deliberate, not an oversight: sort(1p) itself is defined over an
 * arbitrary number of -k options applied in order (line_compare()'s own
 * loop, "the FIRST key... unless... EQUAL", falling through to the next),
 * and the interaction between two keys sharing no attached modifier (each
 * one falling back independently to the SAME global -b/-d/-f/-i/-n/-r)
 * vs. two keys that both carry their own is exactly the part of this
 * file's own header comment ("no option shall apply to either") that a
 * single -k spec could never exercise at all.  An empty-string token
 * (from a leading, trailing, or doubled NUL) is real, reachable input the
 * same way it is in fuzz_find.c's tokenizer -- parse_keydef() rejects it
 * on its very first `isdigit()` check, which is exactly the "malformed
 * key" path this harness exists to reach as much as the well-formed one.
 *
 * OPTION BYTE.  Byte 0 selects the GLOBAL flags a key with no attached
 * modifier of its own falls back to, plus the two whole-program modes
 * that change what "compare" even means: bit 0 -r (reverse, also the
 * tiebreak's own -r per this file's header comment on TIEBREAK), bit 1 -u
 * (collapses the raw-byte tiebreak entirely -- see line_compare()'s `if
 * (c == 0 && !o->u)`), bit 2 -c (check-only: exercises the disorder/
 * duplicate-key scan in __util_sort_main() instead of merge_sort(), and
 * this file's header comment's own note that -c's EXIT STATUS shape is
 * NOT the usual 0/1/2-is-error one), bit 3 -b (global blanks-stripped
 * default, meaningful only for a key -- or the no-key whole-line default
 * -- that never attaches its own 'b').  All four are independent bits, so
 * every one of the 16 combinations this byte can select is reachable,
 * including ones sort(1p) itself treats as pure no-ops (-b with only
 * modifier-carrying keys, -u with -c already forcing a stricter
 * comparison).
 *
 * THE FIXTURE.  A small, fixed multi-line file (not derived from the fuzz
 * input, for the identical reason fuzz_cut.c's own header gives for its
 * own fixed content fixture: the grammar under test is the -k spec's, not
 * the data sort(1p) reorders) with fields of different widths, a leading-
 * blank line (exercises the default splitter's "leading separator stays
 * in field 1" rule, this file's header comment's own DEFAULT FIELD
 * SPLITTING section), a tab as well as space field separators (both
 * `isblank()`), a negative numeric field and a non-numeric field in the
 * same column position across lines (parse_numeric()'s sign handling vs.
 * its "runs of non-digits parse as 0" fallback), an empty line (nfields
 * == 0, exercising key_start_off()'s/key_end_off()'s own `(f - 1) >= nf`
 * clamp to `len`), and two lines sharing a first field so key1-equal,
 * key2-different and fully-equal cases are all reachable from one fixed
 * file.  Always given as the sole file operand, never stdin: -c refuses
 * more than one file operand (this file's header comment), and always
 * supplying exactly one keeps every opts-byte combination, including -c,
 * valid rather than usage-erroring on operand count alone.
 *
 * NO SPAWN, NO FILESYSTEM WRITE RISK.  sort(1p) as this harness drives it
 * never takes -o (output stays on the redirected stdout below) and never
 * invokes another program -- unlike fuzz_find.c's -exec/-ok, no safety
 * exclusion is needed here at all.
 *
 * STDOUT/STDERR REDIRECTION: same freopen()-a-fixed-sink-file-once-per-
 * call mechanism fuzz_find.c's, fuzz_ar.c's and fuzz_pax.c's own header
 * comments give, and for the identical reason -- sort's own successful
 * path prints every line of the (reordered) fixture to stdout on every
 * one of millions of calls, and parse_keydef()/usage failures print a
 * diagnostic to stderr via __util_diagf().
 *
 * WHAT IS CHECKED.  This file's own header comment spells out sort(1p)'s
 * EXIT STATUS in detail and stresses that it is NOT the usual "0 ok, 1
 * per-entry error, 2 usage error" shape -- but reading __util_sort_main()
 * in full (as this task requires) shows every `return` in it is still one
 * of exactly 0, 1 (only from the -c/-C disorder-or-duplicate path) or 2
 * (every other error), so the assertion below checks that real, full
 * range, the same "the file's own documented contract, not just the
 * subset this harness's own argv happens to reach" reasoning fuzz_ar.c's
 * and fuzz_pax.c's own headers give for their targets.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_TOKENS 8
#define CAP_SCRATCH 256

#define ROOT "/tmp/sortfz"
#define FIXTURE ROOT "/data"

/* ==== fixture: a small, fixed multi-line file -- see this file's header
 * comment for why its content is NOT derived from the fuzz input. ======== */

static void write_file(const char *path, const char *data, size_t len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, data, len);
	close(fd);
}

static void fixture(void)
{
	static int done;
	static const char data[] =
		"  3 charlie 30\n"
		"1 alpha -5\n"
		"22 bravo 100\n"
		"1 alpha 7\n"
		"1 alpha 7\n"
		"abc def\tghi\n"
		"\n"
		"10\tdelta\t-1\n";

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
	write_file(FIXTURE, data, sizeof data - 1);
}

/* ==== stdout/stderr redirection -- see this file's header comment. ======= */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opts;
	char scratch[CAP_SCRATCH];
	char *tok[CAP_TOKENS];
	int ntok = 0;
	size_t si = 0, wi = 0;
	/* "sort" + up to 4 global flags + 2 args per -k token + the fixture
	 * path + the NULL terminator. */
	char *argv[4 + CAP_TOKENS * 2 + 2];
	int argc = 0;
	int rc, i;
	char diagbuf[CAP_SCRATCH + 1];
	size_t dn;

	if (size < 1) return 0;
	fixture();

	opts = data[0];
	data++; size--;

	dn = size < CAP_SCRATCH ? size : CAP_SCRATCH;
	memcpy(diagbuf, data, dn);
	diagbuf[dn] = 0;

	while (si < size && ntok < CAP_TOKENS && wi < CAP_SCRATCH - 1) {
		size_t start = wi;

		while (si < size && data[si] != 0 && wi < CAP_SCRATCH - 1)
			scratch[wi++] = (char)data[si++];
		scratch[wi++] = 0;
		tok[ntok++] = &scratch[start];

		if (si < size && data[si] == 0) si++;   /* consume the delimiter itself */
	}

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"sort";
	if (opts & 0x01) argv[argc++] = (char *)"-r";
	if (opts & 0x02) argv[argc++] = (char *)"-u";
	if (opts & 0x04) argv[argc++] = (char *)"-c";
	if (opts & 0x08) argv[argc++] = (char *)"-b";
	for (i = 0; i < ntok; i++) {
		argv[argc++] = (char *)"-k";
		argv[argc++] = tok[i];
	}
	argv[argc++] = (char *)FIXTURE;
	argv[argc] = NULL;

	rc = __util_sort_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("__util_sort_main returned outside {0,1,2}", diagbuf, rc, 0);

	return 0;
}
