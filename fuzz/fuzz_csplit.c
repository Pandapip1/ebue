/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_csplit_main() -- src/util/csplit.c's own `arg` grammar (read
 * that file's header comment in full for the exact three forms
 * implemented: a bare `line_no`; `/regexp/[offset]`, which creates a
 * piece; `%regexp%[offset]`, the identical match but with "no file shall
 * be created for the selected section"; plus the deliberately-refused
 * `{num}` repeat-count form) -- extract_delimited()'s own delimiter/
 * backslash-escape scanning, find_match()'s forward-from-`cur` regcomp()/
 * regexec() search (this project's own BRE engine, src/regex/regex.c, per
 * that file's own header), and apply_offset()'s over/underflow-checked
 * arithmetic on the matched line plus a signed offset.
 *
 * WHAT IS FUZZED, AND HOW.  The identical tokenized-argv shape fuzz_find.c
 * and fuzz_sort.c both use (read fuzz_find.c's own header comment for the
 * general reasoning): the fuzz buffer, after one leading options byte, is
 * split on NUL bytes into up to CAP_TOKENS scratch-owned tokens, and each
 * token becomes ONE `arg` operand, in order -- so this harness always
 * drives `csplit ... FIXTURE TOK1 TOK2 ...`, letting later tokens see
 * whatever `cur` an earlier one left behind (find_match()'s own `from`
 * parameter), which is exactly what makes a SEQUENCE of args exercise
 * more of this file's control flow than any single one could (an
 * out-of-range `target < cur` on the second token, for instance, is only
 * reachable once a first token has already advanced `cur`).  A token
 * beginning with '{' deliberately reaches the NOT-implemented `{num}`
 * refusal path (this file's header comment's own "refused loudly" note)
 * rather than being filtered out -- that refusal running under fuzzer
 * input is coverage this harness exists to provide, not noise to avoid.
 *
 * OPTION BYTE.  Byte 0 selects: bit 0 -s (suppresses the per-piece size
 * line write_piece() prints to stdout); bit 1 -k ("leave previously
 * created files intact" on error -- exercises cleanup_created()'s
 * keep-vs-unlink branch both ways across the fuzzer's run, per this
 * file's own header comment on the default-vs--k EXIT STATUS behavior).
 * -f/-n are NOT fuzzed and are never passed: the prefix and digit count
 * are held fixed (PREFIX, NDIGITS below) specifically so this harness can
 * predict every filename csplit might create and delete it afterward --
 * see FILESYSTEM SIDE EFFECTS below.
 *
 * FILESYSTEM SIDE EFFECTS, AND WHY THIS HARNESS CLEANS UP AFTER ITSELF.
 * Unlike every read-only parser this project's fuzz_*.c harnesses usually
 * target, write_piece() really does fopen(name, "wb") a new file per
 * piece, and -- read directly in __util_csplit_main() -- the SUCCESS path
 * calls cleanup_created(&created, 1), whose own `if (!keep)` guard means
 * a keep of 1 (success) NEVER unlinks: real csplit(1p) is meant to leave
 * its output behind. A harness that just called __util_csplit_main() in a
 * loop would litter ROOT with new numbered files every successful
 * iteration forever -- exactly the "escapes the working directory across
 * a long-running fuzzing process" risk fuzz_pax.c's and fuzz_ar.c's own
 * headers describe for THEIR targets' write paths, except csplit's write
 * path cannot be dodged by picking a read-only mode the way -t/list mode
 * lets those two: splitting into files is the entire point of csplit(1p).
 * So PREFIX and NDIGITS are held fixed (never fuzzed, see OPTION BYTE
 * above) precisely so every name write_piece() could possibly produce is
 * predictable -- piece numbers are assigned sequentially from 0 and
 * CAP_TOKENS bounds how many pieces one call can ever request (at most
 * one explicit piece per token, plus the always-created implicit final
 * piece) -- and reap_pieces() below unlinks every one of them
 * unconditionally after every single call, regardless of whether csplit
 * itself already removed them (-k unset, an error occurred) or
 * deliberately kept them (success, or -k set): unlink() on a name that
 * was never created simply fails and is ignored, the same "don't check,
 * this is best-effort cleanup of a name this harness itself fully
 * predicts" reasoning fuzz_xargs.c's own scratch-directory handling uses.
 * The one file cleanup never touches is FIXTURE itself, which lives
 * outside the PREFIX namespace and is written once by fixture().
 *
 * THE FIXTURE.  A small, fixed ten-line text file (not derived from the
 * fuzz input, for the identical reason fuzz_cut.c's and fuzz_sort.c's own
 * fixed-fixture headers give) with three repeated "MARK ..." lines so a
 * sequence of /MARK/ or %MARK% tokens has more than one match to walk
 * through in order, and plain lines between them so a bare line-number
 * arg and a regexp arg can be mixed across one run.
 *
 * NO SPAWN RISK.  csplit(1p) never invokes another program under any arg
 * form this file implements (checked while reading src/util/csplit.c in
 * full, per this task's own instruction) -- so, unlike fuzz_find.c's
 * -exec/-ok, no argv-content safety exclusion is needed here.
 *
 * STDOUT/STDERR REDIRECTION: the same freopen()-a-fixed-sink-file-once-
 * per-call mechanism fuzz_find.c's, fuzz_ar.c's and fuzz_pax.c's own
 * header comments give, for the identical reason -- write_piece()'s own
 * per-piece size line (unless -s) and every parse diagnostic
 * (__util_diagf(), always stderr) would otherwise hit the real terminal
 * on every one of millions of calls.
 *
 * WHAT IS CHECKED.  This file's own header comment's EXIT STATUS section
 * ("0 Successful completion." ">0 An error occurred.") -- and reading
 * __util_csplit_main() in full shows every `return` in it is actually
 * exactly 0 or 1 (`return had_error ? 1 : 0;` at the very end, and every
 * earlier usage-error return is a literal `1`), so the assertion below
 * checks that real, narrower range rather than a looser ">0" guess.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_TOKENS 6
#define CAP_SCRATCH 256

#define ROOT "/tmp/csplitfz"
#define FIXTURE ROOT "/data"
#define PREFIX ROOT "/p"
#define NDIGITS 2

/* ==== fixture: a small, fixed multi-line file -- see this file's header
 * comment for why its content is NOT derived from the fuzz input. ======== */

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
		"line1 alpha\n"
		"line2 beta\n"
		"MARK one\n"
		"line4 gamma\n"
		"MARK two\n"
		"line6 delta\n"
		"line7 epsilon\n"
		"MARK three\n"
		"line9 zeta\n"
		"line10 eta\n";

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
	write_file(FIXTURE, data, sizeof data - 1);
}

/* ==== stdout/stderr redirection -- see this file's header comment. ======= */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

/* ==== predictable-filename cleanup -- see FILESYSTEM SIDE EFFECTS above. == */

static void reap_pieces(void)
{
	int n;
	char name[64];

	/* At most one explicit piece per token, plus the always-attempted
	 * implicit final piece: piece numbers 0..CAP_TOKENS cover every name
	 * write_piece() could possibly have used this call. */
	for (n = 0; n <= CAP_TOKENS; n++) {
		snprintf(name, sizeof name, "%s%0*d", PREFIX, NDIGITS, n);
		unlink(name);
	}
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opts;
	char scratch[CAP_SCRATCH];
	char *tok[CAP_TOKENS];
	int ntok = 0;
	size_t si = 0, wi = 0;
	/* "csplit" + -s + -k + FIXTURE + up to CAP_TOKENS args + NULL. */
	char *argv[CAP_TOKENS + 5];
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

	argv[argc++] = (char *)"csplit";
	if (opts & 0x01) argv[argc++] = (char *)"-s";
	if (opts & 0x02) argv[argc++] = (char *)"-k";
	argv[argc++] = (char *)FIXTURE;
	for (i = 0; i < ntok; i++) argv[argc++] = tok[i];
	argv[argc] = NULL;

	rc = __util_csplit_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 1)
		oracle_mismatch_i("__util_csplit_main returned outside {0,1}", diagbuf, rc, 0);

	reap_pieces();

	return 0;
}
