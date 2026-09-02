/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_awk_main() -- src/util/awk.c, the biggest of the Tier-4 "bigger
 * engines" utilities: a real pattern-action language with its own hand-
 * written lexer (src/util/awk_lex.c), recursive-descent parser
 * (src/util/awk_parse.c, ~40 mutually-recursive parse_*() functions,
 * one per XCU awk(1p) grammar production) and tree-walking interpreter
 * (src/util/awk_run.c). Exactly the shape fuzz_shparse.c's and
 * fuzz_regex.c's own headers argue is this project's highest-value fuzz
 * target: a hand-written scanner/parser over untrusted input, this time
 * with a second hand-written engine (the interpreter) sitting behind it.
 *
 * THE FUZZ INPUT IS THE AWK PROGRAM TEXT, not field data. awk(1p)'s own
 * grammar offers two ways to get a program into awk: as the first
 * non-option operand, or via one-or-more `-f progfile`. A one-shot
 * operand can't carry embedded NULs or arbitrary bytes as cleanly as a
 * file can (argv strings are NUL-terminated C strings; a fuzz buffer
 * with an embedded NUL would just get silently truncated at the first
 * one, quietly narrowing what gets tested), and this project's `stdin`/
 * `stdout`/`stderr` are `extern FILE *const` -- const pointers, per
 * include/stdio.h -- so unlike a POSIX libc harness there is no
 * fmemopen()-into-stdin trick available for feeding awk source through
 * the "read program from stdin via -f -" path either. So this harness
 * writes the fuzz buffer to a real file at a fixed path under /tmp (the
 * same approach fuzz_glob.c uses for its own fixture, and the reason it
 * works at all: fuzz/ntstubs.c's simulated volume has /tmp available
 * from process start, per fuzz_path.c's own comment) and drives awk
 * with `-f <that file>`, so every byte the fuzzer chose reaches
 * awk_lex.c completely unmodified -- this is deliberately a fuzzer for
 * the awk LANGUAGE, not for field-splitting/record-reading, which would
 * need a second, independent fuzz input (program text and field data
 * are two unrelated grammars) and is not this harness's job.
 *
 * A second, tiny, FIXED data file is still supplied as awk's one `file`
 * operand (see DATAPATH below) rather than leaving input unset: with no
 * data and no operand, a program with a non-BEGIN/END rule would read
 * from the real stdin instead, whose behavior in the harness process is
 * not something this file wants to depend on. A handful of short,
 * regular lines is enough to drive $0/$1../NF/NR/FNR and FS's ordinary
 * (single-space) splitting through real, non-empty state on every run
 * that reaches the main input loop, without the fuzzer needing to spend
 * any of its own bytes constructing field data just to get there.
 *
 * WHAT IS DELIBERATELY NOT RUN, AND WHY (the fuzz_regex.c-style
 * safe_to_exec() problem, awk's version of it):
 *
 * awk is a real Turing-complete language with its own `while`/`do`/
 * `for` loop constructs, and, unlike src/regex/regex.c's regexec()
 * (which carries an unconditional, universal MAX_STEPS budget -- see
 * that file's own header -- so an adversarial PATTERN can never run
 * forever, only slowly), awk_run.c's interpreter has no such bound on
 * loop iteration: `awk 'BEGIN{while(1);}'` is completely ordinary,
 * intentional, XCU-legal awk and loops forever by design, the same way
 * a real user's infinite-loop typo would. Baking an arbitrary iteration
 * cap into the shared interpreter to accommodate this one harness would
 * change awk's actual behavior for every real caller (a legitimate
 * `for (i=0;i<1e9;i++)` numeric loop is unremarkable), which is a
 * production-semantics decision this harness has no business making
 * unilaterally -- so unlike regexec(), the fix does not belong in
 * src/util/awk_run.c.
 *
 * Nor does a per-input wall-clock watchdog belong in THIS file:
 * ../tools/fuzz.sh's own long comment on `watchdog=` explains, measured
 * rather than assumed, why a SIGALRM-based per-unit timeout does not
 * work in this project at all -- ntlibc's own sigaction() is a strong
 * symbol in every harness binary, so libFuzzer's (or a hand-rolled
 * alarm()) handler registers in ntlibc's table while the real itimer
 * fires with the default disposition and simply kills the process, with
 * no artifact and no diagnostic. That is precisely why every harness in
 * this tree already runs with -timeout=0 and relies on fuzz.sh's own
 * external `timeout` wrapper around the whole campaign instead: a
 * per-input signal timer built into this file would not reliably fire
 * any more than libFuzzer's own does.
 *
 * So the fix here is a HARNESS-SIDE static filter, in the same spirit
 * as fuzz_regex.c's safe_to_exec() and with the same tradeoff spelled
 * out by that file's own header: conservative in the safe direction,
 * over-excluding rather than risking a false "safe". program_is_safe()
 * below walks the REAL parsed AST (awk_parse_program() is called here
 * directly, before __util_awk_main() parses its own private copy of the
 * same text) and refuses to let __util_awk_main() actually RUN a
 * program containing any of:
 *
 *   - N_WHILE, N_DOWHILE, N_FOR -- an unbounded loop construct. (N_FORIN
 *     is deliberately left runnable: without one of the three loop
 *     kinds just excluded, nothing in the same program can have built an
 *     array large enough for a `for (k in arr)` to itself take long, so
 *     excluding those three transitively bounds this one too.)
 *   - N_CALL to the builtin "system", N_GETLINE with gl_src==GL_CMD
 *     (`cmd | getline`), and N_PRINT/N_PRINTF with redir==RD_PIPE
 *     (`print ... | cmd`) -- every way XCU awk(1p) can make awk spawn a
 *     real child process. fuzz_shparse.c's own header gives the same
 *     reasoning for never reaching src/sh/execute.c: a fuzzer's mutated
 *     text becoming a real command line and actually forking is not a
 *     property-of-the-parser test any more, it is a property-of-the-
 *     host-shell test, and an unbounded one at that (nothing stops the
 *     spawned command from itself hanging).
 *
 * This costs real coverage -- awk_run.c's N_WHILE/N_FOR/N_DOWHILE
 * execution arms, and the three subprocess-spawning paths, are only
 * reached by a program this filter lets through, i.e. one with none of
 * those constructs anywhere in it -- but the parser itself parses EVERY
 * input regardless of what program_is_safe() decides, loop constructs
 * included: awk_parse.c's parse_while_stmt()/parse_for_stmt()/
 * parse_do_stmt() and every production layered under them are exercised
 * on every single run, safe or not. Only the interpreter's execution of
 * an unsafe program is skipped. That split matches this project's own
 * stated priority for this exact utility (see the batch instructions
 * this harness was written from): the hand-written recursive-descent
 * parser is the highest-value target, and it is never the part being
 * skipped here.
 *
 * (Recursion is a different story and needed no filter: awk_run.c's
 * call_user_func() already refuses a call depth past 1000 -- `function
 * f(){f()}` cannot loop forever even when let through unfiltered, and
 * is exactly the kind of thing this harness wants exercised.)
 *
 * A NOTE ON WHAT THIS HARNESS DOES *NOT* CLAIM: program_is_safe()'s AST
 * walk is generic over struct awk_node's own generic child fields
 * (a/b/c/d/list[nlist], per awk_priv.h's own comment on them) rather
 * than a per-node-type switch that visits each child by name, so a
 * future grammar addition that hangs a new child off one of those
 * fields is covered automatically; the risk this shape accepts instead
 * is exactly the inverse of fuzz_regex.c's bracket-expression lesson
 * recorded in that file's own header (a filter that mis-models the
 * language it filters admits inputs it should not) -- here the model is
 * "every child lives in a, b, c, d, or list", which is what awk_priv.h
 * itself documents rather than a re-derived guess at the grammar, so
 * the failure mode that bit fuzz_regex.c does not have the same
 * opening here.
 *
 * INTENTIONAL, LEAK-SANITIZER-VISIBLE ALLOCATIONS, SUPPRESSED ON
 * PURPOSE: src/util/awk.c's own header says outright that a parsed
 * program's AST (and, for -f, the loaded program-file buffer) is
 * "deliberately never freed... this is a short-lived CLI process (or,
 * as a shell built-in, one bi_awk() invocation), so the OS reclaims it
 * at exit either way". That is a correct, deliberate design choice for
 * every real caller -- and the wrong one for THIS process, which calls
 * __util_awk_main() (and, for the safety scan above, awk_parse_program()
 * a second time) up to hundreds of thousands of times without ever
 * exiting, so the same never-freed allocation that costs a real
 * invocation nothing would otherwise accumulate across the whole
 * campaign and LeakSanitizer would report it as a leak on
 * essentially the first run -- a real signal about this harness's
 * process model, not a defect in awk's. __lsan_disable()/__lsan_enable()
 * (declared directly below, rather than pulling in
 * <sanitizer/lsan_interface.h> under this tree's -nostdinc harness
 * build) bracket exactly the two calls that own such memory, which is
 * LeakSanitizer's own documented use for a knowingly-permanent
 * allocation: memory-safety bugs in the surrounding code (a
 * use-after-free, an overflow while building the AST, a double free in
 * awk_interp_free()) are still fully live and ASan-visible in that same
 * window -- only the leak *count* is suppressed, and only for these two
 * calls.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"
