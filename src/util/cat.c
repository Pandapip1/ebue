/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cat(1p): `cat [-u] [file...]`
 *
 * DESCRIPTION: "The cat utility shall read files in sequence and shall
 * write their contents to the standard output in the same sequence."
 * Byte for byte, with no line-ending translation of any kind -- this is
 * the one utility in this tier where "copy" means exactly that.
 *
 * OPERANDS: "If no file operands are specified, the standard input shall
 * be used.  If a file is '-', the cat utility shall read from the
 * standard input at that point in the sequence" -- so "-" is not just a
 * synonym for "no operands", it can appear anywhere in a mixed operand
 * list and is honored positionally.
 *
 * OPTIONS: -u ("Write bytes from the input file to the standard output
 * without delay as each is read") is accepted and is a real no-op here:
 * __util_copy_stream() below already writes every block it reads
 * immediately, with no buffering layer above the raw read()/write() pair
 * that -u's "without delay" could turn off -- there is nothing to
 * disable.  Accepted rather than refused (unlike touch's -d or rm's -i)
 * because every observable behavior -u could change is already the
 * behavior this file has regardless of the flag.
 *
 * EXIT STATUS: "0 All input files were output successfully." ">0 An
 * error occurred." -- diagnose-and-continue across operands, the same
 * shape rm/cp/mv/touch already established: one unreadable operand does
 * not stop the rest from being copied, and the final exit status is
 * still nonzero.
 *
 * Spec consulted: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/cat.html
 */
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"

/* Copies every byte of `in` to fd 1 (standard output), diagnosing under
 * `label` (the operand text as given, "standard input" for the no-operand
 * and "-" cases) on either a read or a write failure.  Does not close
 * `in`: the caller owns that, since fd 0 (stdin, used for both "-" and
 * the no-operand case) must never be closed by an interior helper that
 * might be called on it more than once in one invocation. */
static int copy_stream(int in, const char *label)
{
	char buf[65536];
	ssize_t n;

	while ((n = read(in, buf, sizeof buf)) > 0) {
		char *p = buf;
		ssize_t left = n;
		while (left > 0) {
			ssize_t w = write(STDOUT_FILENO, p, (size_t)left);
			if (w < 0) {
				fprintf(stderr, "cat: %s: %s\n", label, strerror(errno));
				return -1;
			}
			p += w;
			left -= w;
		}
	}
	if (n < 0) {
		fprintf(stderr, "cat: %s: %s\n", label, strerror(errno));
		return -1;
	}
	return 0;
}

int __util_cat_main(int argc, char **argv)
{
	int i, had_error = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-u")) continue;  /* real no-op -- see header */
		break;
	}

	if (i >= argc) {
		/* No file operands at all: read standard input once. */
		return copy_stream(STDIN_FILENO, "standard input") < 0 ? 1 : 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i];
		int fd;

		if (!strcmp(path, "-")) {
			if (copy_stream(STDIN_FILENO, "-") < 0) had_error = 1;
			continue;
		}

		fd = open(path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "cat: %s: %s\n", path, strerror(errno));
			had_error = 1;
			continue;
		}
		if (copy_stream(fd, path) < 0) had_error = 1;
		(void)close(fd);
	}

	return had_error ? 1 : 0;
}
