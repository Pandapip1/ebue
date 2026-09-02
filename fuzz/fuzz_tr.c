/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_tr_main() -- src/util/tr.c's string1/string2 grammar
 * (expand_spec(): literal chars, \-escapes, \ddd octal, c-c ranges,
 * [:class:], [=c=], and the string2-only [x*n] repeat), a
 * character-class/range parser in the same shape fuzz_fnmatch.c already
 * fuzzes for fnmatch()'s bracket scanner -- read that file's header
 * first for the general approach this one reuses (a bracket/range
 * grammar walked byte-by-byte with no bound of its own beyond the
 * input's length).
 *
 * WHAT IS FUZZED.  string1 and string2 -- src/util/tr.c's own grammar,
 * expand_spec(), the whole reason this harness exists -- plus, more
 * thinly, the translate/delete/squeeze loop that runs after both
 * operands parse, driven by a short fixed-cap slice of input text.  All
 * three (string1, string2, and the input text) come from disjoint
 * slices of the same fuzz buffer, split the way fuzz_grep.c splits
 * pattern from content: a header byte picks the option combination, two
 * more pick where each split falls, and the resulting three segments
 * are capped independently (CAP_S1/CAP_S2/CAP_TEXT below) so one huge
 * segment cannot starve the other two of coverage.
 *
 * WHY REAL argv OPERANDS FOR string1/string2, BUT NOT FOR THE TEXT.
 * string1/string2 are real argv elements (char*), so each is rejected
 * outright on an embedded NUL -- same reasoning fuzz_fnmatch.c and
 * fuzz_sed.c give: such an input does not describe one C string, and
 * accepting it would let the fuzzer spend its budget re-discovering the
 * same truncation under different names.  The TEXT tr(1p) translates is
 * not an argv element -- src/util/tr.c reads it a byte at a time from
 * stdin via getchar() -- so it is written verbatim to a file instead,
 * embedded NULs and all: unlike string1/string2, an embedded NUL in
 * translated text is perfectly ordinary input tr(1p) is specified to
 * handle byte-for-byte.
 *
 * WHY A TEMP FILE FOR STDIN, NOT fmemopen().  include/stdio.h declares
 * `stdin` as `extern FILE *const stdin` (checked directly before writing
 * this), so it cannot be reassigned to point at a memory buffer the way
 * a hosted libc's harness might -- the same restriction fuzz_grep.c's
 * own header documents for `stdin` in general.  freopen(), unlike a
 * plain assignment, reuses the existing FILE* object rather than
 * replacing it, so it is the seam this harness (and fuzz_sed.c, for
 * stdout/stderr) uses instead: the fuzzed text is written to a fixed
 * path under /tmp -- present from start-up in fuzz/ntstubs.c's
 * simulated volume, per fuzz_glob.c's own header -- and stdin is
 * freopen()ed onto it before every call.  stdout and stderr are
 * freopen()ed too, onto their own fixed paths, for the reason
 * fuzz_sed.c's header gives: __util_tr_main() writes every translated
 * byte to the real stdout and every diagnostic to the real stderr, and
 * this harness calls it millions of times with no fork.
 *
 * OPTION COMBINATIONS.  tr(1p)'s SYNOPSIS is exactly four forms (quoted
 * in full in src/util/tr.c's own header comment); this harness's header
 * byte picks one of the four *and* whether string2 is supplied, biased
 * toward combinations __util_tr_main()'s own argument-combination check
 * actually accepts (string2 required with plain translate and with -ds,
 * forbidden with plain -d, optional with plain -s) so most inputs reach
 * expand_spec() rather than bouncing off the option check before the
 * parser under test is ever entered -- the same reasoning fuzz_grep.c's
 * header gives for guarding its own zero-operand hazard.  -c and -C
 * (complement) are exercised too: expand_spec()'s own header comment
 * says they are accepted as exact synonyms in this build, and this
 * harness has no reason to prefer one over the other.
 *
 * NO RUNAWAY-COMPUTATION CONCERN, unlike fuzz_sed.c's b/t branch graph.
 * src/util/tr.c has no backward branch, no recursion, and no loop whose
 * bound depends on anything but the length of string1, string2 or the
 * input text -- every one of which this harness already caps -- so
 * there is nothing here that needs a sed_may_loop_forever()-style
 * pre-scan.
 *
 * NO ORACLE, same reasoning fuzz_printf.c's and fuzz_sed.c's headers
 * give: a byte-for-byte comparison against GNU tr would mostly be a
 * stream of disagreements about extensions src/util/tr.c's own header
 * comment already documents as deliberately narrowed (no real
 * multi-byte/collation-aware -c/-C, no equivalence classes beyond a
 * single character), not defects.  What is checked instead is
 * src/internal/util.h's own contract -- a real process exit status,
 * never a raw errno or boolean -- narrowed, like fuzz_grep.c's oracle,
 * to the values src/util/tr.c's own code was read in full and confirmed
 * to actually return: 0 (success), 1 (the one out-of-memory path, in
 * the -c/-C complement branch), or 2 (every argument/grammar error --
 * bad option, wrong operand count for the chosen form, a malformed
 * string1/string2, an empty string2).  A fourth value would be a real
 * regression.
 *
 * THE exit()-VS-return DISCIPLINE.  Checked for free by construction,
 * same as fuzz_grep.c's header explains: src/util/tr.c was read in full
 * while writing this harness and calls neither exit() nor _exit()
 * anywhere -- required, since src/sh/builtin.c registers this alongside
 * every other __util_<name>_main() as an in-process shell builtin with
 * no fork to contain a stray exit() (src/internal/util.h's own header
 * comment).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_S1   32
#define CAP_S2   32
#define CAP_TEXT 64

#define STDIN_PATH  "/tmp/fuzz_tr_stdin"
#define STDOUT_PATH "/tmp/fuzz_tr_stdout"
#define STDERR_PATH "/tmp/fuzz_tr_stderr"

static int write_file(const char *path, const char *buf, size_t len)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	if (len && fwrite(buf, 1, len, f) != len) { fclose(f); return 0; }
	return fclose(f) == 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char opts;
	size_t sp1, rem, sp2, s1len, s2len, textlen;
	char s1[CAP_S1 + 1], s2[CAP_S2 + 1];
	int form, comp_sel, include_s2;
	char argv0[] = "tr", a_c[] = "-c", a_C[] = "-C", a_d[] = "-d", a_s[] = "-s";
	char *argv[8];
	int argc = 0;
	int rc;

	if (size < 4) return 0;
	opts = data[0];
	sp1 = data[1];
	sp2 = data[2];
	data += 3; size -= 3;

	sp1 = size ? sp1 % size : 0;
	rem = size - sp1;
	sp2 = rem ? sp2 % rem : 0;

	s1len = sp1 < CAP_S1 ? sp1 : CAP_S1;
	s2len = sp2 < CAP_S2 ? sp2 : CAP_S2;
	textlen = rem - sp2;
	if (textlen > CAP_TEXT) textlen = CAP_TEXT;

	memcpy(s1, data, s1len); s1[s1len] = 0;
	if (memchr(s1, 0, s1len)) return 0; /* embedded NUL: not one operand */
	memcpy(s2, data + sp1, s2len); s2[s2len] = 0;
	if (memchr(s2, 0, s2len)) return 0;

	/* The text is not an argv operand: embedded NULs are fine here (see
	 * this file's header). */
	if (!write_file(STDIN_PATH, (const char *)(data + sp1 + sp2), textlen))
		return 0;

	form = opts & 0x03;             /* 0=translate 1=squeeze 2=delete 3=delete+squeeze */
	comp_sel = (opts >> 2) & 0x03;  /* 0/3=none 1=-c 2=-C */

	switch (form) {
	case 0: include_s2 = 1; break;               /* plain translate: required */
	case 1: include_s2 = (opts >> 4) & 1; break;  /* -s alone: optional */
	case 3: include_s2 = 1; break;                /* -ds: required */
	default: include_s2 = 0; break;               /* -d alone: forbidden */
	}

	argv[argc++] = argv0;
	if (comp_sel == 1) argv[argc++] = a_c;
	else if (comp_sel == 2) argv[argc++] = a_C;
	if (form == 2 || form == 3) argv[argc++] = a_d;
	if (form == 1 || form == 3) argv[argc++] = a_s;
	argv[argc++] = s1;
	if (include_s2) argv[argc++] = s2;
	argv[argc] = 0;

	if (!freopen(STDIN_PATH, "r", stdin)) return 0;
	if (!freopen(STDOUT_PATH, "w", stdout)) return 0;
	if (!freopen(STDERR_PATH, "w", stderr)) return 0;

	rc = __util_tr_main(argc, argv);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("__util_tr_main returned outside {0,1,2}", s1, rc, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
