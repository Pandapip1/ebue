/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __sh_parse() -- src/sh/parse.c, the shell's lexer and recursive-descent
 * parser, with src/sh/print.c's canonical reprinter and src/sh/free.c's
 * AST teardown alongside it.  Roughly 900 lines that turn a whole
 * program text into an AST, entirely in memory: quote state across four
 * kinds of quoting, balanced "$(...)" and "${...}", backquotes,
 * here-documents drained at the newline, IO numbers, pipelines, and-or
 * lists, subshells and brace groups.  src/sh/exec.c is deliberately not
 * reached from here -- executing a fuzzer's program would fork.
 *
 * The parser is the single largest untested-against-hostile-input
 * surface in the tree that needs no OS objects at all, and every scanner
 * in it (copy_squoted, copy_dquoted, copy_balanced, copy_backquoted,
 * scan_word, drain_heredocs, strip_delim) advances a pointer through the
 * caller's buffer looking for a terminator that a fuzzer will simply not
 * provide.
 *
 * THE ORACLE IS THE PRINTER, AND IT IS A REAL ONE.  src/sh/print.c's
 * banner states the property test/sh-engine.c already checks by hand for
 * a fixed set of programs: parse -> print -> parse -> print must reach a
 * fixed point, i.e. the second print equals the first.  That is a strong
 * invariant for a parser, and it needs no reference implementation:
 *
 *   - if the printer loses a field, the reprint of the reparse differs;
 *   - if the parser reads its own canonical output differently from the
 *     way it produced it, the two prints differ;
 *   - if either side has an off-by-one in a quote or here-document
 *     boundary, the text drifts on the second pass.
 *
 * This harness generalises that hand-written check to arbitrary input,
 * which is exactly what a fuzzer is for.  The one thing it must not do
 * is assume the reprint parses at all -- print.c's own comment says
 * printing is best-effort under allocation failure -- so a NULL second
 * parse is tolerated and only a *differing* second print is reported.
 *
 * open_memstream() (src/stdio/mem.c) collects the output.  Its own
 * buffer growth is therefore under test here too, incidentally: a
 * program whose reprint is long exercises the memstream's realloc path
 * with a length nothing else in fuzz/ drives.
 *
 * sh.h is included by relative path.  It lives in src/sh/, which is not
 * on the harness include path (-I src/internal is), and adding one would
 * be a change to fuzz/Makefile's shared build rules for a single file's
 * benefit.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../src/sh/sh.h"

extern void oracle_mismatch_s(const char *, const char *, const char *, const char *);
extern void oracle_mismatch_i(const char *, const char *, long long, long long);

/* LeakSanitizer's own interface, declared rather than included:
 * <sanitizer/lsan_interface.h> is a host header and this file is
 * compiled -nostdinc.  Both are exported by the shared ASan runtime
 * the harness already links.  See the block above heredoc_fence(). */
void __lsan_disable(void);
void __lsan_enable(void);

#define CAP 1024

/* BUG: a here-document redirection that is queued and then never
 * drained leaks its queue entry.  parse_redir() (src/sh/parse.c:524)
 * pushes a `struct pending_hd` onto the lexer's pending list before
 * the advance() that would reach the newline, and drain_heredocs() --
 * the only thing that frees those entries -- runs only when that
 * newline arrives.  If the parse fails first, __sh_parse()'s error
 * path frees the AST and returns, and the pending list is dropped on
 * the floor: p.lx.pending_head is initialised at line 725 and never
 * torn down.  24 bytes per queued here-document.
 *
 * Minimal reproducer, found by this harness in three minutes and
 * reduced by hand:
 *
 *     __sh_parse("a <<E (", ...)     ->  NULL, 24 bytes leaked
 *
 * ("a <<E" alone does not leak: it parses, so the queue drains.)
 * Fenced in test/sh-engine.c, not fixed, per the standing rule.
 *
 * The harness has to stay off it, for the reason fuzz_regex.c gives at
 * length: libFuzzer stops at the first finding, so a harness that
 * rediscovers a known defect on input 300 reports nothing else ever
 * again.  The narrowest possible exclusion is used -- LeakSanitizer is
 * switched off around the parse itself, and only for a source text
 * that actually contains "<<".  Every other allocation this harness
 * makes, and every allocation any input without a here-document makes,
 * is still accounted for.
 *
 * When the fence is lifted, delete heredoc_fence() and both calls. */
