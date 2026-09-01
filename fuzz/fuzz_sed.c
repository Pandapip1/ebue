/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_sed_main() -- src/util/sed.c, sed(1p)'s hand-written script
 * parser (addresses, s///, y///, a/i/c text, the b/t/: branch graph,
 * '{'...'}' blocks) and its BRE-driven pattern-space/hold-space
 * execution engine sharing src/regex/regex.c's regcomp()/regexec()
 * with everything else in this tree.
 *
 * WHAT IS FUZZED, AND HOW.  The fuzz buffer is sed's *script*: this
 * file's own header comment identifies the parser as "the highest-
 * value target", so every byte of fuzzer input becomes the contents of
 * a script file handed to sed via `-f`, and the *data* sed edits is a
 * small fixed six-line fixture (fixture_data() below) written once,
 * not derived from the fuzz input at all -- the grammar under test is
 * the script's, not the data's.  `-f` rather than `-e`: an `-e`
 * argument has to be a NUL-terminated C string built from fuzz bytes
 * that may contain an embedded NUL, the same reason fuzz_shparse.c
 * rejects an embedded NUL outright rather than silently truncating (its
 * own comment: "not one program"); `-f` reads the file with fread(),
 * sidestepping the question entirely -- and an embedded NUL is still
 * rejected below anyway, so the script this harness parses is always
 * the whole of what libFuzzer generated, once.
 *
 * Byte 0 of the input selects `-n` (bit 0); the rest, capped at
 * SCRIPT_CAP, is the script.  Both SYNOPSIS forms in src/util/sed.c's
 * own header comment collapse to the same `-f` form here, since a bare
 * first-operand script and an `-e`-supplied one both end up parsed by
 * the identical parse_script() -- there is no separate code path an
 * argument-form script would reach that a script FILE does not.
 *
 * WHAT IS DELIBERATELY NOT FUZZED.  The *data* file's content: giving
 * the fuzzer control of it would trade coverage of the parser (a real,
 * substantial hand-written grammar with address forms, four kinds of
 * delimiter-escaping, and a label/branch graph) for coverage of BRE
 * matching against arbitrary text, which fuzz_regex.c already owns and
 * owns better -- its harness compares against nothing here, is capped
 * for exactly this reason, and this file's own subject strings need
 * only be varied enough to give s///, y///, and the address forms
 * something real to act on (fixture() picks six lines for that: plain
 * text, digits, tabs, and s///'s own metacharacters `&` and `\`
 * appearing literally in the data, so a backreference or whole-match
 * replacement has something worth substituting).
 *
 * REDIRECTED, NOT DROPPED: `stdout`/`stderr`.  Every one of sed's p,
 * P, l, =, the default per-cycle auto-print, and __util_diagf()'s own
 * error diagnostics for a malformed script (the common case when
 * fuzzing) write to the *real* process stdout/stderr -- and this
 * harness runs those millions of times without a fork, so writing them
 * to the terminal would drown the run in its own output long before it
 * found anything.  include/stdio.h's own header comment on freopen()
 * is the reason this is fixable at all: `stdout`/`stderr` are `FILE
 * *const` in this libc, so redirecting them means reusing the existing
 * FILE* object, which is exactly what freopen() does and fopen()
 * cannot.  Done once per call (not once per process) so the sink file
 * never grows past one iteration's output -- freopen()'s own "w" mode
 * truncates it every time, at the cost of one open/close per call,
 * which is cheap next to sed's own parse+execute.
 *
 * BOUNDING RUNAWAY COMPUTATION.  Two distinct risks, not one:
 *
 *   - regcomp()/regexec(): already bounded at the library level (see
 *     fuzz_regex.c's own long banner on MAX_STEPS and REG_ESPACE) --
 *     every regexec() call, however pathological its pattern, returns
 *     in bounded time rather than hanging or overflowing the stack.
 *     This file additionally caps the script at SCRIPT_CAP bytes, which
 *     caps how many s/// commands (and therefore how many regexec()
 *     calls against the six-line, individually-short fixture data) a
 *     single input can ever trigger, so the *cumulative* cost per call
 *     stays a small, bounded number of seconds even in the worst case.
 *     No safe_to_exec()-style pattern filter is needed here for the
 *     reason its own banner gives for adding one in the first place --
 *     that filter exists to dodge a per-call cost on the order of a
 *     second, which this file's own script-length cap already bounds
 *     the same way, without narrowing what patterns get compiled.
 *
 *   - sed's b/t branch graph is a real loop construct with NO bound of
 *     its own, at any level -- src/util/sed.c's run_program() is a
 *     plain `while (pc < (long)pr->n)` that a 'b' or 't' command can
 *     reset arbitrarily, and nothing there counts iterations.  A script
 *     as short as ":x;bx" is a correct, POSIX-permitted sed program
 *     that never terminates -- ordinary semantics, not a bug -- and it
 *     is trivial for a mutator to find inside a 256-byte budget.
 *     tools/fuzz.sh's own comment block above `watchdog=` explains why
 *     libFuzzer's per-unit -timeout is turned off entirely on this host
 *     (SIGALRM never reaches the process; see that comment for the
 *     mechanism) and replaced with ONE watchdog around the *whole*
 *     -max_total_time run -- so an unbounded script here would not
 *     just cost one slow input, it would consume every remaining
 *     second of the run's budget in a single call and then be killed
 *     by that outer watchdog with nothing learned, exactly the failure
 *     mode fuzz_regex.c's safe_to_exec() was written to keep regexec()
 *     away from.  That IS the check this file's own task asked to make
 *     before adding a bound, and the answer is: no, the existing
 *     backstop is a whole-run safety net, not a per-input one, so a
 *     per-input bound belongs here.
 *
 *     sed_may_loop_forever() below is that bound, applied to the
 *     *script text* before __util_sed_main() is ever called (this file
 *     has no access to the parsed struct program -- parsing and
 *     execution both happen inside the one opaque entry point -- so a
 *     source-level scan is the only seam available).  It is
 *     deliberately approximate, in the same spirit and for the same
 *     reason as fuzz_regex.c's safe_to_exec(): a scanner that
 *     re-derived the real parser's full grammar would just be a second,
 *     divergence-prone copy of parse_script(), so this one is
 *     conservative on purpose in ONE specific direction -- see its own
 *     comment for exactly which shape it flags and why every
 *     misclassification it can make either still runs the input (a
 *     lost hazard, tolerable: the outer watchdog is the fallback of
 *     last resort) or skips an input that would have been safe to run
 *     (a lost coverage sample, tolerable: the vast majority of scripts
 *     have no backward branch at all and are never touched by this
 *     scan's classification).
 *
 * NO ORACLE.  Like fuzz_printf.c and fuzz_strtod.c's strtold half, sed
 * has no reference implementation this project can compare against
 * without dragging in GNU sed's well-known non-POSIX extensions (this
 * file's own header comment lists several XCU deliberately narrows
 * against) and calling every divergence a bug.  What is checked is
 * what this project's own contract states unconditionally:
 *
 *   - src/internal/util.h's header banner: __util_sed_main() "returns a
 *     real process exit status ... never a raw errno or a boolean."
 *     src/util/sed.c's own driver returns only 0 or 1 on every path (a
 *     literal reading of the file turns up no third value), so 0/1 is
 *     asserted directly, not merely assumed;
 *   - the same banner's other half -- bi_sed() (src/sh/builtin.c) runs
 *     this exact function in-process, no fork -- so a call to exit()/
 *     _exit() on any malformed-script path would kill the whole
 *     fuzzing process, not just fail one input.  This harness does not
 *     special-case that: it relies on libFuzzer's own documented
 *     defence (an atexit hook that reports "libFuzzer: deadly signal"/
 *     "an exit() was detected" when the target calls exit() mid-run)
 *     to surface it as a finding, the same as any other unexpected
 *     process-ending event a target could trigger.  A grep of
 *     src/util/sed.c today finds no exit()/_exit() call at all; this
 *     is the check that keeps it that way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define ROOT "/tmp/sedfz"
