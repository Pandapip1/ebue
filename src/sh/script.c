/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The `sh` utility (XCU sh(1p)) as a *function*: option and operand
 * handling, the refusal preflight, and the run, over the shell engine
 * in the rest of src/sh/ (see sh.h for the AST and the __sh_exec_*()
 * contract).
 *
 * Two callers, and the second is why this is not sh/main.c any more:
 *
 *   - sh/main.c, which is now literally `return __sh_main(argc, argv)`.
 *     That is what the design note's "the `sh` binary is a thin main()
 *     over them" always said it should be; until this file moved, the
 *     "them" stopped short of everything below and the binary was the
 *     only way to reach it.
 *
 *   - __sh_run_script() at the end of this file: the interpreter that
 *     XSH exec's [ENOEXEC] clause and XCU 2.9.1's command search both
 *     have to invoke for a script with no usable image header.  Those
 *     two used to spawn sh.exe as a second process, found beside the
 *     calling image or on PATH.  Both halves of that were wrong, and
 *     the reasons are src/process/exec.c's to state; the short form is
 *     the design note's own reuse rule, which those two are not
 *     exempt from: "The shell is a set of internal functions compiled
 *     into libc.a.  The sh binary is a thin main() over them.  [...]
 *     call those functions directly and never spawn an external
 *     interpreter."
 *
 * Nothing here may be named `main`: the Makefile archives a wildcard
 * over src/ into libc.a, so a main() here would collide with the main()
 * of every program that links it.  That is the same constraint that put
 * this code under sh/ to begin with; only the name has to give, not the
 * placement, because the placement is what the reuse rule needs.
 *
 * ---- What this accepts (XCU sh(1p), "SYNOPSIS") ----------------------
 *
 *   sh -c command_string [command_name [argument...]]
 *   sh [-s] [command_file [argument...]]     (script file, or stdin)
 *
 * With no -c and no command_file, the program text is read from standard
 * input, exactly as `-s` asks for explicitly. `--` ends option parsing;
 * a lone `-` is accepted as the historical synonym for "no more
 * options" that every sh honours.
 *
 * Deliberate deviation, stated rather than hidden: a script read from
 * standard input is read *to EOF up front* and then executed, where a
 * real sh reads it incrementally and leaves the unread remainder on fd
 * 0 for the commands themselves to consume. Nothing in the supported
 * subset can loop over its own input (no while/read -- see below), so
 * the difference is observable only to a command that deliberately reads
 * the rest of the script off fd 0, and reading up front is what lets
 * this file hand __sh_parse() a complete program the way `-c` does.
 *
 * ---- What it refuses, and why refusing beats running -----------------
 *
 * The engine implements a documented subset (src/sh/sh.h's banner:
 * simple commands, pipelines, and-or lists, redirections including
 * here-documents, subshells, brace groups, the compound commands, the
 * built-ins, and the positional parameters). Two classes of thing a
 * real script does would otherwise be *silently misinterpreted* rather
 * than diagnosed, because they are syntactically indistinguishable from
 * something the engine does support:
 *
 *   - Reserved words and unimplemented built-ins. `case` and `export`
 *     still lex as ordinary WORD tokens (sh.h's banner), so
 *     `case x in y) ;; esac` parses as simple commands and would run a
 *     program called "case". PATH lookup then fails and the shell
 *     reports "case: command not found" with status 127 -- a true
 *     statement about a fiction, and precisely the "cannot tell"
 *     failure test/sh-design.md's "Placement and gates" warns about.
 *     `export X=1` is worse: it fails with 127 while the variable is
 *     silently *not* exported.
 *
 *     Function definitions are no longer in this class either: stage 7b
 *     gave them a grammar (XCU 2.9.5), so `f() { ... }` is parsed and
 *     `f` really is called.  A definition's body is checked *here*, at
 *     the definition, by re-parsing it -- see check_command() below --
 *     so a function whose body uses something on these lists is refused
 *     before any of the program runs, not on the call.
 *
 *     `if`/`while`/`until`/`for` are no longer in this class: stage 6b
 *     gave them a real grammar, and a misplaced `fi`/`do`/`done` is now
 *     a parse error rather than a command name. `for name` with no `in`
 *     list -- XCU 2.9.4's `in "$@"` -- came off too in stage 7, which
 *     gave this shell a "$@" to iterate.
 *   - The special parameters that are still not implemented.
 *     src/wordexp/wordexp.c expands $NAME/${NAME} and, through
 *     __wordexp_sh(), XCU 2.5.1's positional parameters plus 2.5.2's
 *     '@', '*', '#' and '0'; a `$` followed by anything else (?, !, -,
 *     $) is left in place as a literal `$`, so `exit $?` never sees a
 *     status. That is silent corruption of a script's meaning, not a
 *     missing feature the script can notice, so it is refused. So is
 *     `${#NAME}`, which is string length -- a different expansion that
 *     must not be mistaken for the `${#}` this shell does implement.
 *
 * So preflight() below walks the AST __sh_parse() just produced and
 * refuses the whole program, naming what is unsupported, before running
 * any of it. Refusing up front rather than at the point of use is the
 * conservative choice on purpose: a shell that has already run half a
 * build script before discovering it cannot run the rest has done real
 * damage that a diagnostic cannot undo.
 *
 * Everything the *engine* declines at execution time reaches this file
 * as __sh_exec_list()'s -1 ("cannot execute this AST node at all", see
 * src/sh/sh.h) rather than as a status, and is reported the same way --
 * a message on stderr and a nonzero exit, never a made-up status.
 *
 * ---- Exit status (XCU 2.8.2, sh(1p) "EXIT STATUS") -------------------
 *
 * "The exit status of the shell shall be the exit status of the last
 * command executed" -- an empty program (or one that is only comments)
 * runs no command and exits 0. A syntax error, an unsupported construct
 * and a usage error all exit 2 (>0, with a diagnostic on stderr, which
 * is what 2.8.1 requires of a non-interactive shell; the specific value
 * is implementation-defined, and 2 is what bash/dash use for exactly
 * these). A command_file that cannot be opened or read exits 127, the
 * value sh(1p) gives for "the command_file could not be found" and the
 * one this project's system()/exec.c already use for that class.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libc.h"
