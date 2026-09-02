/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_uuencode_main() -- src/util/uuencode.c, uuencode(1p)'s encoder:
 * emit_line()'s 45-bytes-in/4-characters-per-3-bytes-out chunking over
 * src/util/uucode.h's UUENC() mapping, the "begin mode decode_pathname"
 * header (mode read via fstat() when a real source_file is given, a
 * fixed 0644 default over stdin), and the zero-length-line-then-"end"
 * terminator.
 *
 * LOWER PRIORITY THAN fuzz_uudecode.c, ON PURPOSE, PER THIS PROJECT'S
 * OWN TASK BRIEF: uuencode(1p) turns arbitrary bytes into printable
 * output through a fixed, small, non-branching per-3-byte transform
 * (emit_line() has exactly one shape for every input, no format
 * variant, no untrusted structure to misparse) -- encoding arbitrary
 * bytes is inherently lower-risk than uudecode.c's job of *parsing* a
 * stream shaped like a real format, which is exactly why fuzz_uudecode.c
 * is this pair's higher-value harness and this one exists mainly for
 * completeness (basic crash/hang/exit-status coverage of a real, if
 * simple, hand-written loop) rather than because a rich bug surface is
 * expected here.
 *
 * WHAT IS FUZZED, AND HOW. uuencode(1p) reads source_file (or stdin) as
 * raw bytes to encode -- there is no format for this harness's fuzz
 * buffer to look like the way fuzz_uudecode.c's does, so, mirroring
 * fuzz_patch.c's target-file treatment, every fuzzer byte after the
 * first becomes the literal content of a file this harness creates,
 * NUL-safe because it is written with write(2) (never strlen()) and read
 * back with fread() (never a NUL-terminated-string API).
 *
 * BYTE 0 selects three independent bits of real option/operand coverage:
 *
 *   bit 0  one-operand (stdin) vs. two-operand (source_file) form. Set,
 *          real process stdin is freopen()'d from the same fuzz-content
 *          file (so uuencode(1p)'s fread(buf, ..., in) with `in == stdin`
 *          still reads real fuzzer bytes, not the harness's own input);
 *          this is also the ONLY way to reach the "no source_file: mode
 *          defaults to 0644" branch, since fstat() only runs when
 *          src_path is non-NULL. Clear, the file is passed as the
 *          source_file operand directly, reaching the fstat()-derived-
 *          mode branch instead.
 *   bit 1  whether "-m" is prepended -- always refused (Base64 is not
 *          implemented; see src/util/uuencode.c's own header comment),
 *          so this reaches that refusal path directly rather than
 *          leaving it permanently unexercised.
 *
 * decode_pathname (the operand naming what a downstream uudecode should
 * call the *recreated* file) is always a fixed literal, never derived
 * from fuzz bytes: unlike uudecode.c's header-filename field, this
 * string is never open()'d or otherwise resolved as a path by
 * uuencode.c itself (read in full: it only ever appears inside the
 * "begin mode %s\n" printf() as text) -- varying it would add no new
 * code path, only a cosmetic difference in the printed header.
 *
 * SIZE CAP. ENCODE_CAP bytes of the fuzz buffer become the source
 * content -- the same "bound the absolute cost" reasoning fuzz_patch.c's
 * PATCH_CAP and fuzz_uudecode.c's INPUT_CAP give; emit_line()'s own loop
 * is bounded by how many bytes fread() actually returned, which is at
 * most this cap, so there is no separate runaway-computation concern.
 *
 * REDIRECTED: both stdout and stderr. Unlike fuzz_uudecode.c, this file
 * DOES write to stdout -- emit_line()'s putchar() calls and the
 * begin/end printf()s are the entire point of the utility -- so both
 * streams are freopen()'d once per call, matching fuzz_sed.c's/
 * fuzz_ed.c's own reasoning for redirecting real process I/O a fuzzer
 * would otherwise flood on every one of millions of calls.
 *
 * WHAT IS CHECKED. src/internal/util.h's contract: a real exit status.
 * src/util/uuencode.c's every `return` (read in full) uses exactly 0 or
 * 1 -- like uudecode(1p), there is no >1 class here -- so 1 is the real
 * upper bound asserted. No exit()/_exit() call anywhere in the file
 * either; libFuzzer's own atexit-based detection is the backstop, as in
 * every other harness in this directory.
 *
 * NO ORACLE, for the same reason fuzz_uudecode.c's header gives: a
 * reference uuencode(1p) exists, but this file's own -m refusal is a
 * real, deliberate scope narrowing a differential run would report as a
 * false mismatch rather than a finding.
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

#define ROOT "/tmp/uuencodefz"
#define ENCODE_CAP 2048

static void write_file(const char *path, const char *data, size_t len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, data, len);
	close(fd);
}

/* ==== stdout/stderr redirection -- see this file's header comment. ======= */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned opts;
	size_t n;
	int status;
	char *argv[4];
	int argc = 0;
	char diagbuf[ENCODE_CAP + 1];

	if (size < 1) return 0;
	mkdir(ROOT, 0755);

	opts = data[0];
	data++; size--;

	n = size < ENCODE_CAP ? size : ENCODE_CAP;
	write_file(ROOT "/in", (const char *)data, n);
	memcpy(diagbuf, data, n);
	diagbuf[n] = 0;

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"uuencode";
	if (opts & 0x02) argv[argc++] = (char *)"-m";
	if (!(opts & 0x01)) argv[argc++] = (char *)ROOT "/in";
	argv[argc++] = (char *)"out.bin";
	argv[argc] = 0;

	if (opts & 0x01) {
		/* One-operand (stdin) form -- see this file's header comment on
		 * why real process stdin must carry the fuzz content here. */
		if (!freopen(ROOT "/in", "r", stdin)) { fflush(stdout); fflush(stderr); return 0; }
	}

	status = __util_uuencode_main(argc, argv);
	if (status < 0 || status > 1)
		oracle_mismatch_i("__util_uuencode_main returned outside {0,1}", diagbuf, status, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
