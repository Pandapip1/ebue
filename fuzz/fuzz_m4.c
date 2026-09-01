/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_m4_main() -- src/util/m4.c, POSIX m4(1p): a real, hand-written
 * recursive macro-expansion engine (one scan()/collect_args() loop
 * shared by the top-level stream and every macro call's own argument
 * collection -- see that file's own header comment for the scanning
 * model in full), all thirty-two mandatory builtins, depth-counted
 * quote nesting, and a 32-bit precedence-climbing eval() evaluator.
 * Exactly the kind of code this project's fuzz suite already prioritizes
 * (see fuzz/fuzz_regex.c's and fuzz/fuzz_shparse.c's header comments for
 * why a hand-written parser/interpreter is worth this attention), and
 * -- unlike most of Tier 4's other "bigger engines" -- one with a
 * textbook recursive-expansion hazard baked into the language itself.
 *
 * WHAT IS FUZZED, AND HOW.  m4 takes its program as a `file` operand,
 * not stdin-only, and stdin/stdout/stderr are `extern FILE *const` in
 * this library's <stdio.h> -- not reassignable, so fmemopen()-onto-stdin
 * (fuzz_shparse.c's non-option, since it has no stream API to redirect
 * either) is not an option here regardless. This harness uses
 * fuzz_glob.c's fixed-path-under-/tmp technique instead: each call
 * writes the fuzz buffer to a real file (fuzz/ntstubs.c's simulated
 * volume has /tmp from start-up) and passes that path as m4's one file
 * operand, exactly the form POSIX m4(1p) documents ("m4 [-s] [-Dname[=val]]
 * ... [-Uname]... [file...]"). The leading byte of the fuzzer input is
 * spent selecting which of -s/-D/-U accompany the call (see below),
 * spending it there rather than on the program text is what actually
 * exercises __util_m4_main()'s own option-parsing loop, which a fixed
 * argv = {"m4", path} would leave completely untested.
 *
 * TWO GUARDS THIS HARNESS DOES NOT NEED TO PROVIDE ITSELF, AND WHY.
 * Point 2 of this file's own design brief was "does m4.c already have a
 * recursion-depth or expansion-count guard, and if not, is that a real
 * hardening gap" -- checked directly against the source before writing
 * a line of this harness: it did not. `define(a,a)a` -- a macro whose
 * expansion is itself -- looped forever (the top-level scan() loop
 * pushes the expansion back onto the input and reads it right back off,
 * per the file's own "one loop" scanning-model description), and
 * `len(len(len(...)))` -- reachable with NO prior define() at all,
 * since every one of the 32 builtins is predefined from m4_init() --
 * recursed for real C-stack depth with no bound either. Both are now
 * fixed IN src/util/m4.c itself (M4_MAX_EXPANSIONS, M4_MAX_DEPTH; see
 * that file's "runaway expansion is bounded, and why that is not a
 * correctness fix" header section for the full reasoning on why this
 * belongs in the library and not just this harness): a self-referential
 * macro no longer eats a whole fuzzing campaign's time budget on a
 * single trial -- see this project's tools/fuzz.sh, whose own long
 * comment on SIGALRM explains why libFuzzer's per-unit -timeout is
 * unavailable here (ntlibc's sigaction() is a strong symbol that
 * shadows libFuzzer's own handler registration, so the built-in
 * mechanism silently does nothing) and why every harness in this tree
 * relies solely on an outer, whole-campaign `timeout` wrapper instead.
 * Without the library-side bound, THIS harness would be exactly the
 * "unbounded fuzzer-found infinite expansion... every timeout looks the
 * same, 'found nothing, ran out of time'" case its own design brief
 * warned against -- indistinguishable from a harness that never ran at
 * all. With it, a self-referential input still reliably terminates
 * (nonzero status, one diagnostic line) rather than hanging, so no
 * signal-based watchdog of this harness's own is needed either.
 *
 * SYSCMD/SYSCMD IS A SAFE NO-OP HERE, MEASURED NOT ASSUMED. m4's
 * mandatory syscmd() builtin calls this library's system() (see
 * src/util/m4.c's own header comment on why syscmd, not esyscmd, is
 * what POSIX actually specifies), which is a real hazard to fuzz
 * unguarded -- a fuzzer WILL find "syscmd(...)" once it is anywhere in
 * the corpus, and running an attacker-mutated command line once per
 * trial, millions of times an hour, is not a risk to take on faith.
 * Checked directly against src/stdlib/system.c before running anything:
 * this library's system() is Windows-flavoured unconditionally -- it
 * resolves a shell via %ComSpec% or, failing that, `__find_program
 * ("cmd.exe", 1)`, with no /bin/sh fallback of any kind. On this
 * harness's native-Linux build, no file named cmd.exe exists anywhere
 * __find_program can reach, and fuzz/ntstubs.c's own start-up
 * deliberately resets `environ` to empty (see fuzz/Makefile's comment
 * on why: "a native test must not inherit the harness's real
 * environment"), so %ComSpec% is never set either. find_shell() in
 * system.c therefore always fails, system() always returns -1, and
 * src/util/m4.c's bi_syscmd() already handles that outcome the same as
 * any other command failure (errno-derived diagnostic, sysval 127, no
 * process ever spawned) -- confirmed by reading both call sites, not
 * inferred. A future harness that reaches this file on a build where a
 * real cmd.exe (or a repurposed system()) IS reachable would need to
 * revisit this.
 *
 * THE INCLUDE()/SINCLUDE() FIXTURE. Like fuzz_glob.c, this harness
 * seeds one small real file at a fixed path (/tmp/m4inc) before the
 * first call, containing a short line of harmless text -- so a fuzzer
 * input containing the literal text `include(/tmp/m4inc)` (plausible
 * once it appears anywhere in the corpus at all, since libFuzzer splices
 * substrings across corpus entries) exercises a real, successful
 * include() rather than only ever landing on include()'s "file not
 * found" diagnostic path. include()'d content, unlike top-level input,
 * is documented in src/util/m4.c's header as flowing through an
 * ordinary NUL-terminated C string once read, so the fixture's own
 * content is deliberately plain ASCII text with no embedded NUL of its
 * own -- that gap is m4's, not this fixture's, to demonstrate. An
 * `include()`/`sinclude()` of any OTHER path a fuzzer happens to guess
 * simply fails harmlessly (ENOENT against fuzz/ntstubs.c's simulated,
 * in-memory volume, which never exposes the real host filesystem to
 * begin with -- see that file's own header comment), which needs no
 * special handling either way.
 *
 * A SECOND PASS OVER M4's OWN OUTPUT, AS A CRASH/HANG/LEAK CHECK ONLY
 * -- NOT A FIXED-POINT ORACLE. fuzz_shparse.c's oracle is parse/print/
 * reparse/print reaching a fixed point, which holds because printing a
 * parsed program and reparsing that exact text is meant to be lossless.
 * Macro expansion has no analogous property: m4's output is prose, not
 * a serialization of its input, and feeding it back in is not expected
 * to reproduce anything -- text that happened to read as "dnl" or
 * "define(...)" in the expansion becomes live syntax on the second
 * pass, which is not a bug, just what m4 is. So this harness does NOT
 * compare pass 1's output against pass 2's, or even against itself:
 * it captures pass 1's real stdout (via the fd-level redirect below --
 * `stdout` itself is `FILE *const`, not reassignable, so this project's
 * own fmemopen()-onto-stdio trick is not available either) up to
 * SECONDPASS_CAP bytes and, if any came out, feeds that straight back
 * through __util_m4_main() as a second, independent trial. What this
 * buys is real: pass 1's output can legitimately contain quote and
 * comment delimiters, control-character builtin sentinels
 * (M4_BUILTIN_MAGIC, from defn() of a builtin -- see src/util/m4.c's
 * header section on defn()), and other shapes a fresh, from-scratch
 * fuzzer input is unlikely to stumble into on its own, so this is a
 * cheap way to reach scan()/read_quoted_into() states a corpus of raw
 * mutated bytes alone would take far longer to discover -- while never
 * asserting a property (equality, termination-in-fewer-steps, anything)
 * that macro expansion does not actually guarantee.
 *
 * SIZE CAPS. PROGRAM_CAP (768 bytes) bounds the file operand this
 * harness writes, independent of whatever -max_len the runner passes
 * (tools/fuzz.sh's own default is 256, well under this). It matters for
 * one specific hazard PROGRAM_CAP alone does not fully retire: real
 * C-stack recursion through dispatch_macro()->collect_args()->scan()
 * needs only 2 bytes per nesting level ("<builtin-name>(" for the
 * shortest builtins, e.g. "dnl(" is 4, "len(" is 4 -- there is no
 * 1-character builtin name), so 768 bytes bounds nesting to on the
 * order of a couple hundred levels even before M4_MAX_DEPTH (5000, see
 * src/util/m4.c) would ever be reached -- a couple hundred levels of a
 * few hundred bytes of stack each (scan()'s wbuf[256] dominates a
 * frame's cost) is nowhere near a default thread stack, so this size
 * cap is what actually keeps this class sized out of reach here, not
 * M4_MAX_DEPTH, which exists for the case an input far larger than any
 * -max_len this project runs with would otherwise reach. SECONDPASS_CAP
 * (2048 bytes) similarly bounds how much of pass 1's own output this
 * harness will re-feed as pass 2's input, since a small pass-1 program
 * can legitimately produce an output far larger than PROGRAM_CAP
 * (`define(x,\`aaaa...aaaa')x` and similar) and this harness has no
 * interest in growing its own per-trial cost unboundedly chasing that.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "../src/internal/util.h"

#define PROGRAM_CAP 768
#define SECONDPASS_CAP 2048

#define IN1_PATH  "/tmp/fuzz_m4_1.in"
#define OUT1_PATH "/tmp/fuzz_m4_1.out"
#define IN2_PATH  "/tmp/fuzz_m4_2.in"
#define OUT2_PATH "/tmp/fuzz_m4_2.out"
#define INCLUDE_PATH "/tmp/m4inc"

/* This process's real stdout fd, saved once so every redirect below can
 * be undone before returning -- libFuzzer's own reporting (run counts,
 * -print_final_stats) must see an ordinary stdout between trials, not
 * whatever this call last pointed fd 1 at. */
static int real_stdout_fd = -1;

static void fixture(void)
{
	static int done;
	int fd;

	if (done) return;
	done = 1;

	real_stdout_fd = dup(STDOUT_FILENO);

	/* See the header comment's INCLUDE()/SINCLUDE() section: plain
	 * ASCII, no embedded NUL of its own, so a NUL that shows up
	 * downstream of an include() of this file is m4's doing, not the
	 * fixture's. */
	fd = open(INCLUDE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		static const char content[] = "included m4 text\n";
		(void)write(fd, content, sizeof content - 1);
		close(fd);
	}
}

/* Point fd 1 at `path` (truncated, created if needed), or leave it
 * alone if that fails -- a harness that crashed because its OWN file
 * I/O failed would be reporting a defect in itself, not in m4. */
static void redirect_stdout_to(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	dup2(fd, STDOUT_FILENO);
	close(fd);
}

static void restore_stdout(void)
{
	if (real_stdout_fd >= 0) dup2(real_stdout_fd, STDOUT_FILENO);
}

/* Write `n` bytes to `path`, or return 0 (nothing further should be
 * attempted with this trial) if the write itself did not succeed. */
static int write_file(const char *path, const void *data, size_t n)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return 0;
	if (n && write(fd, data, n) != (ssize_t)n) { close(fd); return 0; }
	close(fd);
	return 1;
}

/* Read up to `cap` bytes of `path` back in, returning the count read
 * (0 on any failure -- absence, an empty file, and an error are all
 * "nothing to feed a second pass" alike here). */
static size_t read_file_capped(const char *path, char *buf, size_t cap)
{
	int fd = open(path, O_RDONLY);
	ssize_t r;
	if (fd < 0) return 0;
	r = read(fd, buf, cap);
	close(fd);
	return r > 0 ? (size_t)r : 0;
}

/* One m4 run against a file already written to `path`, with `extra_argv`
 * (NULL-terminated, or NULL for none) of -s/-D/-U options ahead of the
 * file operand -- see the header comment on why the leading fuzz byte
 * is spent selecting these rather than added to the program text. */
static void run_m4(const char *path, char *const *extra_argv, const char *outpath)
{
	char *argv[8];
	int argc = 0;

	argv[argc++] = (char *)"m4";
	if (extra_argv)
		while (*extra_argv && argc < 6) argv[argc++] = *extra_argv++;
	argv[argc++] = (char *)path;
	argv[argc] = NULL;

	redirect_stdout_to(outpath);
	(void)__util_m4_main(argc, argv);
	restore_stdout();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char program[PROGRAM_CAP];
	char second[SECONDPASS_CAP];
	size_t n, got2;
	unsigned mode;
	char *opts[4];	/* up to 3 flags (see below) plus a NULL terminator */
	int nopts = 0;

	fixture();

	if (size < 1) return 0;
	mode = data[0];
	data++; size--;

	n = size < sizeof program ? size : sizeof program;
	memcpy(program, data, n);

	/* -s/-D/-U option coverage: see the header comment.  Names/values
	 * are fixed, not fuzzed -- the interesting logic under test here is
	 * __util_m4_main()'s own option-parsing loop, not diversity of
	 * macro names, which the program text itself already fuzzes via
	 * ordinary define(). */
	if (mode & 1u) opts[nopts++] = (char *)"-s";
	if (mode & 2u) opts[nopts++] = (char *)"-Dfoo=bar";
	if (mode & 4u) opts[nopts++] = (char *)"-Udnl";
	opts[nopts] = NULL;

	if (!write_file(IN1_PATH, program, n)) return 0;

	run_m4(IN1_PATH, nopts ? opts : NULL, OUT1_PATH);

	/* The second pass: crash/hang/leak-freedom only, per the header
	 * comment -- no comparison against pass 1 is made or meaningful. */
	got2 = read_file_capped(OUT1_PATH, second, sizeof second);
	if (got2 && write_file(IN2_PATH, second, got2))
		run_m4(IN2_PATH, NULL, OUT2_PATH);

	return 0;
}
