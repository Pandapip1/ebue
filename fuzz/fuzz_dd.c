/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_dd_main()'s (src/util/dd.c) `operand=value` grammar and
 * parse_dd_num()'s shared `N[bkw]['x' N[bkw]...]` block-size expression
 * parser.
 *
 * Unlike fuzz_sort.c/fuzz_csplit.c, this harness does NOT hand fuzzer
 * bytes straight to argv as free-form operand text: bs=/ibs=/obs= feed
 * a malloc() size directly, and skip=/seek= feed a byte offset to an
 * lseek() that can silently create an enormous sparse file -- an
 * unbounded decimal string handed to parse_dd_num() could push either
 * near UINTMAX_MAX. So this harness builds those five operands itself
 * from a small capped integer plus a fuzzer-chosen b/k/w suffix (bounds
 * chosen so value*worst-case-multiplier stays under MAX_BLOCK_BYTES for
 * sizes, and count*ibs/obs stays a bounded ~64KiB for skip/seek). count=
 * is the one operand genuinely fuzzer-controlled text (capped only in
 * length): however large it parses, the copy loop still stops the
 * moment FIXTURE's real EOF is read, so no cap on its parsed VALUE is
 * needed the way the other four require.
 *
 * Byte layout: data[0] is the OPTS byte (bits 0-1: size mode -- none/
 * bs=/ibs=+obs=/ibs=-only; bits 2-4: conv=notrunc,sync,noerror; bit 5:
 * replaces conv= with the deliberately-invalid "conv=bogus", reaching
 * parse_conv()'s refusal path; bits 6-7: add skip=/seek=). data[1]/[3]
 * are the two size values, data[2] their shared suffix selector,
 * data[4]/[5] the skip=/seek= counts. Everything from data[6] onward
 * (capped, NUL-terminated at the first embedded NUL) becomes count='s
 * value verbatim, or is omitted if empty.
 *
 * FIXTURE is a small fixed 4096-byte 0x00-0xFF ramp (not fuzzer-derived
 * -- the grammar under test is the operand parser's, not the copied
 * bytes), so every sync-padding fill and partial-final-block path
 * copies varied real bytes. OUTFILE is opened O_TRUNC unless
 * conv=notrunc is chosen; even under a persistent notrunc run, this
 * harness's own operand caps bound its size to roughly 64-72 KiB.
 *
 * dd(1p) never spawns another process under any operand this build
 * implements, so unlike fuzz_find.c's -exec/-ok, no argv-content safety
 * exclusion is needed. dd's own SIGINT handling (status 130) is never
 * reached here since this harness never raises SIGINT, but the
 * assertion below still allows for it. stdout/stderr are freopen()'d
 * once per call since __util_dd_main() unconditionally writes an "N+P
 * records in/out" summary to stderr on every call.
 *
 * Checked: __util_dd_main() returns 2 (usage error), 1 (an open/
 * position/copy error), 130 (128+SIGINT, unreachable here but a real
 * documented value), or 0 -- read directly off every return in the
 * function, since dd.c has no single EXIT STATUS citation the way
 * cmp(1p)/diff(1p) do.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define BS_VAL_MAX 7
#define SKIP_SEEK_MAX 8
#define MAX_BLOCK_BYTES 8192
#define COUNT_CAP 32

#define ROOT "/tmp/ddfz"
#define FIXTURE ROOT "/src"
#define OUTFILE ROOT "/dst"

static void ensure_fixture(void)
{
	static int done;
	unsigned char buf[4096];
	size_t i;
	int fd;

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);

	for (i = 0; i < sizeof buf; i++) buf[i] = (unsigned char)i;

	fd = open(FIXTURE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, buf, sizeof buf);
	close(fd);
}

static int redirect_streams(void)
{
	if (!freopen(ROOT "/sink_out", "w", stdout)) return 0;
	if (!freopen(ROOT "/sink_err", "w", stderr)) return 0;
	return 1;
}

