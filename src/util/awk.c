/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * awk(1p) -- XCU: `awk [-F sepstring] [-v assignment]... program
 * [argument...]` or `awk [-F sepstring] -f progfile [-f progfile]...
 * [-v assignment]... [argument...]`. The whole of __util_awk_main()
 * below is just command-line parsing and wiring; the real work is
 * three sibling translation units, each with its own header comment
 * for the part it owns:
 *
 *  - src/util/awk_priv.h: the shared token/AST/cell/interpreter types
 *    every one of the four files below this comment uses.
 *  - src/util/awk_lex.c: the lexer.
 *  - src/util/awk_parse.c: the recursive-descent parser, one function
 *    per grammar production, precedence chain documented in its own
 *    header.
 *  - src/util/awk_run.c: the tree-walking interpreter -- value/cell
 *    model, field splitting, comparisons, the built-in function table,
 *    getline, and the BEGIN/main-loop/END driver.
 *
 * This file's own job is: parse -F/-v/-f/--, assemble the program
 * text, parse it, build the interpreter, seed ENVIRON/ARGV/ARGC and
 * any -v assignments, run it, and translate its own EXIT STATUS
 * section ("If a syntax error occurs while awk is compiling the
 * program... awk shall write a diagnostic message to standard error
 * and shall exit with a non-zero exit status ... Otherwise, ... the
 * exit status ... shall be the value passed to ... exit(); otherwise
 * ... 0") into the process's own exit code.
 *
 * ---- OPTIONS -----------------------------------------------------------
 *
 *  -F sepstring   Sets FS before BEGIN runs (XCU: "shall be used to
 *                 set the value of FS to be sepstring", so this
 *                 simply seeds a global exactly like a -v assignment
 *                 -- and, per "If any of the characters ... are the
 *                 same as ... the value used for FS", the ordinary
 *                 single-space/single-char/ERE FS classification
 *                 (awk_run.c's split_into()) applies to it identically
 *                 to any other FS value).
 *  -v assignment  `name=value`, applied before BEGIN runs, in
 *                 left-to-right command-line order for repeats (last
 *                 one for a given name wins) -- XCU is silent on
 *                 -F/-v ordering relative to each other when both name
 *                 the same variable; this implementation processes
 *                 every option strictly in argv order, so whichever of
 *                 `-F` or `-v FS=...` appears later on the command
 *                 line wins. A deliberate, documented choice among
 *                 conforming ones, not a hidden default.
 *  -f progfile    May repeat; each file's text is concatenated (with a
 *                 newline inserted between files, in case one does not
 *                 end in one) to form the whole program, in the order
 *                 given -- XCU: "the concatenation of the contents of
 *                 each of the progfile operands". When at least one
 *                 -f is given, the first operand is NOT the program
 *                 text (it is instead the first `argument`).
 *  --             Ends option parsing explicitly.
 *
 * OPERANDS: with no -f, the first non-option operand is the program
 * text. Every operand after that (or after the last -f/-v when -f was
 * used) is a `file` or a `var=value` assignment, and -- this is the
 * specific trap the batch instructions called out -- a var=value
 * operand takes effect exactly when the main input loop *reaches* it
 * in ARGV order, not all at once before input processing starts:
 * `awk '{print x}' file1 x=5 file2` prints an empty x for every line
 * of file1 and "5" for every line of file2. This is implemented
 * naturally, not as a special case: awk_run.c's advance_to_next_argv_
 * file() recognizes and applies a var=value ARGV element the moment it
 * is reached while walking ARGV for the next input file, exactly the
 * same way real input processing consumes ARGV one element at a time.
 * -v assignments are different on purpose -- they are seeded here,
 * before BEGIN, so BEGIN can see them, matching XCU's "-v assignment
 * ... shall be used to set the value of a variable, or array element,
 * before ... BEGIN action(s) ... are executed."
 *
 * ---- DELIBERATE SCOPE NARROWINGS (recorded here, the same way
 * src/util/dd.c documents its conv= coverage and src/util/df.c its
 * "no operands" case) -----------------------------------------------
 *
 *  - Numeric literals are decimal only; no hex float constants. XCU's
 *    own NUMBER token grammar is decimal-only anyway (a hex constant
 *    in awk source is a non-portable extension some implementations
 *    add) -- see awk_lex.c's header for exactly what a "0x1" literal
 *    lexes as instead (NUMBER 0 concatenated with NAME "x1").
 *  - Empty-string FS ("split into characters") is a common extension
 *    XCU does not itself define; this implementation's own reading
 *    (awk_run.c's split_into()) is "no separator ever occurs", i.e.
 *    the whole string is field 1 -- a real, working answer, just not
 *    the gawk-compatible one, and recorded here rather than left to
 *    be discovered by surprise.
 *  - RS's value beyond its very first character is never consulted
 *    (a multi-character or ERE RS is a gawk extension XCU does not
 *    define); XCU's own text is exactly "the first character of the
 *    value of RS should be used".
 *  - RS=="" (paragraph mode)'s "the <newline> character shall always
 *    be a field separator, no matter what value FS has" is implemented
 *    for FS==" " (already true -- blank includes newline) and for a
 *    single-character FS (unioned directly into the split), but NOT
 *    additionally unioned into a multi-character (ERE) FS -- an
 *    already-rare combination (paragraph mode *and* a regex FS)
 *    narrowed here rather than left silently half-right.
 *  - `nextfile` (skip to the next ARGV file without running END) is a
 *    real, widely-implemented extension this project's own "POSIX-
 *    mandatory only" scope excludes deliberately: it has no XCU
 *    awk(1p) citation at all.
 *  - `fflush()` as a callable built-in is likewise not implemented:
 *    it is a gawk/BWK extension, not one of XCU awk(1p)'s own mandatory
 *    functions. Every stream this implementation itself opens is still
 *    flushed at the right moments internally (before system()/a
 *    `| getline` pipe command runs, and at normal program exit) without
 *    a user-callable hook.
 *  - printf/sprintf's conversions carry no C length modifiers (h/hh/l/
 *    ll/L) -- meaningless here since every awk value is already a
 *    double or a string, never a typed vararg the way C's own printf()
 *    varargs are; the same narrowing src/util/util_printf.c's own
 *    header documents for printf(1p)'s conversions.
 *  - substr()'s m<=0 / m+n past the string's end behavior is XCU's own
 *    "the effect ... is unspecified" case; this implements the
 *    conventional clamping algorithm every real awk uses (a half-open
 *    [m, m+n) window over 1-based positions, clipped to what actually
 *    overlaps the string) -- see awk_run.c's own comment on it.
 *  - `for (k in arr)` iteration order is XCU's own "unspecified" --
 *    this implementation's order is whatever its hash table's bucket
 *    layout produces (awk_priv.h's struct awk_htab comment).
 *  - A user-defined function's array-vs-scalar parameter binding uses
 *    a dynamic (not static-analysis) rule: a bare-identifier argument
 *    whose current cell is either already an array or still completely
 *    uninitialized is bound *by reference* (aliased into the callee's
 *    frame directly); the first time that shared cell is used as a
 *    scalar inside the callee, the binding is silently forked into a
 *    private copy first (so the write never reaches the caller's
 *    variable) -- see awk_priv.h's struct awk_cell comment and
 *    awk_run.c's call_user_func() for the full mechanism. This gets
 *    the two well-known rules right (scalars always by value, arrays
 *    always by reference, and an uninitialized argument that the
 *    callee treats as an array becomes a real array in the caller's
 *    scope too) including through nested/chained calls, without a
 *    separate whole-program static analysis pass.
 *  - next/exit executed from inside a user-defined function (rather
 *    than directly in a rule's own action) unwind correctly to the
 *    nearest enclosing record/program boundary via a small persistent
 *    interpreter flag rather than setjmp/longjmp -- see awk_run.c's
 *    header for the mechanism and its one acknowledged rough edge
 *    (a next/exit whose effect would need to be observed mid-
 *    expression, e.g. as one of several arguments to a single print
 *    statement, instead lets any *later* argument of that same
 *    statement still evaluate before the statement bails).
 *  - Allocation failure anywhere in the parser or interpreter is
 *    treated as fatal (a diagnostic plus an unwind back to here --
 *    NOT a raw exit(2): see this file's own __util_awk_main() and
 *    awk_priv.h's "fatal-error unwind" header comment for why bi_awk()
 *    running as a no-fork shell built-in makes that distinction load-
 *    bearing) rather than threaded back through every one of this
 *    utility's many mutually-recursive functions as a real error
 *    return -- see awk_parse.c's and awk_run.c's own headers.  The
 *    same unwind now also catches every OTHER fatal runtime condition
 *    awk_run.c's fatal()/oom() cover (division by zero, a scalar/array
 *    type clash, an undefined function call, an invalid dynamic ERE, a
 *    failed output redirect open) -- none of them exit()s the process
 *    either, for the same reason.
 *
 * tolower()/toupper() ARE implemented -- they are XCU awk(1p)'s own
 * mandatory string functions, not an extension, despite the task
 * brief's built-in list not naming them explicitly (that list was
 * itself a floor, not a ceiling).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include "awk_priv.h"
#include "util.h"

struct vassign { char *name, *val; };

/* The one definition of the fatal-error unwind target awk_priv.h
 * declares extern -- see that header's own long comment for the full
 * design. Defined here because __util_awk_main() below is the only
 * function that ever calls setjmp() on it. */
jmp_buf awk_fatal_env;
static int awk_fatal_armed;

void awk_unwind_fatal(void)
{
	if (awk_fatal_armed) longjmp(awk_fatal_env, 1);
	/* No __util_awk_main() call is on the stack to catch this (e.g. a
	 * direct awk_parse_program() call, the way fuzz/fuzz_awk.c's own
	 * harness makes one to pre-check a program before deciding whether
	 * to run it) -- see awk_priv.h's own comment on awk_fatal_armed.
	 * Falling back to the historical diagnostic-plus-exit(2) behavior
	 * is still correct for that caller; only __util_awk_main() itself
	 * needs (and gets) the non-exiting path. */
	exit(2);
}

static void buf_grow_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n)
{
	if (*len + n + 1 > *cap) {
		size_t newcap = *cap ? *cap * 2 : 256;
		while (newcap < *len + n + 1) newcap *= 2;
		*buf = realloc(*buf, newcap);
		if (!*buf) { __util_diagf("awk: out of memory\n"); awk_unwind_fatal(); }
		*cap = newcap;
	}
	for (size_t i = 0; i < n; i++) (*buf)[*len + i] = s[i];
	*len += n;
	(*buf)[*len] = 0;
}

