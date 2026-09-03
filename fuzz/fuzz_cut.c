/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_cut_main() -- src/util/cut.c's own range-list parser
 * (parse_list(), static to that file): "a comma or <blank>-separated
 * list of numbers and/or number ranges" -- "N", "N-", "N-M", "-M" --
 * shared across all three of cut's modes (-b/-c/-f), plus
 * is_selected()'s membership scan and the byte/character/field slicing
 * built on top of it.
 *
 * Not src/util/tablist.c: `grep -n tablist src/util/cut.c` finds
 * nothing -- cut.c defines its own static parse_list()/struct
 * range/is_selected() from scratch, textually similar to tablist.c's
 * parse loop but a genuinely separate grammar (tablist.c has no
 * equivalent of cut's "N-", "N-M", "-M" range forms). tablist.c itself
 * is reachable only through expand(1p)'s/unexpand(1p)'s -t option, and
 * has no fuzz harness of its own yet.
 *
 * Same tokenized-argv shape as fuzz_expr.c's and fuzz_find.c's
 * harnesses: cut's whole interface is argv plus a file operand, so
 * there's no separate stream-level lexer here the way xargs'
 * read_tokens() has. The fuzz buffer is not tokenized into multiple
 * operands, though -- cut's list is syntactically ONE operand (the value
 * of -b/-c/-f), so the whole (embedded-NUL-rejected) buffer becomes
 * that one operand verbatim, capped at LIST_CAP bytes.
 *
 * Byte 0 selects: bits 0-1 the mode (-b/-c/-f, wrapping via %3 so none
 * is systematically favored by a power-of-two mask); bit 2 whether -s
 * is added (field mode only; -s/-d are refused outright with any other
 * mode, so this harness lets that refusal path run too); bit 3 whether
 * an explicit -d is added, with the delimiter byte taken from byte 1 of
 * the input (so a NUL, non-ASCII, or multi-byte-lead delimiter is real,
 * reachable input); bit 4 whether -n is added ("not implemented" --
 * always refused with exit 2, but that refusal path still needs to
 * actually run to count as coverage).
 *
 * The fixture is a small, fixed content file cut(1p) reads -- multiple
 * short lines mixing tab- and colon-delimited fields, one line with NO
 * delimiter at all (exercises -s's suppress-vs-passthrough branch), one
 * empty line, and one line containing a real multi-byte UTF-8 character
 * (exercises -c's mbrtowc()-driven character counting against -b's raw
 * byte counting, since this build's mbrtowc() really does decode UTF-8
 * unconditionally). Fixed rather than fuzzed: the grammar under test is
 * the list's, and there's no separate-value oracle to gain by also
 * fuzzing the data cut(1p) slices.
 *
 * No spawn risk: cut(1p) never invokes another program under any option
 * this file implements (checked while reading the file in full), so,
 * unlike fuzz_find.c's -exec/-ok, no safety exclusion is needed here.
 *
 * Checked: cut(1p)'s own EXIT STATUS section ("0 Success. >0 An error
 * occurred."), narrowed to the two values cut.c's own code ever
 * actually returns on a non-fatal path (1: an unopenable file operand
 * -- unreachable here, since this harness always passes one file that
 * fixture() has already created; 2: any usage error), plus 0 for
 * success.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define LIST_CAP 128
#define ROOT "/tmp/cutfz"

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
		"a\tbb\tccc\n"
		"1:2:3:4\n"
		"no-delimiter-here\n"
		"\n"
		"h\xc3\xa9llo\tw\xc3\xb6rld\n";   /* embedded multi-byte UTF-8 (e, o with diaeresis) */

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
	write_file(ROOT "/data", data, sizeof data - 1);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char list[LIST_CAP + 1];
	size_t n;
	unsigned opt, delimbyte;
	char modeflag[3] = { '-', 0, 0 };
	char delimopt[] = "-d";
	char delimval[2];
	char sopt[] = "-s", nopt[] = "-n";
	char *argv[10];
	int argc = 0;
	int rc;

	if (size < 2) return 0;
	fixture();

	opt = data[0];
	delimbyte = data[1];
	data += 2; size -= 2;

	n = size < LIST_CAP ? size : LIST_CAP;
	memcpy(list, data, n);
	list[n] = 0;
	if (memchr(list, 0, n)) return 0;   /* embedded NUL: not one operand */

	switch (opt % 3) {
	case 0: modeflag[1] = 'b'; break;
	case 1: modeflag[1] = 'c'; break;
	default: modeflag[1] = 'f'; break;
	}

	argv[argc++] = (char *)"cut";
	argv[argc++] = modeflag;
	argv[argc++] = list;
	if (opt & 0x08) {
		delimval[0] = (char)delimbyte;
		delimval[1] = 0;
		if (delimval[0] != 0) {
			argv[argc++] = delimopt;
			argv[argc++] = delimval;
		}
	}
	if (opt & 0x04) argv[argc++] = sopt;
	if (opt & 0x10) argv[argc++] = nopt;
	argv[argc++] = (char *)ROOT "/data";
	argv[argc] = NULL;

	rc = __util_cut_main(argc, argv);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("cut returned an exit status outside {0,1,2}", list, rc, 0);

	return 0;
}