#define SCRIPT_CAP 480
#define MAXSEG (SCRIPT_CAP + 1)

/* ==== fixture: a small, fixed data file sed edits.  Built once; see
 * this file's header comment for why its content is NOT derived from
 * the fuzz input. ============================================================ */

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
		"hello world\n"
		"foo123bar\n"
		"\ttabbed\tfield\n"
		"special & chars \\ here\n"
		"\n"
		"the quick brown fox jumps\n";

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
	write_file(ROOT "/data", data, sizeof data - 1);
}

/* ==== sed_may_loop_forever(): a deliberately approximate scan for the
 * one shape of runaway computation this harness cannot otherwise bound
 * -- see this file's header comment for the reasoning and for what
 * each direction of misclassification costs. ================================
 *
 * The script is split on every UNESCAPED ';' or <newline> (backslash
 * parity tracked across the whole script, so a real a\/i\/c\
 * continuation -- "\<newline>" -- does not split, matching
 * parse_text_arg()'s own rule that only an unescaped <newline> ends the
 * text) into segments approximating one command apiece.  Each segment
 * is then read exactly the way parse_script() reads one: an optional
 * address (skip_one_address() below is parse_one_address()'s grammar,
 * loosely -- digits, '$', or a delimited /BRE/ or \cBREc, optionally
 * twice for a range), optional blanks and '!', and then the command
 * byte itself.
 *
 * A segment starting with ':' is a label; one starting with 'b' or 't'
 * is a branch, whose (rest-of-segment, blank-trimmed) operand is its
 * target -- an EMPTY operand branches to end of script, which
 * terminates the cycle and is never a hazard, exactly as resolve_
 * program()'s own "bare b/t targets pr->n" rule treats it.
 *
 * A branch is flagged HAZARDOUS -- and the whole script rejected -- iff
 * its target label was already seen at or before the branch's own
 * segment (a backward, including self, branch) AND no segment strictly
 * between the label and the branch starts with n, N, d, D, q or Q:
 * every one of those either ends the current cycle outright (d, D when
 * the pattern space has no <newline> left, q) or consumes the next
 * input line (n, N) -- either way, a real bound on how many times the
 * backward edge can be crossed before SOMETHING external changes,
 * which an unconditional b/t alone never provides.
 *
 * This is sound in exactly the direction that matters and unsound in
 * the other, on purpose, mirroring safe_to_exec()'s own rule in
 * fuzz_regex.c ("every uncertainty is resolved as 'not safe'") but
 * pointed the other way, because the base rate is the opposite one:
 * there safe patterns were the overwhelming majority and only a narrow
 * shape was excluded from safety; here scripts with a backward branch
 * at all are already the rare case, so treating every one of THOSE
 * conservatively (flag it unless a consuming command is textually
 * between the label and the branch, never mind whether it is on every
 * path a real interpreter could take) loses only a sliver of coverage.
 * What it can still get wrong, and why each is tolerable:
 *
 *   - a false negative: a script this scan clears that still loops
 *     forever, because the "consuming" command it found sits inside an
 *     address-gated block/command that never actually fires for the
 *     line reaching this point (the scan does not evaluate addresses,
 *     it only checks that the byte is present).  tools/fuzz.sh's own
 *     outer watchdog is the backstop for exactly this residual case --
 *     see this file's header comment on why that backstop is adequate
 *     for a rare miss but not for the common, trivially-mutated
 *     ":x;bx" shape this function exists to catch before it ever
 *     reaches that backstop.
 *   - a false positive: a script flagged and skipped that would in
 *     fact have terminated (e.g. the "consuming" command this scan
 *     credited is itself inside a '{'...'}' block gated by an address
 *     that never matches, so it never really runs, but neither does
 *     the loop skip past it in a way that matters -- or, conversely, a
 *     'b'/'t' this scan treated as unconditional actually carries an
 *     address that makes it fire at most once).  Purely a lost
 *     sample, never a correctness question -- __util_sed_main() is
 *     simply never called on that input.
 */