#include "../src/util/awk_priv.h"

extern void __lsan_disable(void);
extern void __lsan_enable(void);

#define PROGPATH "/tmp/fuzz_awk.prog"
#define DATAPATH "/tmp/fuzz_awk.data"
#define CAP 4096

/* A handful of short, regular records -- see this file's header for why
 * a fixed data file is supplied at all rather than leaving input to a
 * real stdin read. Written once; every run reads the same bytes back,
 * so a reproducer actually reproduces. */
static void fixture(void)
{
	static int done;
	int fd;

	if (done) return;
	done = 1;
	fd = open(DATAPATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		static const char data[] = "hello world\nfoo bar baz\n1 2 3\n";
		(void)write(fd, data, sizeof data - 1);
		close(fd);
	}
}

/* See this file's header ("WHAT IS DELIBERATELY NOT RUN"). Generic over
 * struct awk_node's own generic child fields, not a per-type switch. */
static int scan_unsafe(const struct awk_node *n)
{
	int i;

	if (!n) return 0;
	switch (n->type) {
	case N_WHILE: case N_DOWHILE: case N_FOR:
		return 1;
	case N_GETLINE:
		if (n->gl_src == GL_CMD) return 1;
		break;
	case N_CALL:
		if (n->str && !strcmp(n->str, "system")) return 1;
		break;
	case N_PRINT: case N_PRINTF:
		if (n->redir == RD_PIPE) return 1;
		break;
	default:
		break;
	}
	if (scan_unsafe(n->a) || scan_unsafe(n->b) || scan_unsafe(n->c) || scan_unsafe(n->d))
		return 1;
	for (i = 0; i < n->nlist; i++)
		if (scan_unsafe(n->list[i])) return 1;
	return 0;
}

