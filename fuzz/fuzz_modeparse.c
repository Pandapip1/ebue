/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_parse_mode() -- src/util/modeparse.c, the symbolic/octal
 * `mode` operand parser shared by src/util/chmod_util.c, src/util/
 * mkdir_util.c and src/util/mkfifo.c (all three XCU pages define their
 * mode operand as "the same as ... chmod"). NOT a __util_*_main()
 * entry point itself -- src/util/modeparse.h declares it with real
 * external linkage (`int __util_parse_mode(const char *prog, const
 * char *spec, mode_t base, mode_t umask_bits, mode_t *out)`, `nonnull`
 * on prog/spec/out), so unlike fuzz_resolv.c's target this needs no
 * #include of the .c file and no un-static-ing: modeparse.h is
 * included directly below, the same way fuzz_od.c and fuzz_test.c
 * reach their own __util_*_main() through src/internal/util.h.
 *
 * GRAMMAR. modeparse.h's own header comment (read in full first) gives
 * chmod(1p)'s two operand forms: an octal number, or comma-separated
 * `[who...] (op [perm...])+` clauses (who any of u/g/o/a, op one of
 * +/-/=, perm any of r/w/x -- X/s/t/permcopy are a documented,
 * deliberate gap: refused, not approximated). Both forms are reached
 * by the same fuzzer-controlled spec string; which one parse_clause()
 * or the octal branch takes depends entirely on the bytes the fuzzer
 * found, exactly as a real chmod(1) argv[] would present it.
 *
 * INPUT LAYOUT. Byte 0 is OPTION BYTE (below); the rest, verbatim and
 * capped at SPEC_CAP bytes with an embedded NUL rejected (one operand,
 * not a token stream -- the same treatment fuzz_od.c's header comment
 * gives its own -t argument, for the identical reason: a real argv
 * element cannot contain a NUL either), becomes the `spec` argument.
 *
 * OPTION BYTE. Two independent two-bit fields, each indexing a small
 * fixed table rather than deriving a value from the mask directly, so
 * every table entry gets equal weight:
 *
 *   bits 0-1  `base` -- the mode __util_parse_mode() starts from
 *             (relevant only to a clause missing `=`, or an omitted
 *             who with `+`/`-`: apply_action() ORs/ANDs into *cur,
 *             starting from base). Table: 0000, 0644, 0755, 0777 --
 *             chosen to cover "nothing set", a typical file, a
 *             typical directory/executable, and "everything set", so
 *             both a `+` that has bits to add and a `-` that has bits
 *             to remove are reachable from the same fixed corpus.
 *   bits 2-3  `umask_bits` -- filters an omitted-who clause's perm
 *             per class (modeparse.h's own quoted chmod(1p) umask
 *             rule; parse_clause()'s `mask_who`). Table: 022, 002,
 *             077, 000 -- three real-world umask values plus the
 *             no-op, covering both "some bits filtered" and "no
 *             filtering happens" from the same spec bytes.
 *
 * WHAT IS CHECKED. modeparse.h's own documented contract: 0 with *out
 * set (masked to 07777) on success, -1 (with a diagnostic already
 * written to stderr) on a malformed spec -- no third outcome. Checked
 * here as the same "the file's own contract, not a looser guess"
 * range assertion fuzz_od.c's and fuzz_test.c's own headers give their
 * targets: the return must be exactly 0 or -1, and on 0, *out must lie
 * in [0, 07777] -- modeparse.c's own two paths each enforce this
 * (the octal branch rejects v > 07777 outright; the symbolic branch
 * masks `cur & 07777` before returning), so a violation here is that
 * masking having silently stopped happening, not a looser guess about
 * what the function was ever supposed to promise.
 *
 * NO ORACLE. Like fuzz_test.c's own header reasons for test(1p)'s
 * grammar: this implementation's documented, exact scope (r/w/x only;
 * X/s/t/permcopy refused outright) is itself the specification under
 * test, and a host chmod would disagree with that scope by design,
 * not by defect.
 *
 * STDOUT/STDERR REDIRECTION: __util_diagf() (src/internal/util.h)
 * writes the "invalid mode" diagnostic straight to stderr on every
 * rejected spec, which is the overwhelming majority of fuzzer-found
 * inputs by construction -- so, like fuzz_test.c, stderr is redirected
 * to a fixed sink file once rather than hitting the real terminal on
 * millions of calls. There is no stdout output anywhere in
 * modeparse.c (checked while reading the file in full), so stdout is
 * left alone.
 *
 * NO SPAWN RISK: modeparse.c never invokes another program under any
 * input (checked while reading the file in full: no exec/system/popen
 * anywhere in it).
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