struct sed_seg { size_t off, len; };

static size_t sed_split_segments(const char *s, size_t n, struct sed_seg *out, size_t maxout)
{
	size_t i, start = 0, cnt = 0;
	unsigned bs = 0;

	for (i = 0; i < n; i++) {
		char c = s[i];
		if (c == '\\') { bs++; continue; }
		if ((c == ';' || c == '\n') && (bs % 2) == 0) {
			if (cnt < maxout) { out[cnt].off = start; out[cnt].len = i - start; cnt++; }
			start = i + 1;
		}
		bs = 0;
	}
	if (cnt < maxout) { out[cnt].off = start; out[cnt].len = n - start; cnt++; }
	return cnt;
}

static const char *sed_skip_ws(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	return p;
}

static void sed_skip_one_address(const char **pp, const char *end)
{
	const char *p = *pp;

	if (p < end && *p >= '0' && *p <= '9') {
		while (p < end && *p >= '0' && *p <= '9') p++;
		*pp = p;
		return;
	}
	if (p < end && *p == '$') { *pp = p + 1; return; }
	if (p < end && (*p == '/' || (*p == '\\' && p + 1 < end))) {
		char delim = (*p == '/') ? '/' : p[1];
		p += (*p == '/') ? 1 : 2;
		while (p < end && *p != delim) {
			if (*p == '\\' && p + 1 < end) p += 2;
			else p++;
		}
		if (p < end) p++; /* closing delimiter */
		*pp = p;
	}
}