#include "sh.h"

#define EX_USAGE 2       /* usage error, syntax error, unsupported construct */
#define EX_NOSCRIPT 127  /* command_file unopenable/unreadable */

static const char *progname = "sh";

/* Every diagnostic this file writes goes to stderr, prefixed with $0,
 * and ends in a newline -- XCU sh(1p)'s "STDERR" ("used only for
 * diagnostic messages") plus 2.8.1's requirement that a non-interactive
 * shell say something before it gives up. A macro rather than a
 * function so each call site keeps its own printf arguments. */
#define diag(...) do { \
	fprintf(stderr, "%s: ", progname); \
	fprintf(stderr, __VA_ARGS__); \
	fputc('\n', stderr); \
} while (0)

/* One message, three call sites (a word, a redirection target, a
 * here-document body), so the wording cannot drift between them. */
static void diag_bad_param(const char *what, const char *where)
{
	diag("%s: this special parameter ($!, $$, $-, ${#NAME}) is not "
	     "implemented%s -- see test/sh-design.md", what, where);
}

/* ---- the refusal lists ----------------------------------------------
 *
 * Reserved words (XCU 2.4) that the grammar does not implement.
 *
 * Down to two.  `if`/`then`/`else`/`elif`/`fi`, `while`/`until`,
 * `for`/`do`/`done` and `in` all came off with stage 6b: src/sh/parse.c
 * builds those constructs now, and a *misplaced* one of them -- a bare
 * `fi`, a stray `do` -- is a syntax error raised there (XCU 2.10.1
 * rule 1) rather than a command named "fi".  The property this list
 * exists to preserve is that no such word is ever silently executed as
 * an external program, and the parser now holds that property for these
 * words directly, which is a strictly stronger place for it: it catches
 * a misplaced `fi` anywhere in the program, including inside a "(...)"
 * this check would have to recurse into.  `{`, `}` and `!` were never
 * on this list for exactly the same reason.
 *
 * `case`/`esac` stay: that construct is not implemented, so `case`
 * still lexes as an ordinary WORD (sh.h's banner) and
 * `case x in y) ;; esac` would otherwise run a program called "case"
 * and exit 127 about a fiction. */
