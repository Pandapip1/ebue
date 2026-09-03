/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_patch_main()'s (src/util/patch.c) hand-written
 * diff(1)-format parser (normal/context/unified/ed-script hunk grammars)
 * and its fuzzy hunk-matching/application engine.
 *
 * The fuzz buffer is the patch text, written verbatim (embedded NULs
 * included) to a file and read via `-i` + getline(), which is
 * binary-safe -- unlike fuzz_sed.c/fuzz_ed.c/fuzz_awk.c's argv-borne
 * inputs, there's no NUL-terminated-string question to dodge here at
 * all. The target file patch(1p) needs is a small fixed fixture (its
 * content isn't fuzzed -- the grammar under test is the patch file's).
 * `-o <sink>` is always passed so a successful application never
 * touches the fixture, which is written once; this does cost coverage
 * of -b's write_linebuf() backup path, since -o makes it a no-op --
 * only its option-parsing arm is exercised. `-d` is never passed since
 * it chdir()s the whole process, breaking every other call's relative
 * /tmp paths.
 *
 * Byte 0 selects -b/-l/-N/-R (one bit each) and one of {auto-detect,
 * -c, -e, -n, -u} (three bits, values 5-7 folding back to auto, which
 * is deliberately the majority case since it's what a real `patch <
 * diff` does). -D and -p are not fuzzed: -D only changes output
 * formatting, and -p only affects the header-name guessing this harness
 * bypasses by always supplying the file operand explicitly.
 *
 * No runaway-computation bound is needed: patch(1p)'s grammar has no
 * branch/label construct (unlike sed's b/t), so every loop in
 * patch.c is bounded by patch_lines/target->n/nhunks/neds, quantities
 * PATCH_CAP and the fixed fixture already cap.
 *
 * stderr is freopen()'d once per call (every diagnostic path writes
 * there, and malformed input is the overwhelming common case while
 * fuzzing); patch.c never writes to stdout.
 *
 * No independent oracle exists for patch(1p) (this project's scope
 * narrowings would read as false mismatches against a real
 * implementation). What's checked instead: __util_patch_main() returns
 * 0, 1, or 2 (patch.c's own EXIT STATUS section documents 0/1/">1", and
 * a direct read of every return in the file confirms 2 is the real
 * upper bound), and never calls exit()/_exit() -- surfaced, if it ever
 * happens, by libFuzzer's own exit-detection rather than a bespoke
 * check here.
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
	/* NUL-terminated copy for oracle_mismatch_i()'s `in` arg only --
	 * host_oracle.c's addq() scans with strlen-style `for (; *s; s++)`,
	 * unsafe on the raw buffer, which has no guaranteed NUL. The patch
	 * file written below still gets every fuzzer byte verbatim. */
	char diagbuf[PATCH_CAP + 1];

	if (size < 1) return 0;
	mkdir(ROOT, 0755);

	opts = data[0];
	data++; size--;

	n = size < PATCH_CAP ? size : PATCH_CAP;
	write_file(ROOT "/patch", (const char *)data, n);
	memcpy(diagbuf, data, n);
	diagbuf[n] = 0;

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