enum sed_seg_kind { SK_OTHER, SK_LABEL, SK_BRANCH, SK_CONSUME };

static enum sed_seg_kind sed_classify(const char *s, size_t off, size_t len,
                                       const char **label, size_t *labellen)
{
	const char *p = s + off, *end = s + off + len;

	p = sed_skip_ws(p, end);
	sed_skip_one_address(&p, end);
	p = sed_skip_ws(p, end);
	if (p < end && *p == ',') {
		p++;
		p = sed_skip_ws(p, end);
		sed_skip_one_address(&p, end);
		p = sed_skip_ws(p, end);
	}
	while (p < end && *p == '!') { p++; p = sed_skip_ws(p, end); }
	if (p >= end) return SK_OTHER;

	if (*p == ':') {
		const char *q;
		p++;
		p = sed_skip_ws(p, end);
		q = p;
		while (q < end && *q != ' ' && *q != '\t') q++;
		*label = p;
		*labellen = (size_t)(q - p);
		return SK_LABEL;
	}
	if (*p == 'b' || *p == 't') {
		p++;
		p = sed_skip_ws(p, end);
		*label = p;
		*labellen = (size_t)(end - p);
		return SK_BRANCH;
	}
	if (*p == 'n' || *p == 'N' || *p == 'd' || *p == 'D' || *p == 'q' || *p == 'Q')
		return SK_CONSUME;
	return SK_OTHER;
}

struct sed_label { const char *name; size_t len; size_t idx; };

static int sed_may_loop_forever(const char *s, size_t n)
{
	static struct sed_seg segs[MAXSEG];
	static struct sed_label labels[MAXSEG];
	size_t nseg, nlabels = 0, i;

	nseg = sed_split_segments(s, n, segs, MAXSEG);

	for (i = 0; i < nseg; i++) {
		const char *lab;
		size_t lablen;
		enum sed_seg_kind k = sed_classify(s, segs[i].off, segs[i].len, &lab, &lablen);

		if (k == SK_LABEL) {
			if (nlabels < MAXSEG) {
				labels[nlabels].name = lab;
				labels[nlabels].len = lablen;
				labels[nlabels].idx = i;
				nlabels++;
			}
		} else if (k == SK_BRANCH && lablen != 0) {
			size_t j;
			for (j = 0; j < nlabels; j++) {
				size_t between;
				int consumes;

				if (labels[j].len != lablen || labels[j].idx > i) continue;
				if (memcmp(labels[j].name, lab, lablen) != 0) continue;

				consumes = 0;
				for (between = labels[j].idx; between <= i; between++) {
					const char *l2;
					size_t ll2;
					if (sed_classify(s, segs[between].off, segs[between].len, &l2, &ll2) == SK_CONSUME) {
						consumes = 1;
						break;
					}
				}
				if (!consumes) return 1;
			}
		}
	}
	return 0;
}

/* ==== stdout/stderr redirection -- see this file's header comment. ======== */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char script[SCRIPT_CAP + 1];
	size_t n;
	int opt_n;
	int status;
	char *argv[6];
	int argc = 0;

	if (size < 1) return 0;
	fixture();

	opt_n = data[0] & 1;
	data++; size--;

	n = size < SCRIPT_CAP ? size : SCRIPT_CAP;
	memcpy(script, data, n);
	script[n] = 0;
	if (memchr(script, 0, n)) return 0; /* embedded NUL: not one script */

	if (sed_may_loop_forever(script, n)) return 0;

	write_file(ROOT "/script", script, n);

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"sed";
	if (opt_n) argv[argc++] = (char *)"-n";
	argv[argc++] = (char *)"-f";
	argv[argc++] = (char *)ROOT "/script";
	argv[argc++] = (char *)ROOT "/data";
	argv[argc] = 0;

	status = __util_sed_main(argc, argv);
	if (status != 0 && status != 1)
		oracle_mismatch_i("__util_sed_main returned neither 0 nor 1", script, status, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
