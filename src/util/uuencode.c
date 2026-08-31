/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * uuencode(1p): "The uuencode utility shall read source_file (or by
 * default, its standard input) and shall write an encoded version to
 * standard output.  The encoding uses only printing ASCII characters
 * and includes the mode of the file and the operand decode_pathname for
 * use by uudecode when re-creating the binary file."
 *
 * SYNOPSIS: `uuencode [-m] [source_file] decode_pathname`.
 *
 * ---- format, straight off the OPERANDS/DESCRIPTION text -----------------
 *
 *  - Header line: "begin mode decode_pathname\n" -- mode written as
 *    three octal digits (the traditional, universally-produced width;
 *    the standard itself just says "the file permission bits of
 *    source_file", not a field width, but every real uuencode and
 *    uudecode in the wild agrees on three digits, and uudecode.c's own
 *    parser here accepts more digits regardless via strtoul()).
 *  - Body: input consumed 45 bytes at a time; each chunk becomes one
 *    line: a length character (src/util/uucode.h's UUENC(n)) followed by
 *    ceil(n/3)*4 encoded characters, four per 3-input-byte group (the
 *    last group of a short final chunk is zero-padded before encoding,
 *    but only the length prefix says how many of the decoded bytes are
 *    real -- uudecode.c's own decoder trusts exactly that count, never
 *    the group's own full 3 bytes).
 *  - A zero-length line (UUENC(0), i.e. a lone '`') marks the end of the
 *    data, followed by a literal "end\n" line.
 *
 * mode: "the file permission bits of source_file" -- read via fstat()
 * on the real opened source_file when one was given.  With no
 * source_file (reading standard input instead), there is no real file
 * whose permission bits this could honestly report; 0644 is used as the
 * conventional default every historical uuencode falls back to in that
 * case, documented here as a real, deliberate choice rather than a
 * fstat(stdin)-derived value that would usually just describe a pipe or
 * terminal, not a file mode meant to survive a round trip.
 *
 * -m ("Base64 encoding ... instead of the historical UU encoding
 * algorithm") is a real, distinct algorithm this build does not
 * implement -- refused loudly with a diagnostic and a nonzero exit,
 * per this project's "refuse rather than silently ignore" rule (see
 * src/util/touch.c's -d for the same shape), rather than silently
 * falling back to the historical encoding under a flag that promised
 * something else.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "util.h"
#include "uucode.h"

static void emit_line(const unsigned char *buf, size_t n)
{
	size_t group, groups = n / 3 + (n % 3 != 0);

	putchar(UUENC((unsigned)n));
	for (group = 0; group < groups; group++) {
		size_t i = 3 * group;
		unsigned char b0 = buf[i];
		unsigned char b1 = (i + 1 < n) ? buf[i + 1] : 0;
		unsigned char b2 = (i + 2 < n) ? buf[i + 2] : 0;
		int c1 = (b0 >> 2) & 0x3f;
		int c2 = ((b0 << 4) | (b1 >> 4)) & 0x3f;
		int c3 = ((b1 << 2) | (b2 >> 6)) & 0x3f;
		int c4 = b2 & 0x3f;
		putchar(UUENC(c1));
		putchar(UUENC(c2));
		putchar(UUENC(c3));
		putchar(UUENC(c4));
	}
	putchar('\n');
}

int __util_uuencode_main(int argc, char **argv)
{
	int i = 1;
	const char *src_path = 0, *decode_name;
	FILE *in;
	mode_t mode = 0644; /* traditional stdin-source default -- see header */
	unsigned char buf[45];
	size_t n;
	int noperands;
	int status = 0;

	for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (!strcmp(argv[i], "--")) { i++; break; }
		if (!strcmp(argv[i], "-m")) {
			__util_diagf("uuencode: -m: Base64 encoding is not supported "
			                "by this build -- see src/util/uuencode.c\n");
			return 1;
		}
		__util_diagf("uuencode: %s: invalid option\n", argv[i]);
		return 1;
	}

	noperands = i < argc ? argc - i : 0;
	if (noperands == 1) {
		decode_name = argv[i];
		in = stdin;
	} else if (noperands == 2) {
		src_path = argv[i];
		decode_name = argv[i + 1];
		in = fopen(src_path, "rb");
		if (!in) {
			__util_diagf("uuencode: %s: %s\n", src_path, strerror(errno));
			return 1;
		}
	} else {
		__util_diagf("uuencode: usage: uuencode [-m] [source_file] decode_pathname\n");
		return 1;
	}

	if (src_path) {
		struct stat st;
		if (fstat(fileno(in), &st) == 0) mode = st.st_mode & 0777;
	}

	printf("begin %03o %s\n", (unsigned)mode, decode_name);

	while ((n = fread(buf, 1, sizeof buf, in)) > 0) emit_line(buf, n);
	if (ferror(in)) {
		__util_diagf("uuencode: %s: %s\n", src_path ? src_path : "stdin", strerror(errno));
		/* The input error is primary; close is cleanup only. */
		if (src_path) (void)fclose(in);
		return 1;
	}

	printf("`\nend\n");
	if (src_path && fclose(in) != 0) status = 1;
	if (fflush(stdout) != 0) status = 1;
	return status;
}
