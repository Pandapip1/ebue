/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_cmp_main() -- src/util/cmp.c: `cmp [-l|-s] file1 file2`, a
 * streamed byte-by-byte comparison checked against the real XCU cmp(1p)
 * page (see that file's own header comment for the citations). Simpler
 * than diff(1p): no line-oriented parsing, no edit-script algorithm --
 * just CMP_CHUNK-sized fread() pairs compared byte-for-byte, with -l's
 * "keep scanning and report every differing byte" mode against the
 * default "stop at the first difference" mode, and the EOF-length
 * diagnostic when the two streams run out at different points.
 *
 * WHAT IS FUZZED, AND HOW. Like fuzz_diff.c (read that file's own header
 * comment for the fuller reasoning this harness shares), it is the two
 * files' CONTENT that is fuzzer-controlled here, not just an option
 * string -- cmp's whole surface is "compare these two byte streams", so
 * a harness that held the data fixed and only fuzzed -l/-s would never
 * exercise cmp_main()'s own read loop, its CMP_CHUNK-boundary handling
 * (a real, reachable seam: this harness's DATA_CAP is deliberately set
 * near CMP_CHUNK's own boundary behavior is unreachable at this size,
 * but the block-straddling comparison loop inside a single fread() is
 * still exactly what is under test), or its byte-number/line-number
 * accounting. The fuzz buffer, after a 1-byte option header, is split on
 * the first NUL byte into two chunks (each capped at DATA_CAP bytes) and
 * written verbatim to two fixed paths (FILE1/FILE2) cmp is always
 * invoked against; no NUL in the remainder puts the whole capped
 * remainder in FILE1 and leaves FILE2 empty, the same real "compare
 * against an empty file" case fuzz_diff.c's header describes forcing the
 * EOF-length path (n1 != n2 with one side 0) on essentially every such
 * call.
 *
 * OPTION BYTE. byte 0 mod 5 selects: 0 neither flag; 1 "-l"; 2 "-s"; 3
 * "-l" and "-s" together, deliberately reaching __util_cmp_main()'s own
 * "opt_l && opt_s ... mutually exclusive" refusal (return 2) -- a real
 * error shape no combination of a single flag could ever reach; 4 a
 * bogus "-x", reaching the "invalid option" refusal (also return 2).
 * Both refusal paths are real, documented control flow this harness
 * exists to run under the fuzzer's input mix, not merely to have exist
 * in the source (the same reasoning fuzz_cut.c's own header gives for
 * its -n "not implemented" refusal).
 *
 * NO SPAWN, NO WRITE RISK BEYOND THIS HARNESS'S OWN FIXTURES. cmp(1p)
 * never invokes another program (checked by reading src/util/cmp.c in
 * full -- no exec/system/popen/fork anywhere in the file) and never
 * opens any path for writing at all -- both operands are opened "rb"
 * only (cmp_open()). FILE1/FILE2 are the
 * only files this harness itself ever creates, both capped at DATA_CAP
 * and truncated fresh every call.
 *
 * STDOUT/STDERR REDIRECTION: the same freopen()-a-fixed-sink-file-once-
 * per-call mechanism fuzz_diff.c's, fuzz_sort.c's and fuzz_od.c's own
 * header comments give, for the identical reason -- the default and -l
 * paths both print to stdout on a difference, and every open/read error
 * plus the EOF diagnostic write to stderr via __util_diagf()/printf().
 *
 * WHAT IS CHECKED. This file's own header comment's EXIT STATUS section
 * verbatim: "0 The files are identical. 1 The files are different ...
 * >1 An error occurred." -- and reading __util_cmp_main() in full shows
 * every `return` in it is exactly 0, 1 or 2, never any value past 2, so
 * the assertion below checks that real, narrower range rather than a
 * looser ">1" guess.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define DATA_CAP 512

#define ROOT "/tmp/cmpfz"
#define FILE1 ROOT "/f1"
#define FILE2 ROOT "/f2"

static void ensure_root(void)
{
	static int done;
	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
}

static void write_file(const char *path, const unsigned char *data, size_t len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, data, len);
	close(fd);
}

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opt0;
	const unsigned char *rest;
	size_t restlen, len1, len2;
	const unsigned char *sep;
	char *argv[6];
	int argc = 0;
	int rc;
	char diagbuf[DATA_CAP + 1];
	size_t dn;

	if (size < 1) return 0;
	ensure_root();

	opt0 = data[0];
	data++; size--;

	dn = size < DATA_CAP ? size : DATA_CAP;
	memcpy(diagbuf, data, dn);
	diagbuf[dn] = 0;

	rest = data;
	restlen = size;
	sep = memchr(rest, 0, restlen);
	if (sep) {
		len1 = (size_t)(sep - rest);
		len2 = restlen - len1 - 1;
		if (len1 > DATA_CAP) len1 = DATA_CAP;
		if (len2 > DATA_CAP) len2 = DATA_CAP;
		write_file(FILE1, rest, len1);
		write_file(FILE2, sep + 1, len2);
	} else {
		len1 = restlen > DATA_CAP ? DATA_CAP : restlen;
		write_file(FILE1, rest, len1);
		write_file(FILE2, rest, 0);
	}

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"cmp";
	switch (opt0 % 5) {
	case 0: break;
	case 1: argv[argc++] = (char *)"-l"; break;
	case 2: argv[argc++] = (char *)"-s"; break;
	case 3: argv[argc++] = (char *)"-l"; argv[argc++] = (char *)"-s"; break;
	default: argv[argc++] = (char *)"-x"; break; /* invalid option */
	}
	argv[argc++] = (char *)FILE1;
	argv[argc++] = (char *)FILE2;
	argv[argc] = NULL;

	rc = __util_cmp_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("__util_cmp_main returned outside {0,1,2}", diagbuf, rc, 0);

	return 0;
}
