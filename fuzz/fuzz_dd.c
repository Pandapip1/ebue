/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_dd_main() -- src/util/dd.c's own `operand=value` grammar (read
 * that file's header comment in full for the exact OPERANDS list this
 * build implements: if=/of=, ibs=/obs=/bs=, count=, skip=/seek=,
 * conv=notrunc,sync,noerror) plus parse_dd_num() -- the shared
 * `N[bkw]['x' N[bkw]...]` block-size-expression grammar every one of
 * those numeric operands is parsed with -- and parse_conv()'s
 * comma-separated token scan.
 *
 * WHY THIS HARNESS IS NOT "FUZZ BYTES AS A FREE-FORM ARGV LIST" THE WAY
 * fuzz_sort.c/fuzz_csplit.c ARE. Those harnesses tokenize the fuzz
 * buffer on NUL bytes and hand each token straight to the utility under
 * test, because sort's -k spec and csplit's line/regexp args cannot, by
 * construction, make the program allocate more memory or write more
 * disk than the size of a fixed input fixture already bounds. dd(1p) is
 * genuinely different: bs=/ibs=/obs= feed straight into
 * `malloc((size_t)o->ibs)` / `malloc((size_t)o->obs)` in
 * dd_copy_direct()/dd_copy_blocked() (read in full, per this task's own
 * instruction), and skip=/seek= feed a byte OFFSET (n * ibs or n * obs)
 * to dd_position(), whose lseek() can silently create an
 * enormous-apparent-size sparse file the moment a write lands past it.
 * A free-form fuzzer string handed straight to parse_dd_num() can encode
 * a value near UINTMAX_MAX (via plain decimal digits alone, no suffix or
 * 'x' needed) -- so this harness never lets a fuzzer byte become part of
 * bs=/ibs=/obs=/skip=/seek='s literal text. Instead it builds each of
 * those five operands itself from a small capped integer plus an
 * optional b/k/w suffix chosen by the fuzzer, with the cap on the raw
 * integer picked so that VALUE * WORST-CASE-SUFFIX-MULTIPLIER can never
 * exceed a few KiB: BS_VAL_MAX=7 with k (*1024) tops out at 7168 bytes
 * (safely under MAX_BLOCK_BYTES=8192), and SKIP_SEEK_MAX=8 is used with
 * NO suffix at all (skip=/seek= are block COUNTS; the byte offset
 * dd_position() actually seeks is that count times ibs/obs, so bounding
 * the count alone, against an ibs/obs already bounded the same way,
 * bounds the worst-case seek offset to 8*8192=65536 bytes -- small
 * enough that even a persistent conv=notrunc run across the fuzzer's
 * whole session cannot grow $OUTFILE past that). count=, in sharp
 * contrast, needs NO such cap at all: it is only ever compared against
 * `blocks`, a counter incremented once per block actually read from
 * FIXTURE (a small, fixed-size file -- see THE FIXTURE below), so no
 * matter how astronomically large a count= value parses to, the copy
 * loop still stops the moment read() returns 0 at FIXTURE's real EOF.
 * count= is therefore the one operand this harness DOES hand
 * parse_dd_num() a genuinely fuzzer-controlled string for (capped only
 * in LENGTH, COUNT_CAP bytes, so a pathological all-digit run cannot
 * make strtoumax() itself the bottleneck) -- reaching the full
 * digit/b/k/w/recursive-'x' grammar, and its many rejection shapes
 * (empty string, trailing garbage, a bare suffix with no leading
 * digits), the other four operands' own caps deliberately avoid
 * reaching with anything but a handful of small values.
 *
 * OPERAND-VALUE BYTES.  data[0] OPTS (see below); data[1] a size value
 * (0..BS_VAL_MAX) for bs=/ibs=; data[2] a suffix selector (mod 4: none/
 * b/k/w), reused for whichever size operand(s) OPTS chooses; data[3] a
 * second size value, for obs= when OPTS's size-mode calls for ibs= and
 * obs= to differ; data[4]/data[5] the skip=/seek= counts (each
 * 0..SKIP_SEEK_MAX, no suffix -- see above for why). Everything from
 * data[6] onward, up to COUNT_CAP bytes and stopping at the first
 * embedded NUL (the same "not one operand" rule fuzz_od.c's and
 * fuzz_cut.c's own header comments give for their own single-string
 * operands), becomes count='s value VERBATIM; if that leaves nothing
 * (an empty remainder, or one starting with a NUL), count= is simply
 * omitted from argv rather than the call being abandoned, so every
 * other operand combination OPTS selects still runs.
 *
 * OPTS BYTE (data[0]).  Bits 0-1 (mod-4 size mode): 0 no size operand at
 * all (both ibs/obs stay at dd(1p)'s own 512-byte default); 1 "bs=" (the
 * single value from data[1], forcing bs_mode -- dd_copy_direct(), not
 * dd_copy_blocked()); 2 "ibs=" AND "obs=" both given, independently,
 * from data[1] and data[3] (exercises the recombining blocked-copy path
 * with ibs != obs, and the 0-byte-size rejection whenever either value
 * lands on 0 with no suffix); 3 "ibs=" alone (obs stays default 512,
 * also exercising ibs != obs). Bits 2-4 select conv=: notrunc/sync/
 * noerror independently, joined into one comma-separated conv= operand
 * (e.g. "conv=sync,noerror") -- omitted entirely when none of the three
 * is set, the same "don't emit an operand this call has nothing to say
 * about" convention fuzz_cut.c's own -d handling uses. Bit 5, when set,
 * REPLACES whatever bits 2-4 chose with a single deliberately-invalid
 * "conv=bogus", reaching parse_conv()'s own "not supported by this
 * build" refusal (return 2) -- a real, documented error shape (this
 * file's header comment's own conv= paragraph) no valid combination of
 * notrunc/sync/noerror could ever produce. Bits 6 and 7 independently
 * add skip= and seek=.
 *
 * THE FIXTURE.  A small, fixed 4096-byte binary file (not derived from
 * the fuzz input, for the identical reason fuzz_od.c's and fuzz_sort.c's
 * own fixed-fixture headers give: the grammar under test is the operand
 * parser's, not the bytes dd(1p) copies), built as a repeating 0x00-0xFF
 * ramp so every sync-padding NUL-fill and every partial-final-block path
 * is copying real, varied bytes rather than one repeated value.
 *
 * $OUTFILE, AND WHY IT NEVER GROWS UNBOUNDED. dd(1p) is the one utility
 * among this batch of three that genuinely writes a real output file
 * (unlike diff(1p)/cmp(1p), which only ever read their operands) -- of=
 * always names one fixed path, OUTFILE, opened O_TRUNC (truncated to
 * empty first) UNLESS conv=notrunc was chosen this call. Even across a
 * long run that keeps landing on notrunc, OUTFILE's size can never
 * exceed the largest reachable (seek-offset + one block) this file's own
 * caps allow -- SKIP_SEEK_MAX(8) * MAX_BLOCK_BYTES(8192) + one more
 * block, on the order of 64-72 KiB -- a ceiling this harness's own
 * caps enforce regardless of how many fuzzer executions run against it.
 *
 * NO SPAWN RISK. dd(1p) never invokes another program under any operand
 * this file implements (checked while reading src/util/dd.c in full, per
 * this task's own instruction) -- so, unlike fuzz_find.c's -exec/-ok, no
 * argv-content safety exclusion is needed here.
 *
 * SIGINT. dd installs and restores its own SIGINT handler around the
 * copy loop (see src/util/dd.c's header comment on dd_interrupted); this
 * harness never raises SIGINT against itself, so that path (status 130)
 * is not reached here, though the assertion below still allows for it --
 * see WHAT IS CHECKED.
 *
 * STDOUT/STDERR REDIRECTION: the same freopen()-a-fixed-sink-file-once-
 * per-call mechanism fuzz_sort.c's, fuzz_od.c's and fuzz_diff.c's own
 * header comments give, for the identical reason -- __util_dd_main()
 * unconditionally writes an "N+P records in / N+P records out" summary
 * to stderr on every call, success or failure alike.
 *
 * WHAT IS CHECKED. src/util/dd.c has no dedicated EXIT STATUS section
 * with a single citation the way cmp(1p)/diff(1p) do, but reading
 * __util_dd_main() in full (as this task requires) shows it returns
 * exactly one of: 2 (any operand-parsing/usage error), 1 (an open/
 * position/copy error reported via had_error), 130 (128+SIGINT, this
 * project's own signal-exit-status convention per the file's own
 * comment, unreachable from this harness but still a real, documented
 * value), or 0 (success) -- so the assertion below checks that full,
 * real range rather than a narrower guess this harness's own argv
 * happens to reach.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define BS_VAL_MAX 7          /* * 1024 (k) <= 7168 <= MAX_BLOCK_BYTES */
#define SKIP_SEEK_MAX 8        /* block COUNT, no suffix -- see header */
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

/* Formats `val` (already caller-bounded) as a decimal literal, optionally
 * followed by one b/k/w suffix -- see this file's header comment for why
 * the CALLER's bound on `val`, not anything done here, is what keeps the
 * parsed byte count small. */
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
		/* argv strings must be writable-looking literals only in the
		 * sense that dd never mutates them; a static "bs=" prefix
		 * plus our own buffer is built into one literal below via a
		 * second buffer since operand=value must be one token. */
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