static char *load_progfiles(char **files, int nfiles)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;
	int i;

	for (i = 0; i < nfiles; i++) {
		FILE *f = strcmp(files[i], "-") == 0 ? stdin : fopen(files[i], "r");
		char chunk[4096];
		size_t n;
		if (!f) {
			__util_diagf("awk: %s: %s\n", files[i], strerror(errno));
			free(buf);
			return NULL;
		}
		while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
			buf_grow_append(&buf, &len, &cap, chunk, n);
		if (f != stdin) fclose(f);
		if (len == 0 || buf[len - 1] != '\n') buf_grow_append(&buf, &len, &cap, "\n", 1);
	}
	if (!buf) buf = strdup("");
	return buf;
}

/* `name=value`: name must look like a real awk identifier (letter/'_'
 * then alnum/'_') for the whole thing to be an assignment at all --
 * XCU's own grammar for both -v's operand and a var=value file
 * operand. Splits in place (writes a NUL over the '=') and returns 1
 * on success. */
static int split_assignment(char *s, char **name_out, char **val_out)
{
	char *eq = strchr(s, '=');
	char *p;
	if (!eq || eq == s) return 0;
	if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
	for (p = s + 1; p < eq; p++) if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
	*eq = 0;
	*name_out = s;
	*val_out = eq + 1;
	return 1;
}

