/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_m4_main()'s (src/util/m4.c) recursive macro-expansion
 * engine, all 32 mandatory builtins, and its eval() evaluator.
 *
 * m4 takes its program as a `file` operand (stdin/stdout are `FILE
 * *const` here, so no fmemopen()-onto-stdin trick), so each call writes
 * the fuzz buffer to a fixed path under /tmp and passes that. Byte 0
 * selects which of -s/-D/-U accompany the call, exercising
 * __util_m4_main()'s own option-parsing loop rather than being spent on
 * program text.
 *
 * Before this harness existed, src/util/m4.c had no recursion-depth or
 * expansion-count guard: `define(a,a)a` looped forever and
 * `len(len(len(...)))` recursed the C stack unbounded, reachable with no
 * prior define() at all. Both are now fixed in m4.c itself
 * (M4_MAX_EXPANSIONS, M4_MAX_DEPTH) rather than worked around here,
 * since libFuzzer's per-unit -timeout doesn't work in this project (see
 * tools/fuzz.sh) and an unbounded expansion would otherwise burn a whole
 * campaign's time budget on one trial.
 *
 * syscmd() calls this library's system(), which on this build always
 * fails safely: it resolves a shell via %ComSpec% or cmd.exe with no
 * /bin/sh fallback, neither of which exists here, and fuzz/ntstubs.c
 * resets `environ` to empty, so %ComSpec% is never set. Confirmed by
 * reading both call sites, not assumed -- a future build where a real
 * shell IS reachable would need to revisit this.
 *
 * A fixed file is seeded at /tmp/m4inc so an `include(/tmp/m4inc)`
 * spliced in by the fuzzer exercises a real, successful include() path
 * instead of only ever landing on "file not found"; its content is
 * plain ASCII with no embedded NUL, since included content flows through
 * an ordinary NUL-terminated C string. Any other guessed path just fails
 * harmlessly against ntstubs.c's simulated volume.
 *
 * Pass 1's stdout (captured via fd redirect) is, if non-empty, fed back
 * through __util_m4_main() as an independent second trial -- a
 * crash/hang/leak check only, not a fixed-point oracle: m4's output is
 * prose, not a serialization of its input, so there is no equality or
 * termination property to assert between the two passes. It's a cheap
 * way to reach scan() states (unbalanced quote/comment delimiters,
 * M4_BUILTIN_MAGIC sentinels from defn()) that raw mutated bytes alone
 * would take far longer to discover.
 *
 * PROGRAM_CAP (768 bytes) is what actually bounds C-stack recursion
 * through dispatch_macro()->collect_args()->scan() in practice (every
 * builtin name is at least 3 bytes, so 768 bytes bounds nesting to a
 * couple hundred levels, well under a default thread stack) -- more so
 * than M4_MAX_DEPTH (5000), which exists for inputs larger than any
 * -max_len this project runs with. SECONDPASS_CAP (2048 bytes) bounds
 * how much of pass 1's output gets re-fed, since a small pass-1 program
 * can legitimately produce far more output than PROGRAM_CAP.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "../src/internal/util.h"

#define PROGRAM_CAP 768
#define SECONDPASS_CAP 2048

#define IN1_PATH  "/tmp/fuzz_m4_1.in"
#define OUT1_PATH "/tmp/fuzz_m4_1.out"
#define IN2_PATH  "/tmp/fuzz_m4_2.in"
#define OUT2_PATH "/tmp/fuzz_m4_2.out"
#define INCLUDE_PATH "/tmp/m4inc"

/* Saved so redirects can be undone -- libFuzzer's own reporting needs
 * an ordinary stdout between trials. */
static int real_stdout_fd = -1;

static void fixture(void)
{
	static int done;
	int fd;

	if (done) return;
	done = 1;

	real_stdout_fd = dup(STDOUT_FILENO);

	fd = open(INCLUDE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		static const char content[] = "included m4 text\n";
		(void)write(fd, content, sizeof content - 1);
		close(fd);
	}
}

/* Point fd 1 at `path` (truncated, created if needed), or leave it
 * alone if that fails -- a harness that crashed because its OWN file
 * I/O failed would be reporting a defect in itself, not in m4. */
static void redirect_stdout_to(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	dup2(fd, STDOUT_FILENO);
	close(fd);
}

static void restore_stdout(void)
{
	if (real_stdout_fd >= 0) dup2(real_stdout_fd, STDOUT_FILENO);
}

static int write_file(const char *path, const void *data, size_t n)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return 0;
	if (n && write(fd, data, n) != (ssize_t)n) { close(fd); return 0; }
	close(fd);
	return 1;
}

static size_t read_file_capped(const char *path, char *buf, size_t cap)
{
	int fd = open(path, O_RDONLY);
	ssize_t r;
	if (fd < 0) return 0;
	r = read(fd, buf, cap);
	close(fd);
	return r > 0 ? (size_t)r : 0;
}

/* extra_argv: NULL-terminated -s/-D/-U options ahead of the file operand, or NULL. */
static void run_m4(const char *path, char *const *extra_argv, const char *outpath)
{
	char *argv[8];
	int argc = 0;

	argv[argc++] = (char *)"m4";
	if (extra_argv)
		while (*extra_argv && argc < 6) argv[argc++] = *extra_argv++;
	argv[argc++] = (char *)path;
	argv[argc] = NULL;

	redirect_stdout_to(outpath);
	(void)__util_m4_main(argc, argv);
	restore_stdout();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char program[PROGRAM_CAP];
	char second[SECONDPASS_CAP];
	size_t n, got2;
	unsigned mode;
	char *opts[4];	/* up to 3 flags (see below) plus a NULL terminator */
	int nopts = 0;

	fixture();

	if (size < 1) return 0;
	mode = data[0];
	data++; size--;

	n = size < sizeof program ? size : sizeof program;
	memcpy(program, data, n);

	if (mode & 1u) opts[nopts++] = (char *)"-s";
	if (mode & 2u) opts[nopts++] = (char *)"-Dfoo=bar";
	if (mode & 4u) opts[nopts++] = (char *)"-Udnl";
	opts[nopts] = NULL;

	if (!write_file(IN1_PATH, program, n)) return 0;

	run_m4(IN1_PATH, nopts ? opts : NULL, OUT1_PATH);

	got2 = read_file_capped(OUT1_PATH, second, sizeof second);
	if (got2 && write_file(IN2_PATH, second, got2))
		run_m4(IN2_PATH, NULL, OUT2_PATH);

	return 0;
}
