/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_sort_main() -- src/util/sort.c's own `-k keydef` parser
 * (parse_keydef(), static to that file: `field_start[type][,field_end
 * [type]]`, field_start/field_end each `F[.C]`, type zero or more of
 * "bdfinr" with no separator) plus the comparison machinery a parsed key
 * drives: key_start_off()/key_end_off() resolving a field/char position
 * against a real split_fields() result, and compare_by_key()/
 * compare_range() applying whichever of -b/-d/-f/-i/-n/-r (global, or
 * the key's own attached modifiers -- modifiers anywhere in one spec
 * replace ALL global options for that key) actually won.
 *
 * Same tokenized-argv shape as fuzz_find.c's harness: the fuzz buffer,
 * after one leading options byte, is split on NUL bytes into up to
 * CAP_TOKENS scratch-owned tokens, and EACH token becomes the value of
 * its OWN "-k" option -- this harness always fuzzes
 * `sort -k TOK1 -k TOK2 ...`, never a single key. That's deliberate:
 * sort(1p) is defined over an arbitrary number of -k options applied in
 * order, and the interaction between two keys sharing no attached
 * modifier (each falling back independently to the SAME global flags)
 * vs. two keys that both carry their own is something a single -k spec
 * could never exercise. An empty-string token (from a leading, trailing,
 * or doubled NUL) is real, reachable input -- parse_keydef() rejects it
 * on its very first `isdigit()` check, exactly the "malformed key" path
 * this harness exists to reach as much as the well-formed one.
 *
 * Byte 0 selects the global flags a key with no attached modifier falls
 * back to, plus the two whole-program modes that change what "compare"
 * even means: bit 0 -r (reverse, also the tiebreak's own -r), bit 1 -u
 * (collapses the raw-byte tiebreak entirely), bit 2 -c (check-only:
 * exercises the disorder/duplicate-key scan instead of merge_sort(),
 * and -c's EXIT STATUS shape is NOT the usual 0/1/2-is-error one), bit 3
 * -b (global blanks-stripped default, meaningful only for a key that
 * never attaches its own 'b'). All four bits are independent, so every
 * one of the 16 combinations is reachable, including ones sort(1p)
 * treats as pure no-ops.
 *
 * The fixture is a small, fixed multi-line file (not derived from the
 * fuzz input: the grammar under test is the -k spec's, not the data
 * sort(1p) reorders) with fields of different widths, a leading-blank
 * line (exercises the default splitter's "leading separator stays in
 * field 1" rule), a tab as well as space field separators, a negative
 * numeric field and a non-numeric field in the same column position
 * across lines, an empty line (nfields == 0, exercising key_start_off()/
 * key_end_off()'s `(f - 1) >= nf` clamp to `len`), and two lines sharing
 * a first field so key1-equal, key2-different and fully-equal cases are
 * all reachable from one fixed file. Always given as the sole file
 * operand, never stdin: -c refuses more than one file operand, and one
 * keeps every opts-byte combination valid.
 *
 * No spawn, no filesystem write risk: sort(1p) as this harness drives it
 * never takes -o (output stays on the redirected stdout below) and
 * never invokes another program.
 *
 * stdout/stderr are redirected: sort's successful path prints every
 * line of the (reordered) fixture to stdout on every one of millions of
 * calls, and parse_keydef()/usage failures print a diagnostic to stderr
 * via __util_diagf().
 *
 * Checked: sort(1p)'s EXIT STATUS is NOT the usual "0 ok, 1 per-entry
 * error, 2 usage error" shape, but reading __util_sort_main() in full
 * shows every `return` in it is still one of exactly 0, 1 (only from
 * the -c/-C disorder-or-duplicate path) or 2 (every other error), so the
 * assertion below checks that real, full range.
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

/* fixture: a small, fixed multi-line file -- see this file's header
 * comment for why its content is NOT derived from the fuzz input. */

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

/* stdout/stderr redirection -- see this file's header comment. */

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
