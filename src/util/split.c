/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * split(1p): `split [-l line_count | -b n[k|m]] [-a suffix_length]
 * [file [name]]` -- splits one file into consecutively-named pieces.
 *
 * OPTIONS:
 *  -l line_count  "the number of lines in each resulting file piece"
 *  -b n[k|m]      pieces of n bytes (n*1024 with 'k', n*1,048,576 with
 *                 'm') instead of by line count -- mutually exclusive
 *                 with -l.
 *  -a suffix_length  override the default 2-character suffix width.
 *
 * Default, "[i]f no -l or -b is given ... equivalent to -l 1000".
 *
 * SUFFIX SCHEME: split(1p)'s own base-26 alphabetic suffix -- "aa",
 * "ab", ..., "az", "ba", ... -- implemented by gen_suffix() below as a
 * plain base-26 odometer over suffix_length letter positions, not a
 * decimal-number suffix.  Running out of the 26^suffix_length names
 * available at a given -a width is a real, diagnosed error ("too many
 * output files"), not silent wraparound back to "aa" (which would
 * silently overwrite the first piece with the (26^n+1)-th).
 *
 * OPERANDS: `file` -- "the pathname of the ordinary file to be split.
 * If no input file is given or file is '-', standard input shall be
 * used."  `name` -- "The prefix ... default shall be the character
 * 'x'."
 *
 * EXIT STATUS: "0 Successful completion." ">0 An error occurred."
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "util.h"

/* index 0 -> suffix_length copies of 'a', counting up like an odometer
 * with 26 positions per digit; returns -1 once index has run past the
 * 26^suffix_length names a width of suffix_length can spell. */
static int gen_suffix(char *buf, int suflen, long index)
{
	long cap = 1;
	int i;

	for (i = 0; i < suflen; i++) cap *= 26;
	if (index >= cap) return -1;
	for (i = suflen - 1; i >= 0; i--) { buf[i] = (char)('a' + index % 26); index /= 26; }
	buf[suflen] = 0;
	return 0;
}

static FILE *open_piece(const char *prefix, int suflen, long index, char *namebuf, size_t namebuf_sz)
{
	char suf[32];
	FILE *f;

	if (suflen >= (int)sizeof suf) suflen = (int)sizeof suf - 1;
	if (gen_suffix(suf, suflen, index) < 0) {
		fprintf(stderr, "split: too many output files (suffix length %d exhausted)\n", suflen);
		return 0;
	}
	snprintf(namebuf, namebuf_sz, "%s%s", prefix, suf);
	f = fopen(namebuf, "wb");
	if (!f) fprintf(stderr, "split: %s: %s\n", namebuf, strerror(errno));
	return f;
}

/* -b n[k|m] */
static int parse_bytecount(const char *s, long *out)
{
	size_t n = strlen(s);
	long mult = 1;
	char buf[32];
	char *end;
	long v;

	if (n == 0) return -1;
	if (s[n - 1] == 'k') { mult = 1024; n--; }
	else if (s[n - 1] == 'm') { mult = 1048576; n--; }
	if (n == 0 || n >= sizeof buf) return -1;
	memcpy(buf, s, n);
	buf[n] = 0;
	v = strtol(buf, &end, 10);
	if (end == buf || *end || v <= 0) return -1;
	*out = v * mult;
	return 0;
}

static int split_by_lines(FILE *in, const char *prefix, int suflen, long lcount)
{
	char *line = 0;
	size_t linecap = 0;
	long piece = 0;
	long inpiece = 0;
	FILE *out = 0;
	char namebuf[512];
	int had_output = 0;

	for (;;) {
		ssize_t n = getline(&line, &linecap, in);
		if (n < 0) break;
		if (!out || inpiece >= lcount) {
			if (out) fclose(out);
			out = open_piece(prefix, suflen, piece++, namebuf, sizeof namebuf);
			if (!out) { free(line); return -1; }
			inpiece = 0;
			had_output = 1;
		}
		fwrite(line, 1, (size_t)n, out);
		inpiece++;
	}
	free(line);
	/* split(1p) always creates at least one (possibly empty) output
	 * piece, even for a zero-byte input -- matching every real
	 * implementation's behavior for an empty file. */
	if (!had_output) {
		out = open_piece(prefix, suflen, piece, namebuf, sizeof namebuf);
		if (!out) return -1;
	}
	if (out) fclose(out);
	return 0;
}

