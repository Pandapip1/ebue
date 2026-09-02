/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_pax_main() -- src/util/pax.c, a from-scratch reader/writer for
 * two archive formats (ustar and octet-oriented/"odc" cpio; see that
 * file's own header on why pax's own extended-header format is the one
 * deliberately not implemented). This harness fuzzes the READ side only:
 * pax_reader_open()'s format-detection peek (6-byte cpio magic vs. a
 * full 512-byte block checked for ustar's "ustar" tag at offset 257),
 * parse_ustar_block()'s fixed-width octal field parsing
 * (parse_octal_field()) and name-prefix-joining, and read_cpio_header()'s
 * own octal-field/namesize/filesize parsing -- a tar/cpio-family archive
 * parser is exactly the "parse untrusted, attacker-shaped bytes" surface
 * this project's fuzz harnesses target first (see fuzz_sed.c's and
 * fuzz_patch.c's own headers on the same theme), and unlike patch(1p)'s
 * text-line grammar, this one is entirely fixed-width binary fields with
 * their own overflow/desync failure modes (a corrupt or adversarial
 * `size`/`namesize` field, a missing "ustar" tag, a cpio member whose
 * magic doesn't repeat).
 *
 * WHY LIST MODE (neither -r nor -w), AND WHY THAT MEANS ZERO FILESYSTEM
 * SIDE EFFECTS BEYOND THIS HARNESS'S OWN FIXED SINK FILES. -r (extract)
 * and -r -w (copy) both end in materialize() (src/util/pax.c), which
 * calls mkdir()/symlink()/link()/mkfifo()/mknod()/open()-with-O_CREAT
 * against a *member-supplied* destination path -- exactly the class of
 * "untrusted archive writes arbitrary files" risk name_is_safe() (same
 * file) blunts but does not eliminate. List mode (do_extract == 0 inside
 * do_list_or_read()) never calls materialize(): a matched member goes to
 * print_listing() (stdout only, redirected below) and
 * pax_reader_copy_data(&r, &m, -1), whose `out < 0` means "discard,
 * never write anywhere". The only real file this harness ever opens is
 * the fixed archive path it writes and then hands pax via `-f`,
 * read-only from pax's side. -w (write) is not exercised either, for the
 * unrelated reason that its whole input is real filesystem trees
 * (walk_operands()/nftw()), not fuzzer bytes.
 *
 * THE FUZZ INPUT IS THE ARCHIVE BYTES, verbatim. `-f` takes a real
 * pathname (stdin/stdout/stderr are `FILE *const`, so there is no
 * fmemopen()-onto-stdin trick available), so every fuzzer byte is
 * written to a fixed path under /tmp and pax reads it back with plain
 * fread(), which is binary-safe. Every `struct pax_member` field this
 * harness's oracle check touches is built from the raw archive bytes
 * through a fixed-size, NUL-terminated snprintf()/memcpy() copy first
 * (parse_ustar_block()/read_cpio_header()), so an embedded NUL or a
 * too-long name is truncated the same way any other malformed field is,
 * not a memory-safety hazard.
 *
 * OPTIONS FUZZED: byte 0 bit 0 selects -v (the ls -l-style listing:
 * gmtime()/strftime() over the member's raw, possibly-out-of-range
 * `mtime` field, plus the symlink "-> target" arm); bit 1 selects -c
 * (pattern-complement, harmless here since no pattern operand is ever
 * given, so pax_name_matches()'s own `npat == 0` short-circuit always
 * wins regardless -- fuzzing this bit still costs nothing and exercises
 * the flag's own argv-parsing arm). No pattern operand is supplied: an
 * empty pattern list means every member matches, maximizing how much of
 * do_list_or_read()'s per-member body a single input reaches.
 *
 * BOUNDING RUNAWAY COMPUTATION: not needed here. do_list_or_read()'s
 * only loop is `for (;;) { ... pax_reader_next() ... }`, and every path
 * through pax_reader_next() either consumes real, already-capped input
 * or returns 0/-1 and ends the loop: a short fread() of the ustar
 * 512-byte block (inevitable once the PAX_CAP-capped archive file runs
 * out) returns 0 cleanly; a missing/short cpio "070707" resync magic
 * returns -1 immediately; and read_cpio_header()'s namesize/filesize
 * fields are read from the same capped file, so a header claiming a
 * huge size simply hits a short read on its first fread() of that field
 * and returns -1 the same call. pax_reader_copy_data()'s own discard
 * loop (`while (remain) { fread(...); ... }`) has the identical
 * property. PAX_CAP (below) still bounds the absolute number of
 * well-formed headers one input can pack in.
 *
 * STDOUT/STDERR REDIRECTION: see fuzz_sed.c's header comment for the
 * freopen()-not-fopen() reasoning; the same applies here verbatim. List
 * mode's print_listing() writes every line to real stdout, and
 * pax_reader_open()/pax_reader_next()'s corrupt-archive diagnostics go
 * through __util_diagf() to stderr -- both redirected once per call to a
 * fixed sink file, truncated by freopen()'s "w" mode each time (millions
 * of calls with no fork; an un-redirected run would drown in its own
 * terminal output).
 *
 * NO ORACLE: no reference pax(1p)/tar/cpio implementation this project
 * could differentially compare against without this file's own
 * documented scope narrowings (no pax extended headers, no cpio
 * hard-link detection, the lenient single-all-zero-block ustar
 * end-of-archive check) reading as a false mismatch. What IS checked:
 *
 *   - src/internal/util.h's contract that __util_<name>_main() returns a
 *     real process exit status, never a raw errno or boolean. Every
 *     `return` in src/util/pax.c is 0 or 1 -- unlike patch.c/ar.c, this
 *     file has no ">1 real error" tier at all, so 0/1 is the whole
 *     contract, asserted as such below;
 *   - no exit()/_exit() call anywhere in src/util/pax.c -- libFuzzer's
 *     own atexit-based "an exit() was detected" defence is what would
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

#define ROOT "/tmp/paxfz"
#define PAX_CAP 4096

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
	char *argv[8];
	int argc = 0;
	int fd;
	/* NUL-terminated copy of a prefix of the archive bytes, purely for
	 * oracle_mismatch_i()'s own `in` argument -- see fuzz_patch.c's
	 * identical comment on why the raw fuzzer buffer (not guaranteed to
	 * contain a NUL at all) is unsafe to hand to host_oracle.c's addq(),
	 * which scans with `for (; *s; s++)`. */
	char diagbuf[PAX_CAP + 1];

	if (size < 1) return 0;
	mkdir(ROOT, 0755);

	opts = data[0];
	data++; size--;

	n = size < PAX_CAP ? size : PAX_CAP;
	memcpy(diagbuf, data, n);
	diagbuf[n] = 0;

	fd = open(ROOT "/archive", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return 0;
	if (n && write(fd, data, n) != (ssize_t)n) { close(fd); return 0; }
	close(fd);

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"pax";
	if (opts & 0x01) argv[argc++] = (char *)"-v";
	if (opts & 0x02) argv[argc++] = (char *)"-c";
	argv[argc++] = (char *)"-f";
	argv[argc++] = (char *)ROOT "/archive";
	argv[argc] = 0;

	status = __util_pax_main(argc, argv);
	if (status != 0 && status != 1)
		oracle_mismatch_i("__util_pax_main returned outside {0,1}", diagbuf, status, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