/* -F/-v/-f all share the standard getopt(3)-style rule that an option's
 * argument is either attached (`-Fx`) or, when nothing is attached, the
 * next argv element (`-F x`). Returns that argument text, advancing
 * *argi past it in the "next argv element" case; on the missing-
 * argument case prints the diagnostic itself and returns NULL, which
 * the caller must treat as "return 2" (this function cannot do that
 * unwind itself: it runs before __util_awk_main()'s own setjmp() is
 * armed, so a plain return here is already exactly what every other
 * error in this same option loop does). */
static const char *opt_value(char **argv, int argc, int *argi, char opt, const char *arg)
{
	if (arg[2]) return arg + 2;
	if (++*argi >= argc) {
		__util_diagf("awk: -%c: option requires an argument\n", opt);
		return NULL;
	}
	return argv[*argi];
}

int __util_awk_main(int argc, char **argv)
{
	const char *fsarg = NULL;
	struct vassign *vassigns = NULL;
	int nvassigns = 0;
	char **progfiles = NULL;
	int nprogfiles = 0;
	int have_f = 0;
	int i;
	char *progtext;
	struct awk_program *prog;
	struct awk_interp ip;
	int status;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		const char *val;

		if (!strcmp(arg, "--")) { i++; break; }
		if (arg[0] != '-' || arg[1] == 0) break;

		switch (arg[1]) {
		case 'F':
			val = opt_value(argv, argc, &i, 'F', arg);
			if (!val) return 2;
			fsarg = val;
			break;
		case 'v': {
			char *name, *v2;
			char *dup;
			val = opt_value(argv, argc, &i, 'v', arg);
			if (!val) return 2;
			dup = strdup(val);
			if (!dup) { __util_diagf("awk: out of memory\n"); return 2; }
			if (!split_assignment(dup, &name, &v2)) {
				__util_diagf("awk: -v: %s: not a valid name=value assignment\n", val);
				free(dup);
				return 2;
			}
			{
				struct vassign *g = realloc(vassigns, (size_t)(nvassigns + 1) * sizeof *g);
				if (!g) { __util_diagf("awk: out of memory\n"); return 2; }
				vassigns = g;
				vassigns[nvassigns].name = name;
				vassigns[nvassigns].val = v2;
				nvassigns++;
			}
			break;
		}
		case 'f':
			val = opt_value(argv, argc, &i, 'f', arg);
			if (!val) return 2;
			{
				char **g = realloc(progfiles, (size_t)(nprogfiles + 1) * sizeof *g);
				if (!g) { __util_diagf("awk: out of memory\n"); return 2; }
				progfiles = g;
				progfiles[nprogfiles++] = (char *)val;
			}
			have_f = 1;
			break;
		default:
			__util_diagf("awk: -%c: invalid option\n", arg[1]);
			return 2;
		}
	}

	/* ---- fatal-error unwind: armed once here, covers every phase below
	 * (loading -f progfiles, parsing, running) without separate per-
	 * phase machinery -- see awk_priv.h's own long comment on
	 * awk_fatal_env/awk_unwind_fatal() for the full design, including
	 * why the catching branch below deliberately touches nothing but
	 * awk_fatal_armed and a hardcoded status (ip/prog/progtext are
	 * ordinary, non-volatile locals modified after this setjmp(), so
	 * touching them from here would itself be undefined behavior --
	 * see that same comment's point 3). */
	if (setjmp(awk_fatal_env)) {
		awk_fatal_armed = 0;
		return 2;
	}
	awk_fatal_armed = 1;

	if (have_f) {
		progtext = load_progfiles(progfiles, nprogfiles);
		if (!progtext) { awk_fatal_armed = 0; return 2; }
	} else {
		if (i >= argc) { __util_diagf("awk: missing program text\n"); awk_fatal_armed = 0; return 2; }
		progtext = argv[i];
		i++;
	}

	prog = awk_parse_program(progtext);
	if (!prog) { awk_fatal_armed = 0; return 2; }

	awk_interp_init(&ip, prog);
	awk_interp_setup_environ(&ip, environ);
	if (fsarg) awk_interp_set_str(&ip, "FS", fsarg);
	{
		int vi;
		for (vi = 0; vi < nvassigns; vi++)
			awk_interp_set_str(&ip, vassigns[vi].name, vassigns[vi].val);
	}
	awk_interp_setup_argv(&ip, argv[0], argc - i, argv + i);

	status = awk_interp_run(&ip);
	awk_interp_free(&ip);
	/* The parsed program (AST/lexer-owned strings/compiled literal
	 * EREs) is deliberately never freed -- this is a short-lived CLI
	 * process (or, as a shell built-in, one bi_awk() invocation), so
	 * the OS reclaims it at exit either way; see this file's own
	 * header for the same allocation-failure-handling philosophy
	 * (fatal rather than threaded through every call site) applied
	 * one step further, to a whole-of-run allocation nothing in this
	 * tree's other utilities needs to free piecemeal either (compare
	 * src/util/sort.c's own free_lines() -- sort frees because it may
	 * run again in the same process's loop in principle; awk's own
	 * program is parsed exactly once per process). */
	awk_fatal_armed = 0;
	return status;
}
