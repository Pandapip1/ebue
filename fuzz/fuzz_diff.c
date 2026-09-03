/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_diff_main() -- src/util/diff.c: `diff [-c|-e|-f|-u|-C n|-U n]
 * [-br] file1 file2`. This harness fuzzes BOTH halves of the file: the
 * option parser (set_fmt()'s mutual-exclusion check, -C/-U's own
 * strtol()-and-range validation) AND, more importantly, the two-file
 * comparison machinery that parser feeds -- split_lines() (including the
 * no-trailing-newline `*noeol` path), lines_equal_b()/-b's blank-run
 * comparison, myers_build_ops()'s trace-storing O(ND) edit-script
 * computation, build_hunks(), and build_groups()'s overlap-merge test
 * plus each of the five output formatters.
 *
 * Unlike most harnesses in this directory, which hold a small fixed DATA
 * fixture constant and fuzz only an operand string, diff's whole reason
 * to exist is comparing two bodies of text -- so here it is the DATA
 * itself, not just an option string, that is fuzzer-controlled: the
 * buffer, after a fixed 2-byte option header, is split on the first NUL
 * byte into two chunks, each capped at DATA_CAP bytes, and written
 * verbatim to two fixed file paths (FILE1/FILE2) diff is always invoked
 * against. No NUL in the remainder puts the whole (capped) remainder in
 * FILE1 and leaves FILE2 empty -- a real, reachable "compare something
 * against an empty file" case. This directly drives split_lines()/
 * myers_build_ops() with content a human author would never think to
 * write: embedded NULs inside a "line" (never treated specially by
 * split_lines(), which only ever looks for '\n'), no final newline (the
 * *noeol path both -c/-u's "\ No newline at end of file" marker and
 * -e/-f's stderr-diagnostic-and-exit-2 path key off), and every length
 * relationship between the two files.
 *
 * Byte 0: bit 0 -b, bit 1 -r (recursion is irrelevant here -- FILE1/
 * FILE2 are always regular files, never directories, so -r's own branch
 * in diff_dirs() is never reached; -r is still passed some of the time
 * purely so the flag byte itself is exercised through the option
 * scanner). Bits 2-4 select the format, one value per real format plus
 * one that deliberately reaches set_fmt()'s conflict path: 0 default,
 * 1 -c, 2 -u, 3 -e, 4 -f, 5 "-C <n>", 6 "-U <n>", 7 "-c -u" together --
 * two format flags in one invocation, which set_fmt()'s "opts->fmt_set
 * && opts->fmt != fmt" check must refuse (return 2) via `goto conflict`,
 * a real error shape this harness would otherwise never reach. Byte 1:
 * the -C/-U context count, taken mod (CTX_CAP+1) so the argument is
 * always a small, in-range decimal literal -- -C additionally requires
 * >=1, so 0 there is deliberately still handed to diff (exercising the
 * `n < (fmtc == 'C' ? 1 : 0)` rejection). CTX_CAP is small (31) not for
 * a memory-safety reason but so grouping has interesting
 * adjacent-hunk-merging behavior to exercise against FILE1/FILE2 bodies
 * that are themselves only DATA_CAP bytes.
 *
 * No spawn, no unbounded write risk: diff(1p) as implemented never
 * invokes another program (checked while reading diff.c in full -- no
 * exec/system/popen/fork anywhere in the file) and never writes to any
 * path but its own stdout (redirected below); FILE1/FILE2 are the only
 * files this harness itself ever creates, both capped at DATA_CAP and
 * truncated fresh every call.
 *
 * stdout/stderr are redirected: every one of print_default()/print_ed()/
 * print_falt()/print_context_group()/print_unified_group() writes real
 * hunk text to stdout on every one of millions of calls, and the -e/-f
 * noeol case and every usage error write a diagnostic to stderr via
 * __util_diagf().
 *
 * Checked: this file's EXIT STATUS section ("0 No differences ...
 * 1 Differences ... >1 An error occurred"), narrowed to the exact range
 * __util_diff_main() can actually return on the regular-file
 * (non-directory) path this harness always takes: reading diff_files()
 * and __util_diff_main() in full shows every `return` on that path is
 * exactly 0, 1 or 2 (2 for every usage/parse/I-O error and for the
 * -e/-f noeol promotion, 1 for "differences were found", 0 for "none
 * were") -- diff_dirs()'s own wider >1 range is unreachable here since
 * FILE1/FILE2 are always plain files, never directories.
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
#define CTX_CAP 31

#define ROOT "/tmp/difffz"
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

/* stdout/stderr redirection -- see this file's header comment. */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opt0, ctxbyte;
	unsigned fmtsel;
	char ctxstr[8];
	const unsigned char *rest;
	size_t restlen, len1, len2;
	const unsigned char *sep;
	char *argv[10];
	int argc = 0;
	int rc;
	char diagbuf[DATA_CAP + 1];
	size_t dn;

	if (size < 2) return 0;
	ensure_root();

	opt0 = data[0];
	ctxbyte = data[1];
	data += 2; size -= 2;

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

	snprintf(ctxstr, sizeof ctxstr, "%u", ctxbyte % (CTX_CAP + 1));

	argv[argc++] = (char *)"diff";
	if (opt0 & 0x01) argv[argc++] = (char *)"-b";
	if (opt0 & 0x02) argv[argc++] = (char *)"-r";

	fmtsel = (opt0 >> 2) & 0x07;
	switch (fmtsel) {
	case 0: break;
	case 1: argv[argc++] = (char *)"-c"; break;
	case 2: argv[argc++] = (char *)"-u"; break;
	case 3: argv[argc++] = (char *)"-e"; break;
	case 4: argv[argc++] = (char *)"-f"; break;
	case 5: argv[argc++] = (char *)"-C"; argv[argc++] = ctxstr; break;
	case 6: argv[argc++] = (char *)"-U"; argv[argc++] = ctxstr; break;
	default: /* 7: deliberate conflict -- see OPTION BYTES above. */
		argv[argc++] = (char *)"-c";
		argv[argc++] = (char *)"-u";
		break;
	}

	argv[argc++] = (char *)FILE1;
	argv[argc++] = (char *)FILE2;
	argv[argc] = NULL;

	rc = __util_diff_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("__util_diff_main returned outside {0,1,2}", diagbuf, rc, 0);

	return 0;
}
