/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tail(1p): `tail [-c number|-n number] [file...]`
 *
 * DESCRIPTION: "The tail utility shall copy its input file to standard
 * output beginning at a designated place."  "number" (for either -c or
 * -n) is a decimal integer, optionally signed:
 *  - "+": relative to the beginning of the file -- number must be
 *    non-zero, and counts from 1 ("-c +1" is the first byte, "-n +1"
 *    the first line).
 *  - "-" or no sign: relative to the end of the file -- the number of
 *    trailing bytes/lines to copy.
 * "If neither -c nor -n is specified, -n 10 shall be assumed."
 *
 * ---- Reading the whole input before writing anything ------------------
 *
 * "Relative to the end of the file" cannot be answered without knowing
 * where the end is, and unlike head(1p) (which only ever needs to stop
 * early) there is no way to stream this and still support the from-end
 * form for input that is not seekable -- a real pathname operand could
 * still be a pipe or a FIFO on this platform.  Rather than special-case
 * seekable-vs-not (lseek() to find the size for a regular file, buffer
 * everything for anything else), read_all() below always reads the
 * whole input into one growable buffer first, then both -c and -n (and
 * both signs of each) are answered as an index into that one buffer --
 * see split_lines() and the byte-offset arithmetic in
 * __util_tail_main() below.  The cost is memory proportional to the
 * whole input rather than to `number`; for the sizes this utility tier
 * is ever asked to handle in this project (see test/util-textio.c) that
 * trade is the simpler-and-correct choice over a seek-and-scan-backward
 * optimization this file does not attempt.
 *
 * -f ("do not terminate after the last line ... read the appended data")
 * is real tail(1p) behavior with no natural exit and needs a polling or
 * event loop -- there is no existing long-running poll loop anywhere in
 * this utility tier to build on (contrast rm/cp/mv/head's all-bounded
 * work), and a single-shot approximation of "follow" would silently lie
 * about what it did.  Refused loudly with a diagnostic and a nonzero
 * exit, per this project's "refuse rather than fake" rule (see touch's
 * -d, tee's -i vs. rm's -i in their own files for the same choice made
 * both ways depending on whether a real implementation exists to call).
 *
 * The multi-operand `==> file <==` banner is the same GNU/BSD-convention
 * choice documented in src/util/head.c's header, extended past XCU's own
 * single-`[file]` SYNOPSIS the same way multiple operands are: XCU says
 * nothing about more than one, this project supports it anyway for
 * symmetry with every other utility in this tier, and documents the
 * extension here rather than silently diverging from the page cited.
 *
 * EXIT STATUS: "0 Successful completion." ">0 An error occurred." --
 * diagnose-and-continue across operands.
 *
 * Spec consulted: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/tail.html
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"

enum tail_mode { TAIL_LINES, TAIL_BYTES };

static int write_all(const char *buf, size_t len)
{
	const char *p = buf;
	while (len > 0) {
		ssize_t w = write(STDOUT_FILENO, p, len);
		if (w < 0) return -1;
		p += (size_t)w;
		len -= (size_t)w;
	}
	return 0;
}

/* Reads the whole of `fd` into a malloc()'d buffer, growing it as
 * needed.  *out is set regardless of success (freeing it is always the
 * caller's job on a non-NULL result); returns the number of bytes read,
 * or (size_t)-1 on a read failure. */
static size_t read_all(int fd, char **out)
{
	size_t cap = 65536, len = 0;
	char *buf = malloc(cap);
	ssize_t r;

	if (!buf) { *out = 0; return (size_t)-1; }

	for (;;) {
		if (len == cap) {
			char *nb;
			size_t newcap;
			if (!__util_array_capacity(cap, len, 1, 65536, 1, &newcap)) {
				free(buf); *out = 0; return (size_t)-1;
			}
			nb = realloc(buf, newcap);
			if (!nb) { free(buf); *out = 0; return (size_t)-1; }
			buf = nb;
			cap = newcap;
		}
		r = read(fd, buf + len, cap - len);
		if (r < 0) { free(buf); *out = 0; return (size_t)-1; }
		if (r == 0) break;
		len += (size_t)r;
	}
	*out = buf;
	return len;
}

/* Finds the start offset of the `index`-th line (0-based) in `buf`
 * (length `len`), where a "line" is a maximal run up to and including
 * its terminating '\n', except possibly the last, which may run to
 * end-of-buffer instead.  *nlines is set to the total line count.
 * `index >= *nlines` is the caller's responsibility to check first. */
static size_t nth_line_offset(const char *buf, size_t len, size_t index, size_t *nlines)
{
	size_t i, n = 0, want_offset = 0;
	int found = 0;

	if (len == 0) { *nlines = 0; return 0; }
	if (index == 0) want_offset = 0, found = 1;

	for (i = 0; i < len; i++) {
		if (buf[i] != '\n') continue;
		if (i + 1 >= len) continue;   /* trailing newline: no new line starts after it */
		n++;
		if (!found && n == index) { want_offset = i + 1; found = 1; }
	}
	*nlines = n + 1; /* the n newlines seen mid-buffer, plus the final line itself */
	return want_offset;
}

