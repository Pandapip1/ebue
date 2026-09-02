/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_ar_main() -- src/util/ar.c, a from-scratch reader/writer for the
 * classic common `ar_hdr` archive format (see that file's own header for
 * why this build picked a real, from-scratch codec over wrapping tcc's
 * own `-ar` mode, and for the exact 60-byte header layout: name[16]
 * mtime[12] uid[6] gid[6] mode[8] size[10] fmag[2]). This harness fuzzes
 * the READ side: ar_foreach()'s member-walking loop (the "!<arch>\n"
 * global magic check, then a repeating 60-byte-header-then-data-then-
 * pad-to-even walk) and parse_header()'s own field parsing (the
 * trailing "`\n" fmag validity check, the name-field's trailing-'/'-and-
 * blank-trim logic, and three strtol() fields read straight from
 * attacker-controlled fixed-width text) -- a header/member parser over
 * untrusted archive bytes is exactly the surface fuzz_pax.c's own header
 * comment targets for the sibling tar/cpio formats, and this file's own
 * header explains why the two are NOT the same code: ar's format is
 * entirely its own from-scratch layout, sharing no parsing code with
 * pax's ustar/cpio codecs at all.
 *
 * WHY -t (TABLE OF CONTENTS), AND WHY THAT MEANS ZERO FILESYSTEM SIDE
 * EFFECTS BEYOND THIS HARNESS'S OWN FIXED SINK FILES. src/util/ar.c's
 * own header lists six operations (-d/-p/-q/-r/-t/-x); -d/-q/-r all
 * rewrite the archive in place via a real `<archive>.artmp` temp file
 * and rename() (do_delete()/do_append_or_replace()), and -x
 * (x_visit()) calls fopen(m->name, "wb") for EVERY member matched --
 * i.e., for the fuzzed, attacker-controlled member name straight out of
 * the archive, with none of pax's own name_is_safe() '..'/absolute-path
 * guard (ar(1p)'s own AR_NAME_MAX=15-byte inline names have no '/' at
 * all once GNU-style long names are excluded, per this file's own
 * header, but a name field this build's own writer would refuse can
 * still be a bare `../../x`-shaped string inside those 15 bytes if a
 * *foreign*/adversarial archive puts one there, and this harness's own
 * task is exactly to hand it adversarial archives) -- exactly the
 * "litters/escapes the working directory across a long-running fuzzing
 * process" risk fuzz_pax.c's own header gives for choosing list mode
 * over -r/-w there. -t (ar_foreach() -> t_visit()) only ever calls
 * printf() (redirected below) and, under -v, gmtime()/strftime() over
 * the member's raw `mtime` field -- no open()/mkdir()/rename() anywhere
 * on this path. The only real file this harness itself ever writes is
 * the fixed archive path it hands ar via the `archive` operand,
 * read-only from ar's side (ar_foreach() only ever fopen()s it "rb").
 *
 * THE FUZZ INPUT IS THE ARCHIVE BYTES, verbatim, for the identical
 * reason fuzz_pax.c's header gives for its own archive operand: `ar`
 * takes its archive only as a real pathname operand (there is no
 * stdin-borne form at all, unlike patch's `-i`/stdin choice), so the
 * fuzzer buffer is written to a fixed path under /tmp (present from
 * process start in fuzz/ntstubs.c's simulated volume) and read back with
 * plain fread(), binary-safe end to end -- parse_header() copies every
 * field through a fixed-size local buffer with an explicit length
 * (memcpy(field, raw+N, width); field[width]=0) before ever calling
 * strtol() on it, so an embedded NUL or non-numeric byte inside a field
 * is handled the same way any other malformed field is (strtol() stops
 * early / returns 0), never a memory-safety hazard.
 *
 * OPTIONS FUZZED: byte 0 bit 0 selects -v, the only modifier -t accepts
 * (the ls -l-shaped listing line vs. the bare-name one -- t_visit()'s
 * own `if (ctx->verbose)` branch). No `file...` operands are ever
 * given: name_wanted() with `nfiles == 0` returns 1 unconditionally
 * (read directly in that function), so every member in the archive is
 * visited regardless, which maximizes how much of t_visit()'s own body
 * a single input reaches -- the identical reasoning fuzz_pax.c's header
 * gives for never supplying a pattern operand.
 *
 * BOUNDING RUNAWAY COMPUTATION: NOT NEEDED HERE, checked directly rather
 * than assumed, in the same spirit as fuzz_ed.c's, fuzz_patch.c's and
 * fuzz_pax.c's own "checked, not assumed" sections. ar_foreach()'s only
 * loop is `for (;;) { fread(header) ... visit() ... fseek(next) }`, and
 * every iteration either consumes real, already-capped input or ends
 * the loop outright:
 *
 *   - a short fread() of the 60-byte header (inevitable once the
 *     AR_CAP-capped archive file runs out) returns 0 and breaks
 *     ("clean EOF between members", the loop's own comment) or, for a
 *     partial 1-59-byte remainder, is treated as a truncation error and
 *     also breaks;
 *   - parse_header()'s own strtol() calls on the size field can produce
 *     a negative or enormous `m.size`, and `skip = m.size + (m.size &
 *     1)` inherits whatever sign or magnitude that has -- but the very
 *     next line, `fseek(ar, data_off + skip, SEEK_SET)`, either fails
 *     outright on a negative resulting offset (breaking the loop
 *     immediately) or succeeds and seeks past the small, AR_CAP-capped
 *     file's real end, so the FOLLOWING iteration's header fread() is
 *     the short-read case above -- one extra iteration at most, never a
 *     stall, because there is no amount of seeking that manufactures
 *     bytes the capped file does not actually contain.
 *
 * -p and -x's own per-member data loops (not reached by -t at all, per
 * "WHY -t" above) have the identical short-fread-ends-it property for
 * the same reason -- not exercised here, but confirmed by reading them,
 * not merely assumed safe by omission.
 *
 * So, as in fuzz_ed.c/fuzz_pax.c, no watchdog or pre-scan filter is
 * added. AR_CAP (below) still bounds the absolute number of well-formed
 * 60-byte headers one input can pack in, for the same "keep the worst
 * case cheap in absolute terms" reason every capped harness in this
 * tree gives.
 *
 * STDOUT/STDERR REDIRECTION: see fuzz_sed.c's own header comment for the
 * freopen()-not-fopen() reasoning; the same applies here verbatim. -t's
 * listing lines go to the real stdout (printf(), t_visit()), and
 * ar_foreach()'s own "not an archive"/"truncated"/"corrupt member
 * header" diagnostics go through __util_diagf() to stderr -- both
 * redirected once per call to a fixed sink file, truncated by
 * freopen()'s own "w" mode each time, for the same throughput reason
 * fuzz_sed.c gives.
 *
 * NO ORACLE. Same shape as fuzz_sed.c's, fuzz_ed.c's, fuzz_patch.c's and
 * fuzz_pax.c's own reasoning: no reference ar(1) implementation this
 * project could differentially compare against without every one of
 * this file's own documented, real scope narrowings (no GNU "//" long-
 * name table or BSD "#1/<len>" extended names on read, no symbol table)
 * reading as a false mismatch. What IS checked is the same contract the
 * rest of this tree's __util_<name>_main() harnesses check:
 *
 *   - src/internal/util.h's banner: a real process exit status, never a
 *     raw errno or a boolean. Every `return` in src/util/ar.c (read in
 *     full while writing this harness) is 0, 1 or 2 -- this harness's
 *     own argv is always well-formed (one recognized operation letter,
 *     an archive operand, no `file...` operands), so the several
 *     argument-usage paths that return 2 are not expected to fire, but
 *     the assertion below checks the real, full contract (0, 1, or 2),
 *     not the narrower range this harness happens to exercise, matching
 *     fuzz_ed.c's own stated reasoning for doing the same;
 *   - no exit()/_exit() call anywhere in src/util/ar.c (checked while
 *     writing this harness) -- __util_ar_main() has no bi_ar() shell-
 *     builtin caller today, but src/internal/util.h's contract applies
 *     unconditionally to every __util_<name>_main(), and libFuzzer's own
 *     atexit-based "an exit() was detected" defence is what would
 *     surface a violation, the same backstop every other harness in
 *     this tree relies on rather than a bespoke check in this file.
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

#define ROOT "/tmp/arfz"
#define AR_CAP 2048

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
	int fd;
	char op[4];
	/* NUL-terminated copy of a prefix of the archive bytes, purely for
	 * oracle_mismatch_i()'s own `in` argument -- see fuzz_patch.c's and
	 * fuzz_pax.c's identical comment on why the raw fuzzer buffer (not
	 * guaranteed to contain a NUL at all) is unsafe to hand to
	 * host_oracle.c's addq(), which scans with `for (; *s; s++)`. */
	char diagbuf[AR_CAP + 1];

	if (size < 1) return 0;
	mkdir(ROOT, 0755);

	opts = data[0];
	data++; size--;

	n = size < AR_CAP ? size : AR_CAP;
	memcpy(diagbuf, data, n);
	diagbuf[n] = 0;

	fd = open(ROOT "/archive", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return 0;
	if (n && write(fd, data, n) != (ssize_t)n) { close(fd); return 0; }
	close(fd);

	if (!redirect_streams()) return 0;

	op[0] = '-'; op[1] = 't';
	if (opts & 0x01) { op[2] = 'v'; op[3] = 0; }
	else { op[2] = 0; op[3] = 0; }

	argv[argc++] = (char *)"ar";
	argv[argc++] = op;
	argv[argc++] = (char *)ROOT "/archive";
	argv[argc] = 0;

	status = __util_ar_main(argc, argv);
	if (status < 0 || status > 2)
		oracle_mismatch_i("__util_ar_main returned outside {0,1,2}", diagbuf, status, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
