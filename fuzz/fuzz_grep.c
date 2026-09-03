/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_grep_main()'s (src/util/grep.c) option handling and
 * line-filtering logic -- not the regex engine itself, which
 * fuzz/fuzz_regex.c already fuzzes directly. Covered here instead: -E/-F
 * mutual exclusion, -i/-v/-x/-n/-s and the -c/-l/-q trio; -e supplied
 * attached or as a separate argv element; a bare pattern_list operand;
 * split_patterns()'s split of one operand on embedded '\n' into multiple
 * patterns (an embedded '\n' in the fuzzed pattern is let through rather
 * than filtered, so this gets exercised); -x's whole-line-match check;
 * -F's literal strstr()/strcasestr() path; and getline()'s line
 * splitting over content that may or may not end in a final newline.
 * Not covered: -f pattern_file (same getline()-loop shape already
 * exercised for the content file) and multiple file operands (this
 * harness always passes exactly one).
 *
 * A temp file, not stdin, carries the fuzzed content: `stdin` is `FILE
 * *const` here, so it can't be reassigned via fmemopen(), and grep
 * always needs a real file operand from this harness anyway (see the
 * zero-operand hazard below). The path is fixed and overwritten each
 * call; libFuzzer runs one input at a time so that's safe.
 *
 * The attached "-e" form is guarded against a real hazard: grep.c reads
 * a non-NUL arg[2] as -e's value, else takes the next argv element. An
 * attached "-e" with an EMPTY fuzzed pattern collapses to the literal
 * string "-e", so grep would consume the trailing temp-file path as the
 * pattern instead, leaving zero file operands -- and grep with no
 * operand reads real stdin, risking a hang. So the attached form is only
 * used when the pattern is non-empty; empty patterns always go through
 * the separate-argv-element form. The bare-operand form is separately
 * guarded with a leading "--" in case the fuzzed pattern starts with
 * '-' and would otherwise be read as another option.
 *
 * No differential oracle (same reasoning as fuzz_regex.c). Checked
 * instead: __util_grep_main() returns 0, 1, or 2 -- narrower than XCU's
 * "0 selected / 1 none selected / >1 error", since grep.c's code (read
 * in full) never returns anything else, so a fourth value would be a
 * real regression. The exit()-vs-return discipline (src/internal/util.h)
 * is checked for free: a stray exit() would end the fuzzing process
 * itself, which libFuzzer reports distinctly -- grep.c was read in full
 * and calls neither, unlike src/util/expr.c's dupstr() (fixed alongside
 * this harness; see fuzz_expr.c's header).
 *
 * Pattern capped at 32 bytes, content at 48: regexec()'s outer loop
 * tries every start offset in a line with a fresh step budget each, so
 * an uncapped line is a real throughput cost even though it's no longer
 * a crash risk (fuzz_regex.c's header covers that fix).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_PAT 32
#define CAP_CONTENT 48

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opt1, opt2;
	size_t split, plen, clen;
	char pat[CAP_PAT + 1];
	char content[CAP_CONTENT];
	char epat[CAP_PAT + 3];  /* "-e" + pattern, attached form */
	char argv0[] = "grep";
	char a_E[] = "-E", a_F[] = "-F", a_i[] = "-i", a_v[] = "-v", a_x[] = "-x";
	char a_n[] = "-n", a_s[] = "-s", a_c[] = "-c", a_l[] = "-l", a_q[] = "-q";
	char a_e[] = "-e", a_dashdash[] = "--";
	char tmppath[] = "/tmp/fuzz_grep_input";
	char *argv[16];
	int argc = 0;
	int use_attached;
	FILE *f;
	int rc;

	if (size < 4) return 0;
	opt1 = data[0];
	opt2 = data[1];
	split = data[2] % (size - 3);
	data += 3; size -= 3;

	plen = split < CAP_PAT ? split : CAP_PAT;
	clen = size - split;
	if (clen > CAP_CONTENT) clen = CAP_CONTENT;

	memcpy(pat, data, plen); pat[plen] = 0;
	if (memchr(pat, 0, plen)) return 0;   /* not a genuine C-string operand */
	memcpy(content, data + split, clen);

	f = fopen(tmppath, "wb");
	if (!f) return 0;
	if (clen && fwrite(content, 1, clen, f) != clen) { fclose(f); return 0; }
	if (fclose(f) != 0) return 0;

	argv[argc++] = argv0;
	if ((opt1 & 0x03) == 1) argv[argc++] = a_E;
	else if ((opt1 & 0x03) == 2) argv[argc++] = a_F;
	if (opt1 & 0x04) argv[argc++] = a_i;
	if (opt1 & 0x08) argv[argc++] = a_v;
	if (opt1 & 0x10) argv[argc++] = a_x;
	if (opt1 & 0x20) argv[argc++] = a_n;
	if (opt1 & 0x40) argv[argc++] = a_s;
	switch (opt2 & 0x03) {
	case 1: argv[argc++] = a_c; break;
	case 2: argv[argc++] = a_l; break;
	case 3: argv[argc++] = a_q; break;
	default: break;
	}

	use_attached = (opt2 & 0x08) != 0 && plen > 0;
	if ((opt2 & 0x04) || (opt2 & 0x08)) {
		if (use_attached) {
			epat[0] = '-'; epat[1] = 'e';
			memcpy(epat + 2, pat, plen + 1);
			argv[argc++] = epat;
		} else {
			argv[argc++] = a_e;
			argv[argc++] = pat;
		}
	} else {
		argv[argc++] = a_dashdash;
		argv[argc++] = pat;
	}
	argv[argc++] = tmppath;
	argv[argc] = NULL;

	rc = __util_grep_main(argc, argv);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("grep returned an exit status outside {0,1,2}", pat, rc, 0);

	return 0;
}
