/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_patch_main() -- src/util/patch.c, patch(1p)'s hand-written
 * diff(1)-format parser (normal, context, unified and ed-script hunk
 * grammars: is_normal_header()/parse_at_header()/parse_ctx_range()/
 * is_ed_header() and the four parse_*_section() readers built on them)
 * plus the fuzzy hunk-matching/application engine (find_match()'s
 * offset-then-outward-scan search, apply_section()'s single left-to-
 * right splice pass, apply_ed_section()'s direct line-numbered splice).
 * Applying an untrusted patch file to file content is exactly the class
 * of attack surface this project's fuzz harnesses target first (see
 * fuzz_sed.c's and fuzz_awk.c's own headers on hand-written-parser-over-
 * untrusted-input being the highest-value shape) -- hunk header integer
 * parsing (parse_uint(), is_normal_header()'s a/b/c/d fields), old/new
 * line-count bookkeeping (emit_hunk()'s del/add span walk), and
 * find_match()'s forward/backward offset arithmetic are all real,
 * bespoke code with no shared library beneath them the way sed/awk's
 * BRE matching sits on src/regex/regex.c.
 *
 * WHAT IS FUZZED, AND HOW. __util_patch_main()'s own argv parsing (read
 * in full above) takes the patch text only via `-i patchfile` or real
 * stdin -- there is no `-e`-style single-argument form the way sed has,
 * so unlike fuzz_sed.c there is no NUL-terminated-C-string question to
 * dodge in the first place: every fuzzer byte, NULs included, is written
 * to a file and handed to patch(1p) via `-i`, and read back with
 * getline() (read_all_lines()), which is binary-safe -- every `struct
 * pline` this file builds carries an explicit length, not a strlen()
 * assumption, so an embedded NUL flows through the parser and matcher
 * exactly like any other byte rather than needing to be filtered out
 * here the way fuzz_sed.c's/fuzz_ed.c's/fuzz_awk.c's argv-borne inputs
 * do.
 *
 * The one operand patch(1p) always needs -- the FILE being patched,
 * required outright for the normal/ed formats (src/util/patch.c's own
 * header: "neither of which carries any filename at all in its own
 * patch text") and preferred over pick_target_name()'s own header-name
 * guessing for context/unified too -- is a small, fixed, FIXED-CONTENT
 * six-line fixture, the same shape fuzz_sed.c's and fuzz_ed.c's own
 * fixture() functions use and for the identical reason given in both:
 * the grammar under test is the patch file's, not the target file's, so
 * the target is only varied enough (plain text, digits, tabs, blank
 * lines, `&`/`\` literals) to give context matching and old/new-line
 * comparison something real, non-trivial content to match against or
 * fail to match against.
 *
 * `-o <sink>` is always passed, so a successful application is written
 * to a fixed throwaway file rather than back over the fixture, which is
 * therefore written exactly ONCE (fixture() below, guarded by a `done`
 * flag exactly like fuzz_sed.c's/fuzz_ed.c's own) rather than rewritten
 * per call: unlike either of those two files' targets, this one is never
 * an output of the code under test (src/util/patch.c's own header: "-o
 * redirects all output elsewhere and never touches the target file"),
 * so there is nothing for a later call to have left behind that an
 * earlier call's reproducer would need undone. This does cost real
 * coverage on purpose: -b's own write_linebuf() backup-write path is
 * "a no-op alongside [-o]" per that same header comment, so -b's bit
 * below (see OPTIONS FUZZED) never actually exercises write_linebuf()'s
 * write -- only the option-parsing arm that sets `o.b`. A second harness
 * variant without `-o` would be needed to cover that one write path;
 * not added here because it would also reintroduce exactly the
 * accumulating-in-place-target problem the fixed `-o` sink avoids.
 *
 * `-d dir` is never passed: it chdir()s the whole process, which no
 * other harness in this tree does either (nothing here needs a second
 * relative-path base, and a harness that chdir()'d away from its own
 * fixed /tmp paths would break every subsequent call's -i/-o/operand
 * arguments, which are relative-safe only because the process starts and
 * stays in one place).
 *
 * OPTIONS FUZZED: byte 0 of the input selects -b, -l, -N, -R (one bit
 * each) and one of {auto-detect, -c, -e, -n, -u} (three bits, values 5-7
 * folding back to auto) for the format-forcing flags -- auto-detection
 * (detect_at()) is deliberately the majority case (values 0 and 5-7 of
 * an 8-value field) since it is what a real, no-flags `patch < diff`
 * invocation does and is itself real code under test (parse_patch_stream
 * ()'s scan-for-a-recognizable-header-line loop). -R combined with -e
 * and -D combined with -e are both real, documented refusals
 * (__util_patch_main()'s own early "-R cannot be used with ed scripts"/
 * "-D cannot be used with ed scripts" checks) that this harness reaches
 * directly rather than avoiding.
 *
 * -D (the #ifdef/#ifndef wrapping option) and -p (path-component
 * stripping) are NOT fuzzed: -D only changes emit_hunk()'s output
 * formatting, never its matching, and -p only matters for
 * pick_target_name()'s own name-guessing, which this harness bypasses
 * by always supplying the file operand explicitly (see above) -- neither
 * adds parser/matcher coverage worth the extra option-byte bookkeeping.
 *
 * BOUNDING RUNAWAY COMPUTATION: NOT NEEDED HERE, and why, checked
 * directly rather than assumed by analogy to fuzz_sed.c's b/t concern
 * (that file's own header raises exactly this question for its target
 * and answers it differently for ed's g/v in fuzz_ed.c, so it is asked
 * again here rather than skipped). Every loop in src/util/patch.c is
 * bounded by a quantity this harness already caps or that shrinks every
 * iteration:
 *
 *   - parse_patch_stream()'s section loop and each parse_*_hunk() reader
 *     walk `patch_lines`, a fixed array built once from the (PATCH_CAP-
 *     capped, below) patch file -- strictly bounded, no backward jump;
 *   - find_match()'s forward/backward scan is bounded by `target->n`,
 *     the FIXED six-line fixture's own line count, not by anything the
 *     patch file controls;
 *   - apply_section()'s hunk loop is bounded by `pf->nhunks`, itself
 *     bounded by how many hunk headers PATCH_CAP bytes can encode;
 *   - apply_ed_section()'s edcmd loop is the same shape, bounded by
 *     `pf->neds`.
 *
 * No script text here can make any of these loops re-visit already-
 * consumed input the way sed's `b`/`t` can re-enter the same command
 * text indefinitely -- patch(1p)'s grammar has no branch or label
 * construct at all, matching fuzz_ed.c's own finding for ed's g/v.
 * PATCH_CAP still bounds the absolute cost of a worst case (many small
 * hunks, each triggering a full find_match() scan of the six-line
 * fixture), for the same "keep it cheap in absolute terms" reason
 * fuzz_sed.c's SCRIPT_CAP and fuzz_ed.c's CMD_CAP give.
 *
 * STDERR REDIRECTION. Every diagnostic path here (a malformed patch, a
 * hunk that could not be matched, an -R/-e or -D/-e refusal) writes via
 * __util_diagf() to the real process stderr, and a malformed-input
 * result is the overwhelming common case while fuzzing -- exactly
 * fuzz_sed.c's own reasoning for redirecting stdout/stderr once per call
 * with freopen() rather than fopen(), reusing the existing `FILE *const
 * stderr` object per include/stdio.h's own header comment. There is no
 * stdout use in src/util/patch.c to redirect (read in full: every write
 * goes through -i/-o/-r/-b's own fopen()'d FILE*s or stderr, never
 * `stdout`), so only stderr is touched here.
 *
 * NO ORACLE. Same shape as fuzz_sed.c's and fuzz_ed.c's own reasoning:
 * no reference patch(1p) implementation this project could differential-
 * test against without every one of this file's own documented, real
 * scope narrowings (single-pass hunk application with no reordering,
 * the -o-with-multi-section corner case, no SCCS/RCS retrieval or
 * interactive prompting) reading as a false mismatch. What IS checked is
 * the same two-part contract fuzz_sed.c/fuzz_ed.c check:
 *
 *   - src/internal/util.h's banner: a real process exit status, never a
 *     raw errno or a boolean. src/util/patch.c's own EXIT STATUS section
 *     documents 0 (success), 1 (hunks rejected) and ">1" (error) but
 *     every `return` in the file reads (checked directly) uses exactly
 *     0, 1 or 2 -- so 2, not merely "> 1", is the real upper bound this
 *     build ever produces, and is what is asserted;
 *   - no exit()/_exit() call anywhere in src/util/patch.c (checked while
 *     writing this harness): __util_patch_main() has no bi_patch() shell-
 *     builtin caller today the way bi_sed()/bi_ed() do, but the same
 *     src/internal/util.h contract applies unconditionally to every
 *     __util_<name>_main(), and libFuzzer's own atexit-based "an exit()
 *     was detected" defence is what would surface a violation, the same
 *     backstop fuzz_sed.c and fuzz_ed.c both rely on rather than a
 *     bespoke check in this file.
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

#define ROOT "/tmp/patchfz"
#define PATCH_CAP 900

/* ==== fixture: a small, fixed target file patch(1p) applies hunks
 * against. Written once; see this file's header comment for why its
 * content is NOT derived from the fuzz input, and for why -o keeps it
 * untouched call after call. ================================================ */

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
	write_file(ROOT "/target", data, sizeof data - 1);
}

/* ==== stderr redirection -- see this file's header comment. ============== */

static int redirect_stderr(void)
{
	return freopen(ROOT "/err", "w", stderr) != 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned opts;
	size_t n;
	int status;
	char *argv[16];
	int argc = 0;
	int fmtsel;
	/* A NUL-terminated copy of (a prefix of) the patch bytes, purely
	 * for oracle_mismatch_i()'s own `in` argument below -- host_oracle.
	 * c's addq() scans with `for (; *s; s++)`, so it is unsafe to hand
	 * it the raw fuzzer buffer directly: that buffer is exactly `size`
	 * bytes with no guaranteed NUL anywhere inside it, unlike
	 * fuzz_sed.c's/fuzz_ed.c's script/cmds buffers, which explicitly
	 * NUL-terminate before ever being passed here. The patch file
	 * itself, written below, still gets every one of the real fuzzer
	 * bytes verbatim (embedded NULs included -- see this file's header
	 * comment on why that is safe for a file read with getline()); only
	 * this diagnostic copy is capped and terminated. */
	char diagbuf[PATCH_CAP + 1];

	if (size < 1) return 0;
	mkdir(ROOT, 0755);

	opts = data[0];
	data++; size--;

	n = size < PATCH_CAP ? size : PATCH_CAP;
	write_file(ROOT "/patch", (const char *)data, n);
	memcpy(diagbuf, data, n);
	diagbuf[n] = 0;

	/* Written once, like fuzz_sed.c's/fuzz_ed.c's own fixture()s -- and,
	 * unlike either of those (which run their target in place), never
	 * touched again afterward: -o below routes every successful
	 * application to a separate sink file, so the target this harness
	 * matches against stays exactly this fixed content call after call. */
	fixture();

	if (!redirect_stderr()) return 0;

	argv[argc++] = (char *)"patch";
	if (opts & 0x01) argv[argc++] = (char *)"-b";
	if (opts & 0x02) argv[argc++] = (char *)"-l";
	if (opts & 0x04) argv[argc++] = (char *)"-N";
	if (opts & 0x08) argv[argc++] = (char *)"-R";
	fmtsel = (opts >> 4) & 0x07;
	switch (fmtsel) {
	case 1: argv[argc++] = (char *)"-c"; break;
	case 2: argv[argc++] = (char *)"-e"; break;
	case 3: argv[argc++] = (char *)"-n"; break;
	case 4: argv[argc++] = (char *)"-u"; break;
	default: break; /* 0, 5, 6, 7: auto-detect */
	}
	argv[argc++] = (char *)"-i";
	argv[argc++] = (char *)ROOT "/patch";
	argv[argc++] = (char *)"-o";
	argv[argc++] = (char *)ROOT "/out";
	argv[argc++] = (char *)ROOT "/target";
	argv[argc] = 0;

	status = __util_patch_main(argc, argv);
	if (status < 0 || status > 2)
		oracle_mismatch_i("__util_patch_main returned outside {0,1,2}", diagbuf, status, 0);

	fflush(stderr);
	return 0;
}
