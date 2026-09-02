/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_cut_main() -- src/util/cut.c's own range-list parser
 * (parse_list(), static to that file): "a comma or <blank>-separated
 * list of numbers and/or number ranges" -- "N", "N-", "N-M", "-M" --
 * shared verbatim across all three of cut's modes (-b/-c/-f, per that
 * file's own header comment), plus is_selected()'s membership scan and
 * the byte/character/field slicing built on top of it.
 *
 * NOT src/util/tablist.c, DESPITE THIS FILE'S OWN TASK DESCRIPTION
 * NAMING BOTH: read both files in full before writing this harness, per
 * that instruction, specifically to check whether cut.c actually calls
 * into tablist.c's shared grammar the way expand.c and unexpand.c do
 * (src/util/tablist.c's own header comment: "why it lives here rather
 * than duplicated in src/util/expand.c and src/util/unexpand.c"). It
 * does not: `grep -n tablist src/util/cut.c` finds nothing, and
 * cut.c defines its own static parse_list()/struct range/is_selected()
 * from scratch, textually similar to tablist.c's own parse loop (both
 * walk a comma/blank-separated numeric list with strtol()) but a
 * genuinely separate implementation with a different grammar --
 * tablist.c's is "a single number OR a strictly-ascending list, no
 * ranges, no trailing/leading open end" (interval vs. explicit stops,
 * per __util_tablist_parse()'s own header comment), while cut.c's adds
 * "N-", "N-M" and "-M" range forms tablist.c's grammar has no equivalent
 * of at all. Reported here rather than silently fuzzing the wrong
 * function: tablist.c's __util_tablist_parse() is reachable only through
 * expand(1p)'s and unexpand(1p)'s own -t option (src/util/expand.c,
 * src/util/unexpand.c), and this project has no fuzz_expand.c/
 * fuzz_unexpand.c harness yet -- tablist.c stays without fuzz coverage
 * after this batch, which is outside the three targets this task named.
 *
 * WHAT IS FUZZED, AND HOW.  Same tokenized-argv shape as fuzz_expr.c's
 * and fuzz_find.c's harnesses (read fuzz_expr.c's own header comment for
 * the general reasoning: cut's whole interface is argv plus a file
 * operand, so there is no separate stream-level lexer here the way
 * xargs' read_tokens() has). The fuzz buffer is NOT tokenized into
 * multiple operands, though -- unlike find's whole predicate expression,
 * cut's list is syntactically ONE operand (the value of -b/-c/-f), so
 * the whole (embedded-NUL-rejected) buffer becomes that one operand
 * verbatim, capped at LIST_CAP bytes.
 *
 * OPTION BYTE.  Byte 0 selects: bits 0-1 the mode (-b/-c/-f, wrapping via
 * %3 so all three are reachable and none is systematically favored by a
 * power-of-two mask); bit 2 whether -s is added (field mode only;
 * cut.c's own header comment notes -s/-d are refused outright with any
 * other mode, so this harness lets that refusal path run too rather than
 * only ever building a mode-consistent argv); bit 3 whether an explicit
 * -d is added, with the delimiter byte itself taken from byte 1 of the
 * input (so a NUL, non-ASCII, or multi-byte-lead delimiter byte is real,
 * reachable input, not filtered out); bit 4 whether -n is added ("not
 * implemented", per cut.c's own header comment -- always refused with
 * exit 2, but that refusal path itself has to actually run under the
 * fuzzer's input mix to matter as coverage, not merely exist in the
 * source).
 *
 * THE FIXTURE.  A small, fixed content file cut(1p) reads -- multiple
 * short lines mixing tab- and colon-delimited fields, one line with NO
 * delimiter at all (exercises -s's suppress-vs-passthrough branch), one
 * empty line, and one line containing a real multi-byte UTF-8 character
 * (exercises -c's mbrtowc()-driven character counting against -b's raw
 * byte counting, since this build's mbrtowc() really does decode UTF-8
 * unconditionally -- cut.c's own header comment on why -b and -c are
 * NOT the same thing here). Fixed rather than fuzzed, for the identical
 * reason fuzz_sed.c's and fuzz_ed.c's own header comments give for their
 * own data/text fixtures: the grammar under test is the list's, and
 * there is no separate-value oracle to gain by also fuzzing the data
 * cut(1p) slices.
 *
 * NO SPAWN RISK.  cut(1p) never invokes another program under any
 * option this file implements (checked while reading the file in full,
 * per this task's own instruction) -- so, unlike fuzz_find.c's -exec/-ok
 * and fuzz_xargs.c's own entire reason for existing, no safety exclusion
 * is needed here at all.
 *
 * WHAT IS CHECKED.  cut(1p)'s own EXIT STATUS section, cited in
 * src/util/cut.c's header comment: "0 Success. >0 An error occurred." --
 * narrowed, like every other __util_*_main() harness in this directory,
 * to the two values that file's own code ever actually returns on a
 * non-fatal path (1: an unopenable file operand -- unreachable here,
 * since this harness always passes one file that fixture() has already
 * created; 2: any usage error -- bad list, conflicting -b/-c/-f, -d/-s
 * with the wrong mode, -n), plus 0 for success.
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