static int program_is_safe(const struct awk_program *prog)
{
	int i;

	for (i = 0; i < prog->nrules; i++) {
		if (scan_unsafe(prog->rules[i].pat1)) return 0;
		if (scan_unsafe(prog->rules[i].pat2)) return 0;
		if (scan_unsafe(prog->rules[i].action)) return 0;
	}
	for (i = 0; i < prog->nfuncs; i++)
		if (scan_unsafe(prog->funcs[i].body)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char prog[CAP + 1];
	size_t n = size < CAP ? size : CAP;
	int fd;
	struct awk_program *parsed;
	char *argv[5];

	if (!n) return 0;
	memcpy(prog, data, n);
	prog[n] = 0;
	if (memchr(prog, 0, n)) return 0;        /* embedded NUL: not one program */

	fixture();

	fd = open(PROGPATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return 0;
	(void)write(fd, prog, n);
	close(fd);

	/* This file's own parse, purely to decide whether it is safe to let
	 * __util_awk_main() below actually RUN it -- see "WHAT IS
	 * DELIBERATELY NOT RUN" above. __util_awk_main() parses the same
	 * text again from PROGPATH itself; the double parse is the price of
	 * inspecting the tree before deciding, and is harmless -- both are
	 * "never freed" by design (see "INTENTIONAL, LEAK-SANITIZER-VISIBLE
	 * ALLOCATIONS" above), so nothing here mutates shared state twice. */
	__lsan_disable();
	parsed = awk_parse_program(prog);
	__lsan_enable();

	argv[0] = (char *)"awk";
	argv[1] = (char *)"-f";
	argv[2] = (char *)PROGPATH;
	argv[3] = (char *)DATAPATH;
	argv[4] = NULL;

	if (!parsed || program_is_safe(parsed)) {
		__lsan_disable();
		(void)__util_awk_main(4, argv);
		__lsan_enable();
	}
	/* A parse failure (parsed == NULL) still runs __util_awk_main(),
	 * which re-parses PROGPATH itself and fails the same way -- driving
	 * the "awk: syntax error..." diagnostic path and confirming
	 * __util_awk_main() returns a real nonzero status rather than
	 * exit()ing (see src/internal/util.h's and src/util/awk.c's own
	 * headers on that contract, and this file's header for the "A FATAL
	 * RUNTIME CONDITION" discipline this harness exists to keep
	 * honest). */
	return 0;
}