static int tail_one(int fd, enum tail_mode mode, int from_end, long long number, const char *label)
{
	char *buf;
	size_t len;
	size_t start;
	int rc = 0;

	len = read_all(fd, &buf);
	if (len == (size_t)-1) {
		int saved = errno;
		fprintf(stderr, "tail: %s: %s\n", label, strerror(saved));
		return -1;
	}

	/* Clamp to len+1 (the largest value that can change the outcome:
	 * "at least the whole file") before any (size_t) cast below -- a
	 * huge -c/-n argument must behave like "the whole file", not wrap
	 * around through size_t's narrower range on an ILP32 build. */
	if (number > (long long)len + 1) number = (long long)len + 1;

	if (mode == TAIL_BYTES) {
		if (from_end) {
			start = (number <= 0) ? len : ((size_t)number >= len ? 0 : len - (size_t)number);
		} else {
			/* number is 1-based; "+1" is the first byte. */
			size_t k = (size_t)(number - 1);
			start = (k >= len) ? len : k;
		}
	} else {
		size_t nlines;
		if (from_end) {
			if (number <= 0) {
				start = len; /* "-n 0" (or "-n -0"): nothing to print */
			} else {
				(void)nth_line_offset(buf, len, 0, &nlines); /* just to get nlines */
				if ((size_t)number >= nlines) start = 0;
				else start = nth_line_offset(buf, len, nlines - (size_t)number, &nlines);
			}
		} else {
			/* "+K": start at the K-th line, 1-based. */
			size_t k = (size_t)(number - 1);
			(void)nth_line_offset(buf, len, 0, &nlines);
			start = (k >= nlines) ? len : nth_line_offset(buf, len, k, &nlines);
		}
	}

	if (write_all(buf + start, len - start) < 0) {
		int saved = errno;
		fprintf(stderr, "tail: %s: %s\n", label, strerror(saved));
		rc = -1;
	}
	free(buf);
	return rc;
}

/* Parses "[+|-]number" per tail(1p)'s -c/-n option-argument grammar.
 * *from_end is 1 for '-' or no sign, 0 for '+'.  Returns 0 on success,
 * -1 on anything that is not that grammar (including "+0", which the
 * spec singles out as invalid: "If the '+' ... is used, number shall be
 * non-zero"). */
static int parse_signed_number(const char *s, int *from_end, long long *number)
{
	char *end;
	long long v;
	int sign_plus = 0;

	if (*s == '+') { sign_plus = 1; s++; }
	else if (*s == '-') { s++; }
	if (!*s) return -1;
	v = strtoll(s, &end, 10);
	if (*end || v < 0) return -1;
	if (sign_plus && v == 0) return -1;
	*from_end = !sign_plus;
	*number = v;
	return 0;
}

int __util_tail_main(int argc, char **argv)
{
	int i;
	enum tail_mode mode = TAIL_LINES;
	int from_end = 1;
	long long number = 10;
	int mode_given = 0;
	int had_error = 0;
	int first_banner = 1;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];

		if (!strcmp(a, "--")) { i++; break; }
		if (!strcmp(a, "-f")) {
			fprintf(stderr, "tail: -f: not implemented -- see src/util/tail.c\n");
			return 1;
		}
		if (!strcmp(a, "-c") || !strcmp(a, "-n")) {
			enum tail_mode m = (a[1] == 'c') ? TAIL_BYTES : TAIL_LINES;
			int fe;
			long long num;

			if (i + 1 >= argc) {
				fprintf(stderr, "tail: %s: option requires an argument\n", a);
				return 1;
			}
			if (parse_signed_number(argv[++i], &fe, &num) < 0) {
				fprintf(stderr, "tail: %s: invalid number\n", argv[i]);
				return 1;
			}
			mode = m; from_end = fe; number = num; mode_given = 1;
			continue;
		}
		if (a[0] == '-' && a[1] != 0) {
			fprintf(stderr, "tail: invalid option -- '%s'\n", a);
			return 1;
		}
		break;
	}
	(void)mode_given;

	if (i >= argc)
		return tail_one(STDIN_FILENO, mode, from_end, number, "standard input") < 0 ? 1 : 0;

	{
		int noperands = argc - i;

		for (; i < argc; i++) {
			const char *path = argv[i];
			int fd;

			if (noperands > 1) {
				printf("%s==> %s <==\n", first_banner ? "" : "\n", path);
				first_banner = 0;
				if (fflush(stdout) < 0) had_error = 1;
			}

			if (!strcmp(path, "-")) {
				if (tail_one(STDIN_FILENO, mode, from_end, number, "-") < 0) had_error = 1;
				continue;
			}

			fd = open(path, O_RDONLY);
			if (fd < 0) {
				fprintf(stderr, "tail: %s: %s\n", path, strerror(errno));
				had_error = 1;
				continue;
			}
			if (tail_one(fd, mode, from_end, number, path) < 0) had_error = 1;
			(void)close(fd);
		}
	}

	return had_error ? 1 : 0;
}