static int split_by_bytes(FILE *in, const char *prefix, int suflen, long bcount)
{
	char *buf = malloc((size_t)bcount);
	long piece = 0;
	char namebuf[512];
	int had_output = 0;

	if (!buf) { fprintf(stderr, "split: out of memory\n"); return -1; }
	for (;;) {
		size_t got = fread(buf, 1, (size_t)bcount, in);
		FILE *out;
		if (got == 0) break;
		out = open_piece(prefix, suflen, piece++, namebuf, sizeof namebuf);
		if (!out) { free(buf); return -1; }
		fwrite(buf, 1, got, out);
		fclose(out);
		had_output = 1;
		if (got < (size_t)bcount) break; /* short read: real EOF */
	}
	if (!had_output) {
		FILE *out = open_piece(prefix, suflen, piece, namebuf, sizeof namebuf);
		if (!out) { free(buf); return -1; }
		fclose(out);
	}
	free(buf);
	return 0;
}

int __util_split_main(int argc, char **argv)
{
	int i = 1;
	long lcount = -1, bcount = -1;
	int suflen = 2;
	const char *file = "-";
	const char *prefix = "x";
	FILE *in;
	int rc;

	for (; i < argc; i++) {
		char *a = argv[i];
		if (a[0] != '-' || a[1] == 0) break;
		if (!strcmp(a, "--")) { i++; break; }
		if (!strcmp(a, "-l")) {
			char *end;
			if (i + 1 >= argc) { fprintf(stderr, "split: -l: option requires an argument\n"); return 1; }
			lcount = strtol(argv[++i], &end, 10);
			if (*end || lcount <= 0) { fprintf(stderr, "split: -l: invalid line count\n"); return 1; }
			continue;
		}
		if (!strcmp(a, "-b")) {
			if (i + 1 >= argc) { fprintf(stderr, "split: -b: option requires an argument\n"); return 1; }
			i++;
			if (parse_bytecount(argv[i], &bcount) < 0) {
				fprintf(stderr, "split: -b: invalid byte count\n");
				return 1;
			}
			continue;
		}
		if (!strcmp(a, "-a")) {
			char *end;
			if (i + 1 >= argc) { fprintf(stderr, "split: -a: option requires an argument\n"); return 1; }
			suflen = (int)strtol(argv[++i], &end, 10);
			if (*end || suflen <= 0) { fprintf(stderr, "split: -a: invalid suffix length\n"); return 1; }
			continue;
		}
		fprintf(stderr, "split: %s: invalid option\n", a);
		return 1;
	}

	if (lcount > 0 && bcount > 0) {
		fprintf(stderr, "split: -l and -b are mutually exclusive\n");
		return 1;
	}
	if (lcount < 0 && bcount < 0) lcount = 1000; /* split(1p) default */

	if (i < argc) file = argv[i++];
	if (i < argc) prefix = argv[i++];
	if (i < argc) {
		fprintf(stderr, "split: extra operand '%s'\n", argv[i]);
		return 1;
	}

	if (!strcmp(file, "-")) {
		in = stdin;
	} else {
		in = fopen(file, "rb");
		if (!in) {
			fprintf(stderr, "split: %s: %s\n", file, strerror(errno));
			return 1;
		}
	}

	rc = bcount > 0 ? split_by_bytes(in, prefix, suflen, bcount)
	                : split_by_lines(in, prefix, suflen, lcount);

	if (in != stdin) fclose(in);
	return rc < 0 ? 1 : 0;
}
