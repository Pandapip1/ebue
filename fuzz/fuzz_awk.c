/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_awk_main()'s (src/util/awk.c) hand-written lexer,
 * recursive-descent parser, and tree-walking interpreter. The fuzz
 * buffer is the awk PROGRAM TEXT, written to a fixed file under /tmp and
 * driven via `-f` -- not the field data awk reads, which stays a small
 * fixed fixture (DATAPATH) so $0/$1../NF/FS splitting has real state to
 * act on without spending fuzzer bytes on it. `-f` (over a bare operand
 * or stdin) is needed because argv strings are NUL-terminated and
 * stdin/stdout/stderr are `FILE *const` in this libc, ruling out an
 * fmemopen()-into-stdin trick.
 *
 * awk is Turing-complete with no interpreter-level bound on loop
 * iteration (`while(1);` is ordinary, legal awk), so program_is_safe()
 * below parses the program itself first and refuses to let
 * __util_awk_main() actually RUN one containing an unbounded loop
 * (N_WHILE/N_DOWHILE/N_FOR -- N_FORIN is left runnable since nothing
 * without one of those three could have built a large enough array to
 * make it slow) or a way to spawn a child process (system(), `cmd |
 * getline`, `print | cmd`) -- the same subprocess concern fuzz_shparse.c
 * gives for never reaching src/sh/execute.c. This only skips
 * *execution*: the parser itself, including every loop-statement
 * production, still runs on every input regardless of program_is_safe()'s
 * verdict, since the parser is the higher-value target here. Recursion
 * needed no such filter -- call_user_func() already caps depth at 1000.
 *
 * scan_unsafe() walks struct awk_node's generic a/b/c/d/list[nlist]
 * child fields (awk_priv.h) rather than a per-node-type switch, so a
 * future grammar addition is covered automatically without a second,
 * divergence-prone model of the grammar.
 *
 * src/util/awk.c deliberately never frees a parsed AST (a short CLI
 * process needs it to exit anyway) -- correct for a real caller, but
 * this harness calls __util_awk_main() and awk_parse_program() up to
 * hundreds of thousands of times in one process, so that same
 * allocation would read as a LeakSanitizer leak almost immediately.
 * __lsan_disable()/__lsan_enable() bracket only those two calls; real
 * memory-safety bugs elsewhere in the same window stay fully ASan/LSan
 * visible, only the leak *count* for this known-permanent AST is
 * suppressed.
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

	/* Parsed here just to check program_is_safe(); __util_awk_main()
	 * below reparses the same text from PROGPATH on its own. */
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
	/* parsed == NULL still runs __util_awk_main(), which reparses and
	 * fails the same way, exercising the syntax-error diagnostic path. */
	return 0;
}
