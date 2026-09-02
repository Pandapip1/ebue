/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_uudecode_main() -- src/util/uudecode.c, uudecode(1p)'s
 * hand-written parser for a classic (non-Base64) uuencoded stream: a
 * "begin mode filename" header (mode read via strtoul(..., 8), filename
 * the rest of the line verbatim), a body of length-prefixed data lines
 * each decoded 4 characters at a time via uu_valid_char()/UUDEC()
 * (src/util/uucode.h), a zero-length terminator line, and a mandatory
 * trailing "end" line -- plus the chmod() that applies the header's
 * mode bits to the recreated file. This is exactly the "hand-written-
 * parser-over-untrusted-input" shape fuzz_patch.c's own header cites as
 * this project's highest-value fuzzing target, applied to a format
 * whose entire point is decoding a stream nothing has validated yet.
 *
 * WHAT IS FUZZED, AND HOW. Same shape as fuzz_patch.c's own harness (read
 * that file's header in full first): uudecode(1p) takes its whole input
 * as one file/stdin operand, not as multiple tokenized arguments the way
 * find(1p)'s predicate expression or cut(1p)'s -f list are, so there is
 * no separate lexer here to fuzz apart from the format parser itself.
 * Every fuzzer byte after the first is written to a file, verbatim,
 * embedded NULs included -- safe here for the identical reason
 * fuzz_patch.c's header gives for its own patch file: uudecode.c reads
 * with fgets() into a fixed 1024-byte line buffer, never assumes a
 * caller-supplied length via strlen(), and chomp()/strnlen() already
 * bound every line access to at most that buffer regardless of what an
 * embedded NUL does to a naive strlen() elsewhere.
 *
 * BYTE 0 selects one bit of real option coverage: whether "-o <fixed
 * path>" is passed. Left off (the majority case, and the one this file
 * exists to fuzz), the recreated file's path is *the header's own
 * filename field* -- attacker-controlled, sanitized not at all, per
 * this project's own header comment on uudecode.c ("No path
 * sanitization is applied to the header's filename ... used as given").
 * That is real, in-scope behaviour to fuzz, not a harness bug to route
 * around: unlike fuzz_find.c's -exec/-ok, nothing in uudecode.c's
 * do_unary()-equivalent path ever reaches __spawn() or any other real
 * process/host-filesystem primitive over that filename -- every open(),
 * fopen() and chmod() in this file goes through ntlibc's own I/O, which
 * in this harness (like every other harness in this directory) resolves
 * against fuzz/ntstubs.c's in-process simulated NT volume, not the real
 * host filesystem (see fuzz/Makefile's own long comment on
 * NTLIBC_FUZZ_MIRROR: only the corpus subtree it names is ever bound to
 * a real host path). A header filename of "../../etc/passwd" or
 * anything else a real uudecode would honour literally therefore still
 * only ever touches an in-memory tree private to this process, exactly
 * as fuzz_find.c's fixture() and fuzz_patch.c's write_file() already
 * rely on for their own ROOT-relative paths. Set, "-o" instead routes
 * the same decode through the *override* path uudecode.c's own header
 * documents ("-o only replaces where the file goes, not what mode it is
 * supposed to have") -- both code paths matter, so the option bit
 * exists to reach both rather than picking one permanently.
 *
 * SIZE CAP. INPUT_CAP bytes of the fuzz buffer become the file content,
 * the same "cap the absolute cost, let libFuzzer's own -max_len bound
 * typical runs" reasoning fuzz_patch.c's PATCH_CAP gives -- every loop in
 * decode_line()/the caller's data-line loop is bounded by the file's own
 * length (each line consumed shrinks what is left to fgets()), so there
 * is no separate runaway-computation concern to size against the way
 * fuzz_sed.c's b/t or fuzz_ed.c's g/v are, just an absolute-cost one.
 *
 * REDIRECTED: stderr only. Every diagnostic path here (no "begin" line,
 * a malformed header, an invalid/truncated data line, a missing "end"
 * line, an unopenable output path) writes via __util_diagf(), always to
 * stderr (src/internal/util.h's own definition) -- and a malformed
 * uuencoded stream is the overwhelming common case while fuzzing, the
 * same reasoning fuzz_patch.c's header gives for its own freopen(). This
 * file has no stdout use to redirect (read in full: every write goes
 * through fopen()'d FILE*s or stderr, never `stdout`), unlike its
 * encode-direction counterpart fuzz_uuencode.c.
 *
 * WHAT IS CHECKED. src/internal/util.h's contract: a real exit status,
 * never a raw errno or boolean. src/util/uudecode.c's every `return`
 * (read in full) uses exactly 0 or 1 -- there is no >1 "malformed
 * expression" class the way test(1p)/find(1p) have, uudecode(1p) itself
 * only distinguishes success from "an error occurred" -- so 1, not
 * merely "> 0", is the real upper bound this build ever produces, and is
 * what is asserted. No exit()/_exit() call anywhere in the file either
 * (checked while writing this harness); libFuzzer's own atexit-based
 * detection is the backstop for that, the same one fuzz_patch.c and
 * fuzz_sed.c both rely on rather than a bespoke check here.
 *
 * NO ORACLE beyond that exit-status contract, for the same reason
 * fuzz_patch.c's header gives: there is no reference uudecode(1p) this
 * project could differential-test against without every one of this
 * file's own real, documented scope narrowings (no Base64 -- that is
 * uuencode(1p)'s -m, refused outright, not a uudecode(1p) concern at
 * all; no "leading mail header" content-sniffing beyond the first line
 * literally starting "begin ") reading as a false mismatch.
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

#define ROOT "/tmp/uudecodefz"
#define INPUT_CAP 2048

static void write_file(const char *path, const char *data, size_t len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, data, len);
	close(fd);
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
	char *argv[6];
	int argc = 0;
	/* NUL-terminated diagnostic copy for oracle_mismatch_i()'s `in` --
	 * see fuzz_patch.c's identical field for why the raw fuzzer buffer
	 * cannot be handed to it directly (no guaranteed embedded NUL). The
	 * file written below still gets every real byte, embedded NULs
	 * included. */
	char diagbuf[INPUT_CAP + 1];

	if (size < 1) return 0;
	mkdir(ROOT, 0755);

	opts = data[0];
	data++; size--;

	n = size < INPUT_CAP ? size : INPUT_CAP;
	write_file(ROOT "/in", (const char *)data, n);
	memcpy(diagbuf, data, n);
	diagbuf[n] = 0;

	if (!redirect_stderr()) return 0;

	argv[argc++] = (char *)"uudecode";
	if (opts & 0x01) {
		argv[argc++] = (char *)"-o";
		argv[argc++] = (char *)ROOT "/out";
	}
	argv[argc++] = (char *)ROOT "/in";
	argv[argc] = 0;

	status = __util_uudecode_main(argc, argv);
	if (status < 0 || status > 1)
		oracle_mismatch_i("__util_uudecode_main returned outside {0,1}", diagbuf, status, 0);

	fflush(stderr);
	return 0;
}