static void fmt_size_operand(char *buf, size_t bufsz, unsigned val, unsigned suffix_sel)
{
	char suf = 0;
	switch (suffix_sel & 0x03) {
	case 1: suf = 'b'; break;
	case 2: suf = 'k'; break;
	case 3: suf = 'w'; break;
	default: break;
	}
	if (suf) snprintf(buf, bufsz, "%u%c", val, suf);
	else snprintf(buf, bufsz, "%u", val);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opts, sizeval1, suffix_sel, sizeval2, skipval, seekval;
	unsigned sizemode;
	char bsstr[8], obsstr[8], skipstr[8], seekstr[8];
	char convstr[32];
	char countstr[COUNT_CAP + 1];
	int have_count = 0;
	char *argv[12];
	int argc = 0;
	int rc;
	char diagbuf[COUNT_CAP + 1];

	if (size < 6) return 0;
	ensure_fixture();

	opts = data[0];
	sizeval1 = data[1] % (BS_VAL_MAX + 1);
	suffix_sel = data[2];
	sizeval2 = data[3] % (BS_VAL_MAX + 1);
	skipval = data[4] % (SKIP_SEEK_MAX + 1);
	seekval = data[5] % (SKIP_SEEK_MAX + 1);
	data += 6; size -= 6;

	{
		size_t n = size < COUNT_CAP ? size : COUNT_CAP;
		memcpy(diagbuf, data, n);
		diagbuf[n] = 0;
		if (n > 0 && !memchr(data, 0, n)) {
			memcpy(countstr, data, n);
			countstr[n] = 0;
			have_count = 1;
		}
	}

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"dd";
	argv[argc++] = (char *)"if=" FIXTURE;
	argv[argc++] = (char *)"of=" OUTFILE;

	sizemode = opts & 0x03;
	if (sizemode == 1) {
		fmt_size_operand(bsstr, sizeof bsstr, sizeval1, suffix_sel);
		{
			static char full[16];
			snprintf(full, sizeof full, "bs=%s", bsstr);
			argv[argc++] = full;
		}
	} else if (sizemode == 2 || sizemode == 3) {
		fmt_size_operand(bsstr, sizeof bsstr, sizeval1, suffix_sel);
		{
			static char full[16];
			snprintf(full, sizeof full, "ibs=%s", bsstr);
			argv[argc++] = full;
		}
		if (sizemode == 2) {
			fmt_size_operand(obsstr, sizeof obsstr, sizeval2, suffix_sel);
			{
				static char full2[16];
				snprintf(full2, sizeof full2, "obs=%s", obsstr);
				argv[argc++] = full2;
			}
		}
	}

	if (opts & 0x40) {
		snprintf(skipstr, sizeof skipstr, "%u", skipval);
		{
			static char full[16];
			snprintf(full, sizeof full, "skip=%s", skipstr);
			argv[argc++] = full;
		}
	}
	if (opts & 0x80) {
		snprintf(seekstr, sizeof seekstr, "%u", seekval);
		{
			static char full[16];
			snprintf(full, sizeof full, "seek=%s", seekstr);
			argv[argc++] = full;
		}
	}

	if (opts & 0x20) {
		argv[argc++] = (char *)"conv=bogus";
	} else if (opts & 0x1C) {
		int n = 0, notrunc = opts & 0x04, sync_ = opts & 0x08, noerr = opts & 0x10;
		convstr[0] = 0;
		if (notrunc) n += snprintf(convstr + n, sizeof convstr - n, "%snotrunc", n ? "," : "");
		if (sync_) n += snprintf(convstr + n, sizeof convstr - n, "%ssync", n ? "," : "");
		if (noerr) n += snprintf(convstr + n, sizeof convstr - n, "%snoerror", n ? "," : "");
		{
			static char full[40];
			snprintf(full, sizeof full, "conv=%s", convstr);
			argv[argc++] = full;
		}
	}

	if (have_count) {
		static char full[COUNT_CAP + 8];
		snprintf(full, sizeof full, "count=%s", countstr);
		argv[argc++] = full;
	}

	argv[argc] = NULL;

	rc = __util_dd_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || (rc > 2 && rc != 130))
		oracle_mismatch_i("__util_dd_main returned outside {0,1,2,130}", diagbuf, rc, 0);

	return 0;
}
