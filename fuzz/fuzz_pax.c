/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_pax_main() -- src/util/pax.c, a from-scratch reader/writer for
 * two archive formats (ustar and octet-oriented/"odc" cpio). This harness
 * fuzzes the READ side only: pax_reader_open()'s format-detection peek,
 * parse_ustar_block()'s fixed-width octal field parsing and
 * name-prefix-joining, and read_cpio_header()'s octal-field/namesize/
 * filesize parsing -- unlike patch(1p)'s text-line grammar, this is
 * entirely fixed-width binary fields with their own overflow/desync
 * failure modes (a corrupt `size`/`namesize` field, a missing "ustar"
 * tag, a cpio member whose magic doesn't repeat).
 *
 * List mode only (neither -r nor -w): -r (extract) and -r -w (copy) both
 * end in materialize(), which calls mkdir()/symlink()/link()/mknod()/
 * open()-with-O_CREAT against a *member-supplied* destination path --
 * exactly the "untrusted archive writes arbitrary files" risk
 * name_is_safe() blunts but does not eliminate. List mode never calls
 * materialize(): a matched member goes to print_listing() (stdout,
 * redirected below) and pax_reader_copy_data(&r, &m, -1), whose
 * `out < 0` means discard. The only real file this harness ever opens is
 * the fixed archive path it writes and hands pax via `-f`, read-only
 * from pax's side. -w is not exercised for the unrelated reason that its
 * whole input is real filesystem trees (walk_operands()/nftw()), not
 * fuzzer bytes.
 *
 * The fuzz input is the archive bytes, verbatim: `-f` takes a real
 * pathname (stdin/stdout/stderr are `FILE *const`, no fmemopen() trick
 * available), so every fuzzer byte is written to a fixed path under /tmp
 * and pax reads it back with fread(). Every `struct pax_member` field
 * this harness's oracle check touches is built from the raw archive
 * bytes through a fixed-size, NUL-terminated snprintf()/memcpy() copy
 * first, so an embedded NUL or too-long name truncates the same way any
 * other malformed field does, not a memory-safety hazard.
 *
 * Options fuzzed: byte 0 bit 0 selects -v (gmtime()/strftime() over the
 * member's raw, possibly-out-of-range `mtime` field, plus the symlink
 * "-> target" arm); bit 1 selects -c (pattern-complement, harmless here
 * since no pattern operand is given, so pax_name_matches()'s `npat == 0`
 * short-circuit always wins -- still exercises the flag's argv-parsing
 * arm). No pattern operand: an empty pattern list means every member
 * matches, maximizing how much of do_list_or_read()'s per-member body a
 * single input reaches.
 *
 * No runaway-computation guard needed: do_list_or_read()'s only loop is
 * `for (;;) { ... pax_reader_next() ... }`, and every path through
 * pax_reader_next() either consumes real, already-capped input or
 * returns 0/-1 and ends the loop (a short fread() once the PAX_CAP-capped
 * archive runs out, a missing cpio resync magic, or a huge namesize/
 * filesize field hitting a short read on its own fread()).
 * pax_reader_copy_data()'s discard loop has the identical property.
 * PAX_CAP still bounds the absolute number of well-formed headers one
 * input can pack in.
 *
 * Stdout/stderr redirection: see fuzz_sed.c's header on freopen()-not-
 * fopen(); applies verbatim. print_listing() writes to real stdout, and
 * pax_reader_open()/pax_reader_next()'s corrupt-archive diagnostics go to
 * stderr via __util_diagf() -- both redirected to a fixed sink file,
 * truncated each call (millions of calls with no fork).
 *
 * No oracle: no reference pax/tar/cpio implementation could be
 * differentially compared without this file's own scope narrowings (no
 * pax extended headers, no cpio hard-link detection, the lenient
 * single-all-zero-block ustar end-of-archive check) reading as a false
 * mismatch. Checked instead:
 *
 *   - __util_<name>_main() returns a real exit status: every `return` in
 *     src/util/pax.c is 0 or 1 -- unlike patch.c/ar.c, no ">1 real
 *     error" tier at all, so 0/1 is the whole contract;
 *   - no exit()/_exit() call anywhere in src/util/pax.c -- caught for
 *     free by libFuzzer's own atexit-based detection, same backstop
 *     every other harness here relies on.
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
