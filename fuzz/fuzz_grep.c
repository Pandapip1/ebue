/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_grep_main() -- src/util/grep.c's regex-driven line filter,
 * shared with src/sh/builtin.c's bi_grep() as an in-process shell
 * builtin (no fork/exec: see that file's own header and
 * src/internal/util.h's).  fuzz/fuzz_regex.c already fuzzes
 * regcomp()/regexec() -- src/regex/regex.c's parser, bytecode emitter and
 * bounded backtracking VM -- directly and at length; read that file's
 * header comment in full before this one, because this harness
 * deliberately does NOT re-fuzz the regex engine's own grammar (bracket
 * expressions, backreferences, interval counts, the historical
 * stack-overflow-on-a-nullable-repeat defect that file's banner
 * documents as fixed).  What this harness fuzzes instead is the layer
 * grep.c adds on top of that engine:
 *
 *   - OPTION HANDLING: -E/-F (genuinely mutually exclusive, checked by
 *     grep.c itself -- violating this is expected to return 2, not
 *     silently pick one), -i, -v, -x, -n, -s, and the -c/-l/-q
 *     mutually-exclusive trio; -e supplied either attached
 *     ("-e<pattern>") or as a separate argv element, per this project's
 *     own "-xVALUE or -x VALUE" attachment convention (grep.c's own
 *     header comment); a bare pattern_list operand with neither -e nor
 *     -f, exercising the third SYNOPSIS form.
 *   - split_patterns(): a single argv operand (from -e or the bare
 *     pattern_list operand) is itself split on embedded '\n' into one or
 *     more patterns, each tried against every line -- an embedded '\n'
 *     in the fuzzed pattern bytes is deliberately let through rather
 *     than filtered out, so this gets exercised, including the "-e"
 *     attached form's own value containing a '\n'.
 *   - -x's whole-line-match logic (checking rm_so==0 &&
 *     (size_t)rm_eo==linelen against a REG_NOSUB-free compile), which
 *     depends on regexec()'s leftmost-longest guarantee but is grep.c's
 *     own code, not the engine's.
 *   - -F's strstr()/strcasestr() literal-match path (-i combined with
 *     -F, and -x combined with -F's separate whole-token-compare
 *     branch), which never touches the regex engine at all.
 *   - getline()'s line splitting over content that may or may not end in
 *     a final newline (INPUT FILES: "a missing final newline... is
 *     preserved exactly on output", per grep.c's own header), and -c/-l/
 *     -n/-q/-v's per-line vs. deferred-to-EOF output decisions.
 *
 * DELIBERATELY EXCLUDED: -f pattern_file (add_pattern_file()'s own
 * getline()-over-a-file loop is the same shape as scan_one()'s, already
 * exercised here for the searched content; not worth a second temp
 * file), and multiple file operands (this harness always passes exactly
 * one, so the "file:" STDOUT prefix's `multi` branch is never taken --
 * a one-line addition on top of the single-file path this harness
 * already drives at length, per grep.c's own header comment on the
 * STDOUT prefix rule).
 *
 * WHY A TEMP FILE, NOT STDIN.  include/stdio.h declares `stdin` as
 * `extern FILE *const stdin`, so it cannot be reassigned to
 * fmemopen()'s result the way a hosted libc's harness might.  grep
 * always needs a real file operand here (this harness never passes "-"
 * or omits the operand, both of which would read real stdin instead --
 * see the zero-operand hazard called out below), so the fuzzed
 * "content" half of the input is written to a fixed path under /tmp --
 * present from start-up in fuzz/ntstubs.c's simulated volume, the same
 * fact fuzz_glob.c's header comment records -- and overwritten on every
 * call.  libFuzzer runs one input at a time in this process, so a
 * fixed, reused path is safe.
 *
 * THE ZERO-OPERAND HAZARD, AND WHY THE ATTACHED "-e" FORM IS GUARDED.
 * grep.c's own -e parsing is "if arg[2] is non-NUL, that suffix is the
 * value; otherwise the *next* argv element is."  An attached "-e" whose
 * fuzzed pattern happens to be empty collapses to the literal string
 * "-e" -- indistinguishable, to grep.c, from "-e" meaning "take my value
 * from the next argv element."  This harness always appends exactly one
 * more argv element after the pattern (the temp file path), so an
 * empty-pattern attached "-e" would silently consume that path as the
 * *pattern* instead, leaving zero file operands -- and grep with zero
 * operands reads real stdin (STDIN section / OPERANDS' file
 * description), which this harness's process may not have closed,
 * risking a hang rather than a clean, fast return. The attached form is
 * therefore only used when the pattern is non-empty; an empty pattern
 * always goes through the separate-argv-element form instead, where
 * "-e" and "" are two distinct elements and no such collapse can happen.
 * The bare-operand (no -e/-f) form is separately guarded with a leading
 * "--" for the same reason, in case the fuzzed pattern happens to start
 * with '-' and would otherwise be read as more options (possibly another
 * value-taking one) rather than the pattern operand.
 *
 * WHAT IS CHECKED.  No differential oracle exists here any more than one
 * does for the underlying regex engine (fuzz_regex.c's header explains
 * why one was tried there, against glibc, and abandoned as noisier than
 * useful).  What is checked is the property XCU grep(1p)'s own EXIT
 * STATUS section states -- "0 One or more lines were selected... 1 No
 * lines were selected... >1 An error occurred" -- narrowed to the three
 * values grep.c's own code actually ever returns (0, 1, 2: read in full
 * while writing this harness, and no `return` in that file produces
 * anything else), so a fourth value would be a real regression rather
 * than a standards-permitted extension.
 *
 * THE exit()-VS-return DISCIPLINE (src/internal/util.h's header: every
 * __util_<name>_main() "returns a real process exit status... never a
 * raw errno or a boolean," precisely because bi_grep() runs this
 * in-process with no fork) is checked for free by construction: a stray
 * exit()/_exit() call from inside LLVMFuzzerTestOneInput would end the
 * fuzzing process itself, which libFuzzer reports distinctly from an
 * ordinary crash and which would be impossible to miss across a
 * multi-minute run. grep.c was read in full while writing this harness
 * and calls neither -- unlike src/util/expr.c's dupstr(), which did and
 * has been fixed alongside this harness; see fuzz_expr.c's header.
 *
 * SIZE CAPS.  Pattern 32 bytes, file content 48 bytes.  Kept small for
 * the same reason fuzz_regex.c's are, and the cost compounds here:
 * regexec()'s outer loop tries every start offset in a line and gives
 * each a fresh MAX_STEPS budget (src/regex/regex.c), so one long line
 * with no embedded '\n' is O(line_length * MAX_STEPS) -- no longer a
 * crash risk (that defect is fixed, per fuzz_regex.c's header), but a
 * real throughput cost this harness does not need to pay just to reach
 * grep.c's own option/pattern-list/line-splitting logic.
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

	use_attached = (opt2 & 0x08) != 0 && plen > 0;   /* see header: zero-operand hazard */
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
		argv[argc++] = a_dashdash;    /* pat is always an operand, never re-parsed as an option */
		argv[argc++] = pat;
	}
	argv[argc++] = tmppath;
	argv[argc] = NULL;

	rc = __util_grep_main(argc, argv);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("grep returned an exit status outside {0,1,2}", pat, rc, 0);

	return 0;
}
