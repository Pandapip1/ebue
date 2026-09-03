/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_uuencode_main() -- src/util/uuencode.c, uuencode(1p)'s encoder:
 * emit_line()'s 45-bytes-in/4-characters-per-3-bytes-out chunking, the
 * "begin mode decode_pathname" header (mode read via fstat() when a real
 * source_file is given, a fixed 0644 default over stdin), and the
 * zero-length-line-then-"end" terminator.
 *
 * Lower priority than fuzz_uudecode.c, on purpose: emit_line() has
 * exactly one shape for every input, no format variant, no untrusted
 * structure to misparse, so encoding arbitrary bytes is inherently
 * lower-risk than uudecode.c's job of *parsing* a stream shaped like a
 * real format. This harness exists mainly for completeness.
 *
 * uuencode(1p) reads source_file (or stdin) as raw bytes to encode --
 * there's no format for the fuzz buffer to resemble, so every fuzzer
 * byte after the first becomes the literal content of a file this
 * harness creates, NUL-safe because it's written with write(2) (never
 * strlen()) and read back with fread().
 *
 * Byte 0 selects two independent bits:
 *
 *   bit 0  one-operand (stdin) vs. two-operand (source_file) form. Set,
 *          real process stdin is freopen()'d from the same fuzz-content
 *          file; this is also the only way to reach the "no
 *          source_file: mode defaults to 0644" branch, since fstat()
 *          only runs when src_path is non-NULL. Clear, the file is
 *          passed as the source_file operand, reaching the
 *          fstat()-derived-mode branch instead.
 *   bit 1  whether "-m" is prepended -- always refused (Base64 is not
 *          implemented), reaching that refusal path directly.
 *
 * decode_pathname is always a fixed literal, never derived from fuzz
 * bytes: unlike uudecode.c's header-filename field, this string is never
 * open()'d or resolved as a path by uuencode.c (read in full: it only
 * ever appears inside the "begin mode %s\n" printf() as text).
 *
 * ENCODE_CAP bytes of the fuzz buffer become the source content;
 * emit_line()'s loop is bounded by how many bytes fread() actually
 * returned, which is at most this cap, so there's no separate
 * runaway-computation concern beyond the absolute-cost bound.
 *
 * Both stdout and stderr are redirected. Unlike fuzz_uudecode.c, this
 * file DOES write to stdout -- emit_line()'s putchar() calls and the
 * begin/end printf()s are the entire point of the utility.
 *
 * Checked: src/util/uuencode.c's every `return` (read in full) uses
 * exactly 0 or 1 -- like uudecode(1p), there is no >1 class here -- so 1
 * is the real upper bound asserted. No exit()/_exit() call anywhere in
 * the file either; libFuzzer's own atexit-based detection is the
 * backstop.
 *
 * No oracle, for the same reason fuzz_uudecode.c gives: a reference
 * uuencode(1p) exists, but this file's own -m refusal is a real,
 * deliberate scope narrowing a differential run would report as a false
 * mismatch rather than a finding.
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
