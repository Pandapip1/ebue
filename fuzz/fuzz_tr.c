/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_tr_main() -- src/util/tr.c's string1/string2 grammar
 * (expand_spec(): literal chars, \-escapes, \ddd octal, c-c ranges,
 * [:class:], [=c=], and the string2-only [x*n] repeat), a
 * character-class/range parser in the same shape fuzz_fnmatch.c already
 * fuzzes for fnmatch()'s bracket scanner: a bracket/range grammar walked
 * byte-by-byte with no bound of its own beyond the input's length.
 *
 * string1 and string2 -- expand_spec(), the whole reason this harness
 * exists -- plus, more thinly, the translate/delete/squeeze loop that
 * runs after both operands parse, driven by a short fixed-cap slice of
 * input text. All three come from disjoint slices of the same fuzz
 * buffer, split the way fuzz_grep.c splits pattern from content: a
 * header byte picks the option combination, two more pick where each
 * split falls, and the three segments are capped independently
 * (CAP_S1/CAP_S2/CAP_TEXT below) so one huge segment cannot starve the
 * other two of coverage.
 *
 * string1/string2 are real argv elements (char*), so each is rejected
 * outright on an embedded NUL -- such an input doesn't describe one C
 * string. The TEXT tr(1p) translates is not an argv element -- tr.c
 * reads it a byte at a time from stdin via getchar() -- so it's written
 * verbatim to a file instead, embedded NULs and all: unlike
 * string1/string2, an embedded NUL in translated text is ordinary input
 * tr(1p) is specified to handle byte-for-byte.
 *
 * `stdin` is `FILE *const` here, so it can't be reassigned to a memory
 * buffer; freopen() reuses the existing FILE* object instead, onto a
 * fixed path under /tmp. stdout and stderr are freopen()ed too, onto
 * their own fixed paths: __util_tr_main() writes every translated byte
 * to real stdout and every diagnostic to real stderr, millions of times
 * with no fork.
 *
 * tr(1p)'s SYNOPSIS is exactly four forms; this harness's header byte
 * picks one of the four *and* whether string2 is supplied, biased
 * toward combinations __util_tr_main()'s own argument-combination check
 * actually accepts (string2 required with plain translate and with -ds,
 * forbidden with plain -d, optional with plain -s) so most inputs reach
 * expand_spec() rather than bouncing off the option check first. -c and
 * -C (complement) are exercised too: expand_spec() accepts them as
 * exact synonyms in this build.
 *
 * No runaway-computation concern, unlike fuzz_sed.c's b/t branch graph:
 * tr.c has no backward branch, no recursion, and no loop whose bound
 * depends on anything but the length of string1, string2 or the input
 * text -- every one of which this harness already caps.
 *
 * No oracle: a byte-for-byte comparison against GNU tr would mostly be
 * disagreements about extensions tr.c's own header documents as
 * deliberately narrowed (no real multi-byte/collation-aware -c/-C, no
 * equivalence classes beyond a single character), not defects. Checked
 * instead: tr.c's code was read in full and confirmed to only ever
 * return 0 (success), 1 (the one out-of-memory path, in the -c/-C
 * complement branch), or 2 (every argument/grammar error) -- a fourth
 * value would be a real regression.
 *
 * exit()-vs-return discipline checked for free by construction: tr.c was
 * read in full and calls neither exit() nor _exit() anywhere -- required,
 * since src/sh/builtin.c registers this as an in-process shell builtin
 * with no fork to contain a stray exit().
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
