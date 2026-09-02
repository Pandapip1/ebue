/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_printf_main() -- src/util/util_printf.c, POSIX printf(1p)'s OWN
 * format-string interpreter.  Not the same code as this library's C
 * printf() family (src/stdio/printf.c, already fuzzed by fuzz_printf.c):
 * that one takes typed varargs the compiler already knows the types of;
 * this one is a shell utility where every argument is text, so it has
 * its own standalone parser for `format` (format_escape(), parse_spec(),
 * format_signed()/format_unsigned()/format_str()/format_char()/
 * format_float()) and its own, separate escape table for %b's argument
 * (expand_b_arg()) -- see that file's own header comment for the full
 * "these are two genuinely different things that happen to share a
 * name" argument.  This harness fuzzes THAT parser, never
 * src/stdio/printf.c.
 *
 * WHAT IS FUZZED.  `format` (argv[1]) and up to three copies of one
 * `argument` operand (argv[2..]) -- both slices of the same fuzz buffer,
 * split the way fuzz_fnmatch.c and fuzz_grep.c already split a pattern
 * from a subject: one header byte for a small option word, one more to
 * pick the split point.  Feeding `format` fuzzer bytes exercises
 * format_escape()'s own \-table and parse_spec()'s flag/width/precision
 * grammar (SPEC_MAX-clamped, per that file's own comment, so a
 * pathological `%99999999999999999999d` cannot overflow int while
 * accumulating); feeding `argument` fuzzer bytes as the operand of a %b
 * conversion in a fuzzer-chosen `format` exercises expand_b_arg()'s own,
 * separate \-table (including \0ddd and the \c "stop everything" early
 * return) the same way -- both tables are reachable because `format`
 * itself is fuzzed, not fixed, so the fuzzer is free to discover "%b"
 * (and "%d", "%s", "%f", ...) on its own.
 *
 * WHY REAL argv OPERANDS, NOT A FIXED FORMAT.  Both `format` and
 * `argument` are real argv elements (char*), so each is rejected
 * outright on an embedded NUL -- the same reasoning fuzz_fnmatch.c and
 * fuzz_sed.c give: such an input does not describe one C string, and
 * accepting it would let the fuzzer spend its budget re-discovering the
 * same truncation under different names.
 *
 * ARGUMENT COUNT.  Bits 0-1 of the header byte pick 0, 1, 2 or 3 copies
 * of `argument` to append -- covering printf(1p)'s "reuse format until
 * arguments are exhausted" loop (run_printf()'s do/while in
 * __util_printf_main()) with zero, one, or several arguments to cycle
 * through, and, at zero, the "extra c or s ... null string ... other
 * ... zero" exhausted-argument path this file's own header quotes.  No
 * pre-scan is needed to bound that loop the way fuzz_sed.c's
 * sed_may_loop_forever() bounds sed's b/t branch graph: the loop's own
 * termination condition is `a->i < a->n && a->any_this_pass`, and `a->n`
 * is at most 3 here, so the whole call is bounded by a small, fixed
 * number of passes over a FMT_CAP-length format string regardless of
 * what the fuzzer supplies.
 *
 * REDIRECTED, NOT DROPPED: stdout/stderr.  Same reasoning, and the same
 * mechanism, as fuzz_sed.c's header comment: __util_printf_main() writes
 * every conversion's output to the real stdout and every diagnostic (a
 * malformed `%q`, a non-numeric `%d` argument -- both common once the
 * fuzzer is choosing `format` itself) to the real stderr via
 * __util_diagf(), and this harness calls it millions of times with no
 * fork.  freopen() is used because stdout/stderr are `FILE *const` in
 * this libc (include/stdio.h) -- reusing the existing FILE* object is
 * the only way to redirect them at all.
 *
 * NO ORACLE, same reasoning src/util/util_printf.c's own header implies
 * and fuzz_sed.c's states outright: a byte-for-byte comparison against a
 * real printf(1p) would mostly be a stream of disagreements about
 * unspecified corners (this file's flag/precision handling of malformed
 * numeric arguments, `%b`'s \c early-stop point) rather than defects.
 * What is checked is src/internal/util.h's own contract -- a real
 * process exit status, never a raw errno or boolean -- narrowed, like
 * fuzz_sed.c's and fuzz_grep.c's oracles, to the values
 * src/util/util_printf.c's own code was read in full and confirmed to
 * actually return: g_status starts at 0 and is only ever set to 1 (a
 * malformed numeric argument, a write failure, an unrecognized
 * conversion letter); __util_printf_main() itself returns 1 directly
 * only for "missing operand" (argc < 2, never reached here: `format` is
 * always supplied) and otherwise returns g_status.  So the return value
 * is always 0 or 1; a third value would be a real regression.
 *
 * THE exit()-VS-return DISCIPLINE.  Checked for free by construction,
 * same as fuzz_sed.c's and fuzz_grep.c's headers explain:
 * src/util/util_printf.c was read in full while writing this harness
 * and calls neither exit() nor _exit() anywhere -- required, since
 * src/sh/builtin.c registers this alongside every other
 * __util_<name>_main() as an in-process shell builtin with no fork to
 * contain a stray exit() (src/internal/util.h's own header comment).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define FMT_CAP 128
#define ARG_CAP 64

static int redirect_streams(void)
{
	if (!freopen("/tmp/fuzz_util_printf_out", "w", stdout)) return 0;
	if (!freopen("/tmp/fuzz_util_printf_err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char opts;
	size_t split, flen, alen, i;
	int nargs;
	char fmt[FMT_CAP + 1];
	char arg[ARG_CAP + 1];
	char argv0[] = "printf";
	char *argv[6];
	int argc = 0;
	int status;

	if (size < 3) return 0;
	opts = data[0];
	split = data[1] % (size - 2);
	data += 2; size -= 2;

	flen = split < FMT_CAP ? split : FMT_CAP;
	alen = size - split;
	if (alen > ARG_CAP) alen = ARG_CAP;

	memcpy(fmt, data, flen); fmt[flen] = 0;
	if (memchr(fmt, 0, flen)) return 0; /* embedded NUL: not one operand */
	memcpy(arg, data + split, alen); arg[alen] = 0;
	if (memchr(arg, 0, alen)) return 0;

	if (!redirect_streams()) return 0;

	nargs = opts & 0x03; /* 0..3 copies of `arg`: see header on loop bounds */

	argv[argc++] = argv0;
	argv[argc++] = fmt;
	for (i = 0; i < (size_t)nargs; i++) argv[argc++] = arg;
	argv[argc] = 0;

	status = __util_printf_main(argc, argv);
	if (status != 0 && status != 1)
		oracle_mismatch_i("__util_printf_main returned neither 0 nor 1",
		                  fmt, status, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