static const char *const reserved[] = {
	"case", "esac",
	0
};

/* Utilities whose whole effect is on the shell's own execution
 * environment (XCU 2.12) and which therefore *cannot* be a program
 * found on PATH: every XCU 2.14 special built-in plus the regular
 * built-ins from XCU 2.9.1's "Command Search and Execution" note that
 * are equally intrinsic.
 *
 * A name comes off this list exactly when src/sh/builtin.c grows a real
 * implementation of it, never before. `cd`, `:` and `exit` are already
 * gone (stage 6a's dispatcher); `test`, `[`, `true` and `false` were
 * never on it, because on a POSIX system they are genuine external
 * utilities and letting PATH lookup fail honestly was the right answer
 * -- except that this platform has no /bin at all, which is why stage
 * 6a built them in too (see src/sh/builtin.c's header).  Anything still
 * on this list is refused, up front, by name. */
static const char *const unimplemented_builtins[] = {
	".", "break", "continue", "eval", "exec", "export",
	"readonly", "times", "trap", "unset",
	"alias", "unalias", "bg", "fg", "jobs", "command", "getopts",
	"hash", "read", "ulimit", "umask", "wait",
	0
};

/* list is required: `list[i]` is indexed unconditionally in the loop's
 * own init/condition, evaluated at least once regardless. Every real
 * call site passes one of this file's own static, always-populated
 * arrays (`reserved`, `unimplemented_builtins`). s is left unmarked --
 * only ever forwarded into strcmp(), never dereferenced by this
 * function itself. */
static int in_list(const char *const *list, const char *s) __attribute__((nonnull(1)));
static int in_list(const char *const *list, const char *s)
{
	size_t i;
	for (i = 0; list[i]; i++) if (strcmp(list[i], s) == 0) return 1;
	return 0;
}

/* Scans one word's *raw* source text (quotes and backslashes still in
 * place, see sh.h) for a parameter expansion wordexp() would not
 * perform. Returns the offending text's leading characters in `what`
 * (NUL-terminated, at most 4 characters, so `what` needs 5 bytes)
 * and 1, or 0 if the word is clean.
 *
 * The quoting state machine is the minimum that gets the answer right
 * rather than a second lexer: single quotes make everything up to the
 * next single quote literal (2.2.2, no escapes inside), a backslash
 * outside single quotes escapes the next character (2.2.1/2.2.3), and
 * double quotes change nothing about whether `$` introduces an
 * expansion (2.2.3 -- "$1" is still an expansion), only about field
 * splitting, which is not what this is looking for. */
