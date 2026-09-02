/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * uudecode(1p): "The uudecode utility shall read a file, or standard
 * input if no file operand is given, that was created by uuencode, and
 * shall recreate the original file, including the file mode."
 *
 * SYNOPSIS: `uudecode [-o outfile] [file]` (-o is the one real POSIX
 * option: "Use outfile, rather than the pathname contained inside the
 * file, as the name of the recreated file.").
 *
 * ---- parsing, mirroring src/util/uuencode.c's format exactly -----------
 *
 *  - Any lines before "begin mode filename" are skipped -- historical
 *    uudecode has always tolerated (and even expected) leading mail
 *    headers a transport prepended, and DESCRIPTION's own "shall ignore
 *    any leading header lines" language matches: only the first line
 *    that actually starts with "begin " is treated as the real header.
 *    No such line at all is a diagnosed error, not a silent no-op.
 *  - mode is parsed as octal (strtoul(..., 8)); filename is the rest of
 *    the line after the mode field, so a filename containing spaces is
 *    still read correctly (only the mode field is whitespace-delimited).
 *  - Each data line's first character is a length (src/util/uucode.h's
 *    UUDEC()); a decoded length of 0 marks the end of the data and must
 *    be followed by a line reading "end" -- anything else there
 *    (including EOF) is a truncated-stream error, diagnosed and refused
 *    rather than treated as an implicit terminator.
 *  - Every character read as data is validated via uu_valid_char()
 *    first: a byte outside the uuencoding alphabet (corruption, or a
 *    stream mangled by a text-mode transfer, or simply not
 *    uuencoded data at all) is a diagnosed error immediately, never
 *    silently decoded into a plausible-looking wrong byte.
 *
 * ---- the output file's mode --------------------------------------------
 *
 * "shall recreate the original file, including the file mode" --
 * chmod()'d, after the file is fully written, to exactly the mode octal
 * value parsed from the begin line (masked to the permission bits,
 * 07777, the same mask src/util/modeparse.h's own contract uses) --
 * applied even when -o overrides the filename the header itself names,
 * since -o only replaces *where* the file goes, not what mode it is
 * supposed to have.
 *
 * No path sanitization is applied to the header's filename (or -o's
 * value): whatever names a creatable, writable path on this platform is
 * used as given, exactly the literal behaviour the standard describes
 * ("the pathname contained inside the file"). This is the same trust
 * model src/util/rm.c's/cp.c's own operand handling already has for any
 * other pathname operand -- nothing here treats a uuencoded stream as
 * more or less trustworthy input than an ordinary command-line pathname.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "util.h"
#include "uucode.h"

static size_t chomp(char *s, size_t n)
{
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
	return n;
}

/* Decodes one data line (`have` bytes, already chomped) into `out`, up to
 * `n` real bytes (uuencode.c's own length prefix, parsed by the caller).
 * Returns 0 on success, -1 (diagnostic already written) on a malformed
 * or truncated line. */
static int decode_line(const char *prog, const char *line, size_t have,
                       unsigned n, FILE *out) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	size_t needed = ((size_t)n + 2) / 3 * 4;
	size_t pos, written = 0;

	if (have < needed) {
		__util_diagf("%s: truncated data line (need %zu characters, got %zu)\n", prog, needed, have);
		return -1;
	}

	for (pos = 0; pos < needed; pos += 4) {
		char c1 = line[pos], c2 = line[pos + 1], c3 = line[pos + 2], c4 = line[pos + 3];
		unsigned char outbuf[3];
		size_t take;

		if (!uu_valid_char(c1) || !uu_valid_char(c2) || !uu_valid_char(c3) || !uu_valid_char(c4)) {
			__util_diagf("%s: invalid character in uuencoded data\n", prog);
			return -1;
		}
		{
			int d1 = UUDEC(c1), d2 = UUDEC(c2), d3 = UUDEC(c3), d4 = UUDEC(c4);
			outbuf[0] = (unsigned char)(((unsigned)d1 << 2) | (unsigned)d2 >> 4);
			outbuf[1] = (unsigned char)(((unsigned)(d2 & 0xf) << 4) | (unsigned)d3 >> 2);
			outbuf[2] = (unsigned char)(((unsigned)(d3 & 0x3) << 6) | (unsigned)d4);
		}
		take = n - written;
		if (take > 3) take = 3;
		if (fwrite(outbuf, 1, take, out) != take) {
			__util_diagf("%s: write error: %s\n", prog, strerror(errno));
			return -1;
		}
		written += take;
	}
	return 0;
}

