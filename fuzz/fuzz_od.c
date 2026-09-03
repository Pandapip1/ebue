/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_od_main() -- src/util/od.c's own `-t type` parser (parse_type(),
 * static to that file: "c", or one of "xodu" followed by a decimal byte
 * count that must parse cleanly AND be exactly 1, 2, 4 or 8; this build
 * implements only the decimal-byte-count form, refusing the C/S/I/L
 * letter-size suffix, `-t a`, `-t f`) plus the per-type row formatting
 * parse_type()'s output selects between: print_row()'s -t c escape
 * table (char_field()), and load_unit()'s native-byte-order multi-byte
 * decode feeding odigits()/udigits()/ddigits()'s width tables and
 * od_run()'s -v-gated run-of-identical-rows "*" elision.
 *
 * The fuzz buffer, after one leading options byte, becomes the -t
 * argument verbatim (capped at TYPE_CAP bytes, embedded NUL rejected):
 * od's -t takes exactly one string and parse_type() is the only thing
 * that ever tokenizes it internally, via s[0] and strtol(s+1, ...). No
 * prefix or suffix is added: a string the fuzzer discovers on its own is
 * handed straight to parse_type(), so both well-formed spellings
 * ("x1".."u8", "c") and every rejected shape are reached exactly as a
 * real argv[] would present them.
 *
 * Byte 0 selects: bit 0 -v (disables od_run()'s "*" elision -- fuzzed
 * because the fixture below is built specifically to contain an
 * elidable run); bits 1-2 -A's address-base letter, cycled through all
 * four od(1p) defines ('d','o','x','n') via a fixed table so all four
 * -- including 'n', which print_offset() special-cases as a bare early
 * return -- are reached with equal weight.
 *
 * The fixture is a small, fixed binary file (not derived from the fuzz
 * input: the grammar under test is -t's, not the bytes od(1p) dumps)
 * built, not written as a string literal, because it deliberately
 * contains every byte value 0x00-0xFF in order (every char_field()
 * branch -- named escapes, printable range, three-digit-octal fallback
 * -- gets at least one real byte, and every load_unit() unit size sees
 * the full value range as it slides across the ramp), followed by three
 * full 16-byte rows of the same repeated byte (an elidable run for
 * od_run()'s "*" logic -- only a full ROWBYTES row can ever be elided,
 * which is why this run is placed after, not inside, the 256-byte ramp)
 * and a short, sub-16-byte tail (od_run()'s own final-short-row path).
 *
 * No spawn risk: od(1p) never invokes another program under any option
 * this file implements (checked while reading od.c in full).
 *
 * stdout/stderr are redirected: od_run()'s own row output and
 * print_type's diagnostics would otherwise hit the real terminal on
 * every one of millions of calls.
 *
 * Checked: src/util/od.c has no dedicated EXIT STATUS section of its
 * own, but every `return` in __util_od_main() (read in full) is exactly
 * 0 or 1 -- every usage/parse error is a literal `return 1;`, and the
 * success path's `return status ? 1 : 0;` folds od_run()'s own
 * `is->any_error || od_output_failed` boolean into the same two values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define TYPE_CAP 32
#define ROOT "/tmp/odfz"
#define FIXTURE ROOT "/data"

/* fixture: a small, fixed binary file -- see this file's header comment
 * for why its content is NOT derived from the fuzz input, and for why it
 * is built rather than written as a string literal. */

static void write_file(const char *path, const unsigned char *data, size_t len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, data, len);
	close(fd);
}

static void fixture(void)
{
	static int done;
	unsigned char buf[256 + 48 + 5];
	size_t i, n = 0;

	if (done) return;
	done = 1;

	for (i = 0; i < 256; i++) buf[n++] = (unsigned char)i;       /* full byte ramp */
	for (i = 0; i < 48; i++) buf[n++] = 0x41;                    /* 3 elidable rows */
	for (i = 0; i < 5; i++) buf[n++] = (unsigned char)(0x30 + i); /* short final row */

	mkdir(ROOT, 0755);
	write_file(FIXTURE, buf, n);
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
	char typestr[TYPE_CAP + 1];
	size_t n;
	static const char abases[4] = { 'd', 'o', 'x', 'n' };
	char abaseval[2];
	char *argv[8];
	int argc = 0;
	int rc;

	if (size < 1) return 0;
	fixture();

	opts = data[0];
	data++; size--;

	n = size < TYPE_CAP ? size : TYPE_CAP;
	memcpy(typestr, data, n);
	typestr[n] = 0;
	if (memchr(typestr, 0, n)) return 0;   /* embedded NUL: not one operand */

	if (!redirect_streams()) return 0;

	abaseval[0] = abases[(opts >> 1) & 0x03];
	abaseval[1] = 0;

	argv[argc++] = (char *)"od";
	if (opts & 0x01) argv[argc++] = (char *)"-v";
	argv[argc++] = (char *)"-A";
	argv[argc++] = abaseval;
	argv[argc++] = (char *)"-t";
	argv[argc++] = typestr;
	argv[argc++] = (char *)FIXTURE;
	argv[argc] = NULL;

	rc = __util_od_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 1)
		oracle_mismatch_i("__util_od_main returned outside {0,1}", typestr, rc, 0);

	return 0;
}