static int bad_expansion(const char *text, char *what)
{
	const char *p = text;
	int dq = 0;

	while (*p) {
		if (*p == '\'' && !dq) {
			for (p++; *p && *p != '\''; p++) ;
			if (*p) p++;
			continue;
		}
		if (*p == '\\' && p[1]) { p += 2; continue; }
		if (*p == '"') { dq = !dq; p++; continue; }
		if (*p == '$') {
			char c = p[1];
			const char *bad = 0;
			if (c == '{') {
				/* What __wordexp_sh() accepts inside braces, and
				 * nothing else.  ${1}/${10} (XCU 2.5.1 requires the
				 * braces past one digit), ${@}/${*}/${#} (2.5.2) and
				 * ${NAME} are real expansions now; ${#NAME} is string
				 * length, a different expansion this shell does not
				 * implement, and it must stay refused rather than
				 * being mistaken for ${#}. */
				const char *d = p + 2;
				if (*d >= '0' && *d <= '9') {
					while (*d >= '0' && *d <= '9') d++;
					if (*d != '}') bad = p;
				} else if ((*d == '@' || *d == '*' || *d == '#' || *d == '?') && d[1] == '}') {
					/* implemented */
				} else if (!((*d >= 'a' && *d <= 'z') || (*d >= 'A' && *d <= 'Z') || *d == '_')) {
					bad = p;
				}
			} else if (c == '!' || c == '-' || c == '$') {
				/* The special parameters of 2.5.2 that are still not
				 * implemented.  $0..$9, $@, $* and $# came off this
				 * list in stage 7a and $? in stage 7b; all are
				 * expanded for real. */
				bad = p;
			}
			if (bad) {
				/* Quote back just the expansion's introducer ("$1",
				 * "$@", "${#"), never a fixed number of following
				 * characters -- those would drag in whatever the rest
				 * of the word happened to be and make the message
				 * read as if the shell were confused about it. */
				size_t n = 0;
				what[n++] = '$';
				if (bad[1]) what[n++] = bad[1];
				if (bad[1] == '{' && bad[2]) what[n++] = bad[2];
				what[n] = 0;
				return 1;
			}
		}
		p++;
	}
	return 0;
}

static int check_words(const struct sh_word *w)
{
	char what[8];
	for (; w; w = w->next) {
		if (bad_expansion(w->text, what)) {
			diag_bad_param(what, "");
			return -1;
		}
	}
	return 0;
}

static int check_redirs(const struct sh_redir *r)
{
	char what[8];
	for (; r; r = r->next) {
		if (r->word && bad_expansion(r->word, what)) {
			diag_bad_param(what, "");
			return -1;
		}
		/* A here-document body is expanded exactly like a double-quoted
		 * word unless the delimiter was quoted (2.7.4), so an unquoted
		 * one is subject to the same silent-literal problem. */
		if (r->heredoc && !r->heredoc_quoted &&
		    bad_expansion(r->heredoc, what)) {
			diag_bad_param(what, " (in a here-document body)");
			return -1;
		}
	}
	return 0;
}

static int check_list(const struct sh_list *list);