int __util_uudecode_main(int argc, char **argv)
{
	int i = 1;
	const char *out_override = 0, *in_path = 0;
	FILE *in, *out;
	char line[1024];
	int found_begin = 0, terminated = 0, status = 0;
	unsigned long mode_val;
	char *p, *end;
	const char *filename, *outname;

	for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (!strcmp(argv[i], "--")) { i++; break; }
		if (!strcmp(argv[i], "-o")) {
			if (i + 1 >= argc) { __util_diagf("uudecode: -o: option requires an argument\n"); return 1; }
			out_override = argv[++i];
			continue;
		}
		__util_diagf("uudecode: %s: invalid option\n", argv[i]);
		return 1;
	}
	if (i < argc) in_path = argv[i++];
	if (i < argc) { __util_diagf("uudecode: too many operands\n"); return 1; }

	in = in_path ? fopen(in_path, "rb") : stdin;
	if (!in) {
		int saved = errno;
		__util_diagf("uudecode: %s: %s\n", in_path, strerror(saved));
		return 1;
	}

	while (fgets(line, sizeof line, in)) {
		(void)chomp(line, strnlen(line, sizeof line));
		if (!strncmp(line, "begin ", 6)) { found_begin = 1; break; }
	}
	if (!found_begin) {
		__util_diagf("uudecode: %s: no valid \"begin\" line found\n", in_path ? in_path : "stdin");
		/* Parse failure is primary; these closes are cleanup only. */
		if (in_path) (void)fclose(in);
		return 1;
	}

	p = line + 6;
	mode_val = strtoul(p, &end, 8);
	if (end == p || *end != ' ') {
		__util_diagf("uudecode: %s: malformed begin line\n", in_path ? in_path : "stdin");
		/* The malformed header is primary; close only releases the input. */
		if (in_path) (void)fclose(in);
		return 1;
	}
	while (*end == ' ') end++;
	filename = end;
	if (!*filename) {
		__util_diagf("uudecode: %s: begin line has no filename\n", in_path ? in_path : "stdin");
		/* The missing filename is primary; close only releases the input. */
		if (in_path) (void)fclose(in);
		return 1;
	}
	outname = out_override ? out_override : filename;

	out = fopen(outname, "wb");
	if (!out) {
		__util_diagf("uudecode: %s: %s\n", outname, strerror(errno));
		/* Output-open failure is primary; input close is cleanup only. */
		if (in_path) (void)fclose(in);
		return 1;
	}

	while (fgets(line, sizeof line, in)) {
		size_t line_len;
		unsigned n;
		line_len = chomp(line, strnlen(line, sizeof line));
		if (!line[0]) continue; /* tolerate a stray blank line between records */
		if (!uu_valid_char(line[0])) {
			__util_diagf("uudecode: %s: invalid length character in data\n", in_path ? in_path : "stdin");
			goto fail;
		}
		n = (unsigned)UUDEC(line[0]);
		if (n == 0) { terminated = 1; break; }
		if (n > 45) {
			__util_diagf("uudecode: %s: data line length %u out of range\n", in_path ? in_path : "stdin", n);
			goto fail;
		}
		if (decode_line("uudecode", line + 1, line_len - 1, n, out) < 0) goto fail;
	}
	if (!terminated) {
		__util_diagf("uudecode: %s: truncated uuencoded stream (no terminator line)\n", in_path ? in_path : "stdin");
		goto fail;
	}
	if (!fgets(line, sizeof line, in)) {
		__util_diagf("uudecode: %s: truncated uuencoded stream (no \"end\" line)\n", in_path ? in_path : "stdin");
		goto fail;
	}
	{
		size_t line_len = chomp(line, strnlen(line, sizeof line));
		if (line_len != 3 || memcmp(line, "end", 3) != 0) {
			__util_diagf("uudecode: %s: missing \"end\" line\n", in_path ? in_path : "stdin");
			goto fail;
		}
	}

	if (fclose(out) < 0) {
		__util_diagf("uudecode: %s: %s\n", outname, strerror(errno));
		/* Output-close failure is primary; input close is cleanup only. */
		if (in_path) (void)fclose(in);
		return 1;
	}
	if (in_path && fclose(in) < 0) {
		__util_diagf("uudecode: %s: %s\n", in_path, strerror(errno));
		status = 1;
	}

	if (chmod(outname, (mode_t)(mode_val & 07777)) != 0) {
		__util_diagf("uudecode: %s: chmod: %s\n", outname, strerror(errno));
		status = 1;
	}
	return status;

fail:
	/* A decode/parse failure selected this path; both closes are cleanup and
	 * cannot supersede that primary nonzero result. */
	(void)fclose(out);
	if (in_path) (void)fclose(in);
	return 1;
}