static int heredoc_fence(const char *src)
{
	return strstr(src, "<<") != 0;
}

/* BUG: the IO-number lexer overflows a signed int.  src/sh/parse.c:433
 * turns an all-digit word that is followed by '<' or '>' into an
 * SH_R_* redirection's fd with
 *
 *     for (i = 0; i < len; i++) v = v * 10 + (w[i] - '0');
 *
 * and `v` is an `int` with no bound on `len` and no check before the
 * multiply.  "877777777777777<x" makes that signed overflow, which is
 * undefined behaviour -- UBSan reports it as
 * "signed integer overflow: 877777777 * 10 cannot be represented in
 * type 'int'" -- and on a build where it does not trap it yields an
 * arbitrary, possibly negative, file descriptor number for the
 * redirection.  Found by this harness in a sixty-second run.
 *
 * Fenced in test/sh-engine.c, not fixed, per the standing rule.  The
 * filter below is an over-approximation on purpose: it rejects any
 * source with ten or more consecutive digits, which is a superset of
 * the inputs that can overflow (INT_MAX is ten digits) and is cheap
 * and obviously correct, where "does this particular digit run exceed
 * INT_MAX" would be the harness reimplementing the thing under test.
 * Nine-digit IO numbers, and every other lexer path, stay reachable.
 *
 * When the fence is lifted, delete ionum_fence() and its caller. */
static int ionum_fence(const char *src)
{
	int run = 0;
	size_t i;

	for (i = 0; src[i]; i++) {
		if (src[i] >= '0' && src[i] <= '9') {
			if (++run >= 10) return 1;
		} else {
			run = 0;
		}
	}
	return 0;
}

/* Reprint `l` into a fresh heap string, or NULL if the memstream could
 * not be created.  The caller frees. */
static char *reprint(const struct sh_list *l)
{
	char *buf = 0;
	size_t n = 0;
	FILE *f = open_memstream(&buf, &n);

	if (!f) return 0;
	__sh_print_list(f, l);
	if (fclose(f) != 0) { free(buf); return 0; }
	return buf;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char src[CAP + 1];
	char errbuf[256];
	struct sh_list *l1, *l2;
	char *p1, *p2;
	size_t n;
	int fenced;

	n = size < CAP ? size : CAP;
	if (!n) return 0;
	memcpy(src, data, n);
	src[n] = 0;
	if (memchr(src, 0, n)) return 0;        /* embedded NUL: not one program */

	/* errbuf is filled with a sentinel so the "wrote a diagnostic that
	 * is not NUL-terminated" case is visible: __sh_parse documents that
	 * it truncates to fit, and a truncation that forgets the NUL is the
	 * classic form of that bug. */
	if (ionum_fence(src)) return 0;

	memset(errbuf, 'Z', sizeof errbuf);
	fenced = heredoc_fence(src);
	if (fenced) __lsan_disable();
	l1 = __sh_parse(src, errbuf, sizeof errbuf);

	if (!l1) {
		if (memchr(errbuf, 0, sizeof errbuf) == NULL)
			oracle_mismatch_i("__sh_parse left errbuf unterminated", src,
			                  (long long)sizeof errbuf, 0);
		/* A NULL errbuf must be accepted, not dereferenced. */
		l1 = __sh_parse(src, NULL, 0);
		if (fenced) __lsan_enable();
		if (l1) {
			/* Two identical calls disagreeing about whether the program
			 * parses is a defect on its own -- the parser has no state
			 * that could legitimately differ between them. */
			oracle_mismatch_i("__sh_parse succeeded only when errbuf was NULL", src, 1, 0);
			__sh_list_free(l1);
		}
		return 0;
	}

	if (fenced) __lsan_enable();
	p1 = reprint(l1);
	__sh_list_free(l1);
	if (!p1) return 0;

	/* The fixed point, per src/sh/print.c's banner and the by-hand
	 * version of this check in test/sh-engine.c. */
	if (fenced) __lsan_disable();
	l2 = __sh_parse(p1, NULL, 0);
	if (fenced) __lsan_enable();
	if (l2) {
		p2 = reprint(l2);
		__sh_list_free(l2);
		if (p2) {
			if (strcmp(p1, p2) != 0)
				oracle_mismatch_s("parse/print is not a fixed point", src, p2, p1);
			free(p2);
		}
	}
	free(p1);
	return 0;
}