static int check_command(const struct sh_command *c) __attribute__((nonnull(1)));
static int check_command(const struct sh_command *c)
{
	const char *name;
	const struct sh_ifarm *a;

	if (check_redirs(c->redirs)) return -1;

	/* Switched on the kind rather than on "does it have a body?": the
	 * compound commands stage 6b added keep their parts in several
	 * different fields (an if's arms, a loop's condition, a for's word
	 * list), and a `for` in particular has *both* a word list to scan
	 * for unsupported expansions and a body to recurse into.  The old
	 * `if (c->body) return check_list(c->body);` would have walked a
	 * loop's body and silently skipped its condition -- a program whose
	 * `while` test used "$1" would then have run. */
	switch (c->kind) {
	case SH_CMD_SUBSHELL:
	case SH_CMD_BRACE:
		return check_list(c->body);
	case SH_CMD_IF:
		for (a = c->arms; a; a = a->next)
			if (check_list(a->cond) || check_list(a->body)) return -1;
		return check_list(c->else_body);
	case SH_CMD_LOOP:
		if (check_list(c->cond)) return -1;
		return check_list(c->body);
	case SH_CMD_FUNCDEF:
		/* The body is source text (src/sh/sh.h), not a subtree, so it
		 * is re-parsed to be checked.  Checking it *here*, at the
		 * definition, is what keeps this file's refuse-before-anything-
		 * runs property: a function whose body uses `export` or `$!`
		 * would otherwise be defined happily and blow up on the call,
		 * by which time half the script has run.  A parse failure is
		 * impossible for text src/sh/parse.c already parsed once, and
		 * is reported rather than assumed away. */
		{
			struct sh_list *body = __sh_parse(c->func_text, 0, 0);
			int rc;
			if (!body) {
				diag("%s: cannot re-parse the function body", c->name);
				return -1;
			}
			rc = check_list(body);
			__sh_list_free(body);
			return rc;
		}
	case SH_CMD_FOR:
		/* `for name` with no `in` list -- XCU 2.9.4's "Omitting: in
		 * word ... shall be equivalent to: in "$@"" -- used to be
		 * refused here, because there was no "$@" to iterate.  Stage 7
		 * gives this shell positional parameters, so it runs; there is
		 * nothing left for this arm to refuse beyond what the word
		 * list and body already get. */
		if (check_words(c->words)) return -1;
		return check_list(c->body);
	default:
		break;
	}

	if (check_words(c->assigns) || check_words(c->words)) return -1;

	if (!c->words || !c->words->text) return 0;
	name = c->words->text;
	if (in_list(reserved, name)) {
		diag("%s: the `case' construct is not implemented -- see "
		     "test/sh-design.md", name);
		return -1;
	}
	if (in_list(unimplemented_builtins, name)) {
		diag("%s: this shell has no `%s' built-in yet, and it cannot be an "
		     "external command -- see test/sh-design.md", name, name);
		return -1;
	}
	return 0;
}

/* list is deliberately left unmarked: `if (!list) return 0;` right below
 * is a real, working check -- check_command()'s own SH_CMD_IF/LOOP/FOR
 * arms above pass a compound command's optional parts (e.g. cmd->else_body
 * with no `else`) straight through as NULL.
 *
 * Not fixed by this: the flagged `a->pipeline.commands[i]` deref is
 * about `a`, a local loop variable walking `it->andor`, and its own
 * `.pipeline.commands` array pointer -- the same class of internal-AST
 * residual as print.c's queue_nested_heredocs_list() above, not
 * something list's own nullability can express. */
static int check_list(const struct sh_list *list)
{
	const struct sh_list_item *it;
	const struct sh_andor *a;
	size_t i;

	if (!list) return 0;
	for (it = list->items; it; it = it->next) {
		if (it->sep == SH_SEP_AMP) {
			/* src/sh/execute.c's __sh_exec_list() runs an async item
			 * synchronously ("true backgrounding is future work"),
			 * which is a silently different meaning -- the caller
			 * blocks, and the list's status is the command's rather
			 * than 0 -- not a missing feature it can detect. */
			diag("asynchronous lists (`&') are not implemented -- see "
			     "test/sh-design.md");
			return -1;
		}
		for (a = it->andor; a; a = a->next)
			for (i = 0; i < a->pipeline.ncommands; i++)
				if (check_command(&a->pipeline.commands[i])) return -1;
	}
	return 0;
}

/* Refuses the whole program if any part of it is something the engine
 * would misinterpret rather than diagnose. See this file's header. */
static int preflight(const struct sh_list *list)
{
	return check_list(list);
}

/* ---- reading the program text --------------------------------------- */

/* Reads all of `f` into a freshly malloc'd, NUL-terminated buffer.
 * Returns 0 on success (and never leaves *out set on failure).
 *
 * The buffer is grown *before* each read rather than after, so `room`
 * is never zero and fread() is never called with nothing to read into;
 * and the loop stops on the first short read, so it is never called
 * again on a stream that already hit EOF or an error. */
