/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_csplit_main() -- src/util/csplit.c's own `arg` grammar (three
 * forms implemented: a bare `line_no`; `/regexp/[offset]`, which creates
 * a piece; `%regexp%[offset]`, the identical match but with "no file
 * shall be created for the selected section"; plus the
 * deliberately-refused `{num}` repeat-count form) -- extract_delimited()'s
 * delimiter/backslash-escape scanning, find_match()'s forward-from-`cur`
 * regcomp()/regexec() search (this project's own BRE engine,
 * src/regex/regex.c), and apply_offset()'s over/underflow-checked
 * arithmetic on the matched line plus a signed offset.
 *
 * The identical tokenized-argv shape fuzz_find.c and fuzz_sort.c both
 * use: the fuzz buffer, after one leading options byte, is split on NUL
 * bytes into up to CAP_TOKENS scratch-owned tokens, and each token
 * becomes ONE `arg` operand, in order -- so this harness always drives
 * `csplit ... FIXTURE TOK1 TOK2 ...`, letting later tokens see whatever
 * `cur` an earlier one left behind, which is what makes a SEQUENCE of
 * args exercise more control flow than any single one could (an
 * out-of-range `target < cur` on the second token, for instance, is only
 * reachable once a first token has already advanced `cur`). A token
 * beginning with '{' deliberately reaches the not-implemented `{num}`
 * refusal path rather than being filtered out.
 *
 * Byte 0 selects: bit 0 -s (suppresses the per-piece size line
 * write_piece() prints to stdout); bit 1 -k ("leave previously created
 * files intact" on error -- exercises cleanup_created()'s keep-vs-unlink
 * branch both ways). -f/-n are NOT fuzzed and never passed: the prefix
 * and digit count are held fixed (PREFIX, NDIGITS below) specifically so
 * this harness can predict every filename csplit might create and delete
 * it afterward.
 *
 * Unlike every read-only parser this project's fuzz_*.c harnesses
 * usually target, write_piece() really does fopen(name, "wb") a new file
 * per piece, and the SUCCESS path calls cleanup_created(&created, 1),
 * whose `if (!keep)` guard means a keep of 1 (success) NEVER unlinks:
 * real csplit(1p) is meant to leave its output behind. A harness that
 * just called __util_csplit_main() in a loop would litter ROOT with new
 * numbered files every successful iteration forever, and csplit's write
 * path can't be dodged by picking a read-only mode -- splitting into
 * files is the entire point. So PREFIX and NDIGITS are held fixed
 * precisely so every name write_piece() could possibly produce is
 * predictable -- piece numbers are assigned sequentially from 0 and
 * CAP_TOKENS bounds how many pieces one call can ever request -- and
 * reap_pieces() below unlinks every one of them unconditionally after
 * every single call, regardless of whether csplit itself already
 * removed them or deliberately kept them: unlink() on a name that was
 * never created simply fails and is ignored. The one file cleanup never
 * touches is FIXTURE itself, which lives outside the PREFIX namespace
 * and is written once by fixture().
 *
 * The fixture is a small, fixed ten-line text file (not derived from the
 * fuzz input) with three repeated "MARK ..." lines so a sequence of
 * /MARK/ or %MARK% tokens has more than one match to walk through in
 * order, and plain lines between them so a bare line-number arg and a
 * regexp arg can be mixed across one run.
 *
 * No spawn risk: csplit(1p) never invokes another program under any arg
 * form this file implements (checked while reading csplit.c in full).
 *
 * stdout/stderr are redirected: write_piece()'s per-piece size line
 * (unless -s) and every parse diagnostic would otherwise hit the real
 * terminal on every one of millions of calls.
 *
 * Checked: this file's EXIT STATUS section ("0 Successful completion."
 * ">0 An error occurred.") -- and reading __util_csplit_main() in full
 * shows every `return` in it is actually exactly 0 or 1
 * (`return had_error ? 1 : 0;` at the end, and every earlier usage-error
 * return is a literal `1`), so the assertion below checks that real,
 * narrower range rather than a looser ">0" guess.
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

/* fixture: a small, fixed multi-line file -- see this file's header
 * comment for why its content is NOT derived from the fuzz input. */

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

/* stdout/stderr redirection -- see this file's header comment. */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

/* Predictable-filename cleanup -- see this file's header comment. */

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
