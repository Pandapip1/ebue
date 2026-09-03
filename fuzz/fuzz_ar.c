/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_ar_main()'s (src/util/ar.c) read side: ar_foreach()'s
 * member-walking loop and parse_header()'s field parsing -- ar's format
 * is its own from-scratch layout, sharing no code with fuzz_pax.c's
 * ustar/cpio codecs despite the similar header/member-parser shape.
 *
 * Only -t (table of contents) is driven, deliberately: -d/-q/-r rewrite
 * the archive in place, and -x's fopen(m->name, "wb") writes to the
 * fuzzed, attacker-controlled member name with none of pax's
 * name_is_safe() '..'/absolute-path guard. -t only ever calls printf()
 * and, under -v, gmtime()/strftime() on the raw mtime field -- no
 * open()/mkdir()/rename() on this path, so the only file this harness
 * itself ever writes is the archive it hands ar, read-only from ar's
 * side.
 *
 * The archive bytes are written verbatim to a fixed path (ar only takes
 * a pathname operand, no stdin form) and read back with fread();
 * parse_header() copies each field through a fixed-size local buffer
 * with an explicit length before calling strtol() on it, so an embedded
 * NUL or non-numeric byte is just another malformed field, never a
 * memory-safety hazard. Byte 0 bit 0 selects -v; no `file...` operands
 * are given, so name_wanted() visits every member unconditionally.
 *
 * No runaway-computation bound is needed: ar_foreach()'s only loop
 * consumes AR_CAP-capped input or ends outright on every iteration --
 * a short header fread() breaks the loop, and even a garbage `size`
 * field's fseek() either fails (negative offset) or lands past the
 * capped file's real end, making the next header read the same
 * short-read case. -p/-x's per-member loops (unreached by -t) share
 * that property.
 *
 * stdout/stderr are freopen()'d once per call for the same reason as
 * fuzz_sed.c (both -t's listing lines and ar_foreach()'s diagnostics
 * write to the real streams).
 *
 * No independent oracle (this project's scope narrowings -- no GNU/BSD
 * long-name extensions, no symbol table -- would read as false
 * mismatches). Checked instead: __util_ar_main() returns 0, 1, or 2
 * (every return in ar.c is one of those), and never calls exit()/
 * _exit() -- libFuzzer's own exit-detection is the backstop.
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
	/* NUL-terminated copy for oracle_mismatch_i()'s `in` arg only -- the
	 * raw fuzzer buffer has no guaranteed NUL, unsafe for host_oracle.c's
	 * strlen-style addq(). */
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