static int slurp(FILE *f, char **out)
{
	size_t cap = 4096, len = 0;
	char *buf = malloc(cap);

	if (!buf) return -1;
	for (;;) {
		size_t room, n;

		if (cap - len < 2) {
			char *nb;
			if (cap > (size_t)-1 / 2) { free(buf); return -1; }
			nb = realloc(buf, cap * 2);
			if (!nb) { free(buf); return -1; }
			buf = nb;
			cap *= 2;
		}
		room = cap - len - 1;
		n = fread(buf + len, 1, room, f);
		if (n > room) { free(buf); return -1; }   /* cannot happen; keeps the bound checked, not assumed */
		len += n;
		if (n < room) break;                       /* EOF or error: nothing more is coming */
	}
	if (ferror(f)) { free(buf); return -1; }
	buf[len] = 0;
	*out = buf;
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: %s -c command_string [command_name [argument...]]\n"
		"       %s [-s] [command_file [argument...]]\n",
		progname, progname);
}

int __sh_main(int argc, char **argv)
{
	const char *cmdstr = 0;
	const char *file = 0;
	int stdin_flag = 0;
	char *text = 0;
	char err[256];
	struct sh_list *list;
	int status = 0, i, pfirst;
	const char *zero;

	/* Reset rather than rely on the initialiser: __sh_run_script()
	 * below can call this more than once in one process. */
	progname = "sh";
	if (argc > 0 && argv[0] && *argv[0]) {
		const char *b = argv[0], *p;
		for (p = argv[0]; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
		if (*b) progname = b;
	}
	zero = progname;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] != '-' || a[1] == 0) break;   /* operand, or a lone "-" */
		if (strcmp(a, "--") == 0) { i++; break; }
		if (strcmp(a, "-c") == 0) {
			if (i + 1 >= argc) { diag("-c requires a command_string"); usage(); return EX_USAGE; }
			cmdstr = argv[++i];
			continue;
		}
		if (strcmp(a, "-s") == 0) { stdin_flag = 1; continue; }
		diag("unrecognized option `%s'", a);
		usage();
		return EX_USAGE;
	}
	if (i < argc && strcmp(argv[i], "-") == 0) i++;   /* historical "-" */

	/* sh(1p) OPERANDS, and XCU 2.5.1's "[p]ositional parameters are
	 * initially assigned when the shell is invoked (see sh)".  This
	 * used to be a comment explaining that there was nowhere to put
	 * them and that any program referencing one was refused; stage 7
	 * gives the engine a real list (src/sh/param.c), so the operands
	 * are installed for real here.
	 *
	 * Which operand is $0 differs by form, and 2.5.2 is emphatic that
	 * $0 is not one of the positional parameters, so it is set
	 * separately in all three:
	 *
	 *  - `sh -c command_string [command_name [argument...]]`:
	 *    command_name is $0 and the arguments after it are $1 on.  An
	 *    absent command_name leaves $0 as the shell's own name.
	 *  - `sh command_file [argument...]`: sh(1p) OPERANDS makes
	 *    command_file "$0", and the arguments after it $1 on.
	 *  - `sh [-s] [argument...]`: the program comes from standard
	 *    input, so every operand is a positional parameter.
	 *
	 * `progname` -- what this file prefixes its own diagnostics with --
	 * deliberately does *not* follow $0 into the command_file case:
	 * "script.sh: script.sh: cannot open command_file" reads as a
	 * confused shell rather than a clear one.  It follows $0 for -c,
	 * which is the form where a build system chooses a name precisely
	 * so that diagnostics carry it. */
	pfirst = argc;
	if (cmdstr) {
		if (i < argc && *argv[i]) progname = argv[i];   /* command_name is $0 */
		if (i < argc) { zero = argv[i]; pfirst = i + 1; }
	} else if (!stdin_flag && i < argc) {
		file = argv[i];
		zero = argv[i];
		pfirst = i + 1;
	} else {
		pfirst = i;
	}
	if (__sh_param_set_zero(zero) < 0 ||
	    __sh_params_replace(argv + pfirst, argc - pfirst) < 0) {
		diag("out of memory");
		return EX_USAGE;
	}

	if (cmdstr) {
		text = malloc(strlen(cmdstr) + 1);
		if (!text) { diag("out of memory"); return EX_USAGE; }
		strcpy(text, cmdstr);
	} else if (file) {
		FILE *f = fopen(file, "rb");
		if (!f) { diag("%s: cannot open command_file", file); return EX_NOSCRIPT; }
		if (slurp(f, &text)) { (void)fclose(f); diag("%s: read error", file); return EX_NOSCRIPT; }
		(void)fclose(f);
	} else {
		if (slurp(stdin, &text)) { diag("stdin: read error"); return EX_NOSCRIPT; }
	}

	list = __sh_parse(text, err, sizeof err);
	if (!list) {
		diag("syntax error: %s", err);
		free(text);
		return EX_USAGE;
	}
	if (preflight(list)) {
		__sh_list_free(list);
		free(text);
		return EX_USAGE;
	}
	if (__sh_exec_list(list, &status)) {
		/* src/sh/sh.h: -1 is "cannot execute this AST node at all",
		 * with no status written. This used to say the commonest cause
		 * was a word containing a command substitution; stage 5
		 * implements those, so what is left is narrower and not worth
		 * guessing at in the message -- two directly adjacent compound
		 * commands in one pipeline ("( a ) | { b; }", which exec.c
		 * refuses rather than deadlock without a fork()), a command
		 * substitution whose own command hits one of these, and
		 * resource failures. */
		diag("cannot execute: an unsupported construct -- see "
		     "test/sh-design.md");
		__sh_list_free(list);
		free(text);
		return EX_USAGE;
	}
	__sh_list_free(list);
	free(text);
	return status;
}

