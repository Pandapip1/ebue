/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_sed_main()'s (src/util/sed.c) script parser and its
 * BRE-driven execution engine. The fuzz buffer becomes a script fed via
 * `-f` against a small fixed fixture file -- the fixture content is NOT
 * derived from fuzz input, since BRE matching against arbitrary text is
 * fuzz_regex.c's job, not this file's. `-f` (not `-e`) sidesteps having
 * to build a NUL-terminated argument from bytes that may contain an
 * embedded NUL (which is also rejected outright below). Byte 0 selects
 * `-n`; the rest, capped at SCRIPT_CAP, is the script.
 *
 * stdout/stderr are freopen()'d once per call so sed's p/P/l/= output
 * and error diagnostics don't flood the terminal across millions of
 * in-process iterations.
 *
 * sed's b/t branch graph has no iteration bound of its own -- a script
 * as short as ":x;bx" is valid, non-terminating sed and trivial for a
 * mutator to find. SCRIPT_CAP bounds regexec() cost per call but not
 * this; sed_may_loop_forever() below statically rejects the hazardous
 * shape before __util_sed_main() ever runs (see its own comment).
 *
 * No independent oracle exists for sed (GNU sed's extensions diverge
 * from POSIX). What's checked is this project's own contract instead:
 * __util_sed_main() must return 0 or 1 (src/internal/util.h), never a
 * raw errno; and since bi_sed() runs it in-process with no fork, an
 * exit()/_exit() call on any path would kill the whole fuzzing process
 * -- not special-cased here, since libFuzzer's own exit-detection
 * already surfaces that as a finding if it ever happens.
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

#define ROOT "/tmp/sedfz"
#define SCRIPT_CAP 480
#define MAXSEG (SCRIPT_CAP + 1)

static void write_file(const char *path, const char *data, size_t len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, data, len);
	close(fd);
}

static void fixture(void)
{
	static int done;
	static const char data[] =
		"hello world\n"
		"foo123bar\n"
		"\ttabbed\tfield\n"
		"special & chars \\ here\n"
		"\n"
		"the quick brown fox jumps\n";

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
	write_file(ROOT "/data", data, sizeof data - 1);
}

/* Approximate scan for the one hazard sed_may_loop_forever() must catch:
 * a b/t branch to a label seen at or before it (a backward or self
 * branch) with no n/N/d/D/q/Q segment textually between the two, since
 * those are the only commands that end the cycle or consume input and
 * so bound how many times the backward edge can be crossed. The scan
 * does not evaluate addresses, so it can both miss a script that still
 * loops (the "consuming" command it found is address-gated and never
 * fires -- caught by the outer watchdog instead) and flag one that
 * would have terminated; both are tolerable, since backward branches at
 * all are rare and a lost coverage sample is cheap next to a stuck run.
 */
struct sed_seg { size_t off, len; };

static size_t sed_split_segments(const char *s, size_t n, struct sed_seg *out, size_t maxout)
{
	size_t i, start = 0, cnt = 0;
	unsigned bs = 0;

	for (i = 0; i < n; i++) {
		char c = s[i];
		if (c == '\\') { bs++; continue; }
		if ((c == ';' || c == '\n') && (bs % 2) == 0) {
			if (cnt < maxout) { out[cnt].off = start; out[cnt].len = i - start; cnt++; }
			start = i + 1;
		}
		bs = 0;
	}
	if (cnt < maxout) { out[cnt].off = start; out[cnt].len = n - start; cnt++; }
	return cnt;
}

static const char *sed_skip_ws(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	return p;
}

static void sed_skip_one_address(const char **pp, const char *end)
{
	const char *p = *pp;

	if (p < end && *p >= '0' && *p <= '9') {
		while (p < end && *p >= '0' && *p <= '9') p++;
		*pp = p;
		return;
	}
	if (p < end && *p == '$') { *pp = p + 1; return; }
	if (p < end && (*p == '/' || (*p == '\\' && p + 1 < end))) {
		char delim = (*p == '/') ? '/' : p[1];
		p += (*p == '/') ? 1 : 2;
		while (p < end && *p != delim) {
			if (*p == '\\' && p + 1 < end) p += 2;
			else p++;
		}
		if (p < end) p++; /* closing delimiter */
		*pp = p;
	}
}

