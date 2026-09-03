/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_parse_mode() -- src/util/modeparse.c, the symbolic/octal
 * `mode` operand parser shared by chmod_util.c, mkdir_util.c and
 * mkfifo.c. Not a __util_*_main() entry point: modeparse.h declares it
 * with real external linkage, included directly below.
 *
 * Both of chmod(1p)'s operand forms (an octal number, or comma-separated
 * `[who...] (op [perm...])+` clauses; X/s/t/permcopy are a documented,
 * deliberate gap: refused, not approximated) are reached by the same
 * fuzzer-controlled spec string -- which one parse_clause() or the octal
 * branch takes depends entirely on the bytes the fuzzer found.
 *
 * Byte 0 is the option byte (below); the rest, verbatim and capped at
 * SPEC_CAP bytes with an embedded NUL rejected (one operand, not a token
 * stream -- a real argv element can't contain a NUL either), becomes the
 * `spec` argument.
 *
 * Option byte: two independent two-bit fields, each indexing a small
 * fixed table rather than deriving a value from the mask directly, so
 * every table entry gets equal weight:
 *
 *   bits 0-1  `base` -- the mode __util_parse_mode() starts from
 *             (relevant only to a clause missing `=`, or an omitted who
 *             with `+`/`-`). Table: 0000, 0644, 0755, 0777, so both a
 *             `+` with bits to add and a `-` with bits to remove are
 *             reachable from the same fixed corpus.
 *   bits 2-3  `umask_bits` -- filters an omitted-who clause's perm per
 *             class. Table: 022, 002, 077, 000 -- three real-world
 *             values plus the no-op.
 *
 * Checked: modeparse.h's documented contract of 0 with *out set (masked
 * to 07777) on success, or -1 on a malformed spec -- no third outcome,
 * and on 0, *out must lie in [0, 07777] (modeparse.c's two paths each
 * enforce this independently, so a violation here means that masking
 * silently stopped happening).
 *
 * No oracle: like fuzz_test.c's reasoning for test(1p)'s grammar, this
 * implementation's documented, exact scope is itself the specification
 * under test, so a host chmod would disagree by design, not by defect.
 *
 * __util_diagf() writes the "invalid mode" diagnostic straight to
 * stderr on every rejected spec (the overwhelming majority of
 * fuzzer-found inputs), so stderr is redirected to a fixed sink file
 * once rather than hitting the real terminal on millions of calls.
 * modeparse.c has no stdout output and no exec/system/popen anywhere in
 * it (checked while reading the file in full).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/util/modeparse.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define SPEC_CAP 48
#define ROOT "/tmp/modeparsefz"

static int redirect_stderr(void)
{
	return freopen(ROOT "/err", "w", stderr) != 0;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static const mode_t bases[4]  = { 0000, 0644, 0755, 0777 };
	static const mode_t umasks[4] = { 0022, 0002, 0077, 0000 };
	unsigned opts;
	char spec[SPEC_CAP + 1];
	size_t n;
	mode_t base, umask_bits, out;
	int rc;

	if (size < 1) return 0;
	mkdir(ROOT, 0755);
	if (!redirect_stderr()) return 0;

	opts = data[0];
	data++; size--;

	n = size < SPEC_CAP ? size : SPEC_CAP;
	memcpy(spec, data, n);
	spec[n] = 0;
	if (memchr(spec, 0, n)) return 0;   /* embedded NUL: not one operand */

	base = bases[opts & 0x03];
	umask_bits = umasks[(opts >> 2) & 0x03];
	out = (mode_t)0xdeadbeef; /* poisoned: a success path must overwrite this */

	rc = __util_parse_mode("modeparse_fuzz", spec, base, umask_bits, &out);
	fflush(stderr);

	if (rc != 0 && rc != -1)
		oracle_mismatch_i("__util_parse_mode returned outside {0,-1}", spec, rc, 0);
	if (rc == 0 && out > 07777)
		oracle_mismatch_i("__util_parse_mode's *out escaped [0,07777]", spec, out, 07777);

	return 0;
}