/* ---- the [ENOEXEC] interpreter --------------------------------------
 *
 * Runs `argv` -- already in the shape both clauses specify, i.e.
 * { arg0, command_file, argument..., 0 } -- as one invocation of the
 * sh utility above, in this process, and returns its exit status.
 *
 * The only thing this adds to __sh_main() is that it is re-entrant with
 * respect to a shell that is *already* running in this process, which
 * is exactly the src/sh/execute.c caller's situation: XCU 2.9.1's fallback
 * is a shell invoked to run one command, not a takeover of the one that
 * invoked it, so the running shell's positional parameters, $0 and
 * function definitions have to survive it.  Save/restore is the same
 * move-out/move-in pair a subshell and a function call already use
 * (src/sh/param.c, src/sh/func.c), for the same reason and with the
 * same nesting behaviour.
 *
 * __sh_flow_clear() consumes any pending `exit` on the way out: the
 * script exiting is the end of *this* invocation, and leaving the flag
 * set would unwind the calling shell too.  Consuming a pending exit at
 * a shell-environment boundary is what sh.h already specifies for a
 * subshell.
 *
 * The src/process/exec.c caller needs none of that -- it _exit()s with
 * the status and never comes back -- but paying for it there is a
 * strdup and two pointer swaps, and one shared entry point is what
 * keeps the two clauses' behaviour identical by construction rather
 * than by review. */
int __sh_run_script(int argc, char *const argv[])
{
	struct sh_params psaved;
	struct sh_funcs fsaved;
	char *zsaved;
	int status;

	{
		const char *z = __sh_param_zero();   /* never NULL; "sh" if unset */
		size_t n = strlen(z) + 1;
		zsaved = malloc(n);
		if (!zsaved) { diag("out of memory"); return EX_USAGE; }
		memcpy(zsaved, z, n);
	}
	__sh_params_take(&psaved);
	__sh_funcs_take(&fsaved);

	status = __sh_main(argc, (char **)argv);

	__sh_flow_clear();
	__sh_funcs_install(&fsaved);
	__sh_params_install(&psaved);
	__sh_param_set_zero(zsaved);
	free(zsaved);
	return status;
}