enum sed_seg_kind { SK_OTHER, SK_LABEL, SK_BRANCH, SK_CONSUME };

static enum sed_seg_kind sed_classify(const char *s, size_t off, size_t len,
                                       const char **label, size_t *labellen)
{
	const char *p = s + off, *end = s + off + len;

	p = sed_skip_ws(p, end);
	sed_skip_one_address(&p, end);
	p = sed_skip_ws(p, end);
	if (p < end && *p == ',') {
		p++;
		p = sed_skip_ws(p, end);
		sed_skip_one_address(&p, end);
		p = sed_skip_ws(p, end);
	}
	while (p < end && *p == '!') { p++; p = sed_skip_ws(p, end); }
	if (p >= end) return SK_OTHER;

	if (*p == ':') {
		const char *q;
		p++;
		p = sed_skip_ws(p, end);
		q = p;
		while (q < end && *q != ' ' && *q != '\t') q++;
		*label = p;
		*labellen = (size_t)(q - p);
		return SK_LABEL;
	}
	if (*p == 'b' || *p == 't') {
		p++;
		p = sed_skip_ws(p, end);
		*label = p;
		*labellen = (size_t)(end - p);
		return SK_BRANCH;
	}
	if (*p == 'n' || *p == 'N' || *p == 'd' || *p == 'D' || *p == 'q' || *p == 'Q')
		return SK_CONSUME;
	return SK_OTHER;
}

struct sed_label { const char *name; size_t len; size_t idx; };

static int sed_may_loop_forever(const char *s, size_t n)
{
	static struct sed_seg segs[MAXSEG];
	static struct sed_label labels[MAXSEG];
	size_t nseg, nlabels = 0, i;

	nseg = sed_split_segments(s, n, segs, MAXSEG);

	for (i = 0; i < nseg; i++) {
		const char *lab;
		size_t lablen;
		enum sed_seg_kind k = sed_classify(s, segs[i].off, segs[i].len, &lab, &lablen);

		if (k == SK_LABEL) {
			if (nlabels < MAXSEG) {
				labels[nlabels].name = lab;
				labels[nlabels].len = lablen;
				labels[nlabels].idx = i;
				nlabels++;
			}
		} else if (k == SK_BRANCH && lablen != 0) {
			size_t j;
			for (j = 0; j < nlabels; j++) {
				size_t between;
				int consumes;

				if (labels[j].len != lablen || labels[j].idx > i) continue;
				if (memcmp(labels[j].name, lab, lablen) != 0) continue;

				consumes = 0;
				for (between = labels[j].idx; between <= i; between++) {
					const char *l2;
					size_t ll2;
					if (sed_classify(s, segs[between].off, segs[between].len, &l2, &ll2) == SK_CONSUME) {
						consumes = 1;
						break;
					}
				}
				if (!consumes) return 1;
			}
		}
	}
	return 0;
}

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char script[SCRIPT_CAP + 1];
	size_t n;
	int opt_n;
	int status;
	char *argv[6];
	int argc = 0;

	if (size < 1) return 0;
	fixture();

	opt_n = data[0] & 1;
	data++; size--;

	n = size < SCRIPT_CAP ? size : SCRIPT_CAP;
	memcpy(script, data, n);
	script[n] = 0;
	if (memchr(script, 0, n)) return 0; /* embedded NUL: not one script */

	if (sed_may_loop_forever(script, n)) return 0;

	write_file(ROOT "/script", script, n);

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"sed";
	if (opt_n) argv[argc++] = (char *)"-n";
	argv[argc++] = (char *)"-f";
	argv[argc++] = (char *)ROOT "/script";
	argv[argc++] = (char *)ROOT "/data";
	argv[argc] = 0;

	status = __util_sed_main(argc, argv);
	if (status != 0 && status != 1)
		oracle_mismatch_i("__util_sed_main returned neither 0 nor 1", script, status, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
