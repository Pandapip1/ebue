/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * sh -- the `sh` utility (XCU sh(1p)), a thin main() over the shell
 * engine that is compiled into libc.a (src/sh/, see src/sh/sh.h for the
 * AST and the __sh_exec_*() contract, and test/sh-design.md for why a
 * libc repo grows a shell at all).
 *
 * This file lives in its own top-level directory, and builds its own
 * binary, on purpose: test/sh-design.md's "Placement and gates" says to
 * "Ship it as a clearly separate deliverable in this repo -- its own
 * source directory and binary -- rather than blurring into src/", and
 * CONTRIBUTING.md's "Why a shell lives in a libc repo" says the same
 * thing in terms of what the rest of that file is about. Mechanically it
 * also matters: the Makefile builds lib/libc.a from a src/* wildcard, so
 * a main() under src/ would be archived into libc.a and could collide
 * with the main() of every program that links it.
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
 * here-documents, subshells, brace groups, `cd`). Two classes of thing a
 * real script does would otherwise be *silently misinterpreted* rather
 * than diagnosed, because they are syntactically indistinguishable from
 * something the engine does support:
 *
 *   - Reserved words and unimplemented built-ins. `if`, `for`, `export`,
 *     `exit`, ... all lex as ordinary WORD tokens today (sh.h's banner),
 *     so `if foo; then bar; fi` parses as four separate simple commands
 *     and would run a program called "if". PATH lookup then fails and
 *     the shell reports "if: command not found" with status 127 -- a
 *     true statement about a fiction, and precisely the "cannot tell"
 *     failure test/sh-design.md's "Placement and gates" warns about.
 *     `export X=1` is worse: it fails with 127 while the variable is
 *     silently *not* exported.
 *   - Positional and other special parameters. src/wordexp/wordexp.c's
 *     expand_param() supports exactly $NAME and ${NAME}; a `$` followed
 *     by anything else (a digit, @, *, #, ?, !, -, $) is left in place as
 *     a literal `$`, so `echo "$1"` prints the two characters "$1"
 *     instead of the first argument, and `exit $?` never sees a status.
 *     That is silent corruption of a script's meaning, not a missing
 *     feature the script can notice.
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
#include "../src/sh/sh.h"

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
	diag("%s: positional and special parameters ($1, $@, $#, $?, ...) are "
	     "not implemented%s -- see test/sh-design.md", what, where);
}

/* ---- the refusal lists ----------------------------------------------
 *
 * Reserved words (XCU 2.4) that the grammar does not implement. `{`,
 * `}` and `!` are absent because the parser does recognise those three
 * (sh.h's banner), and `in` is here because it can only ever appear as
 * part of a for/case the parser cannot build. */
static const char *const reserved[] = {
	"if", "then", "else", "elif", "fi",
	"case", "esac", "while", "until", "for", "do", "done", "in",
	0
};

/* Utilities whose whole effect is on the shell's own execution
 * environment (XCU 2.12) and which therefore *cannot* be a program
 * found on PATH: every XCU 2.14 special built-in plus the regular
 * built-ins from XCU 2.9.1's "Command Search and Execution" note that
 * are equally intrinsic. `cd` is not here -- src/sh/exec.c implements
 * it. Anything not on this list (echo, test, kill, printf, ...) is a
 * genuine external utility on a POSIX system, so letting PATH lookup
 * fail with an honest "command not found" is the right answer for it,
 * not a refusal from here. */
static const char *const unimplemented_builtins[] = {
	":", ".", "break", "continue", "eval", "exec", "exit", "export",
	"readonly", "return", "set", "shift", "times", "trap", "unset",
	"alias", "unalias", "bg", "fg", "jobs", "command", "getopts",
	"hash", "read", "ulimit", "umask", "wait",
	0
};

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
				/* wordexp()'s expand_param() requires a name
				 * immediately after '{': ${1}, ${@}, ${#VAR} all
				 * fall out of it as a literal '$' plus literal
				 * "{...}" text, which is silent nonsense. */
				char d = p[2];
				if (!((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || d == '_'))
					bad = p;
			} else if ((c >= '0' && c <= '9') ||
			           c == '@' || c == '*' || c == '#' ||
			           c == '?' || c == '!' || c == '-' || c == '$') {
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

static int check_command(const struct sh_command *c)
{
	const char *name;

	if (check_redirs(c->redirs)) return -1;
	if (c->body) return check_list(c->body);
	if (check_words(c->assigns) || check_words(c->words)) return -1;

	if (!c->words || !c->words->text) return 0;
	name = c->words->text;
	if (in_list(reserved, name)) {
		diag("%s: control-flow reserved words (if/while/for/case) are not "
		     "implemented -- see test/sh-design.md", name);
		return -1;
	}
	if (in_list(unimplemented_builtins, name)) {
		diag("%s: this shell has no `%s' built-in yet, and it cannot be an "
		     "external command -- see test/sh-design.md", name, name);
		return -1;
	}
	return 0;
}

static int check_list(const struct sh_list *list)
{
	const struct sh_list_item *it;
	const struct sh_andor *a;
	size_t i;

	if (!list) return 0;
	for (it = list->items; it; it = it->next) {
		if (it->sep == SH_SEP_AMP) {
			/* src/sh/exec.c's __sh_exec_list() runs an async item
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
 * Returns 0 on success. */
static int slurp(FILE *f, char **out)
{
	size_t cap = 4096, len = 0;
	char *buf = malloc(cap);

	if (!buf) return -1;
	for (;;) {
		size_t n = fread(buf + len, 1, cap - len - 1, f);
		len += n;
		if (n == 0) break;
		if (len + 1 >= cap) {
			char *nb = realloc(buf, cap * 2);
			if (!nb) { free(buf); return -1; }
			buf = nb;
			cap *= 2;
		}
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

int main(int argc, char **argv)
{
	const char *cmdstr = 0;
	const char *file = 0;
	int stdin_flag = 0;
	char *text = 0;
	char err[256];
	struct sh_list *list;
	int status = 0, i;

	if (argc > 0 && argv[0] && *argv[0]) {
		const char *b = argv[0], *p;
		for (p = argv[0]; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
		if (*b) progname = b;
	}

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

	/* Positional parameters are deliberately *not* installed anywhere:
	 * there is nothing to install them into. The engine expands words
	 * through wordexp(), whose only variable store is the real environ
	 * (src/wordexp/wordexp.c's expand_param() -> getenv), and $0/$1/$#
	 * are not environment variables and never reach getenv() at all --
	 * `$1` is not even lexed as an expansion. Rather than pretend, this
	 * file refuses any program that references one (preflight() above),
	 * so the remaining operands are only ever consumed as $0 (a
	 * diagnostic prefix) and, for the non-`-c` forms, the script path. */
	if (cmdstr) {
		if (i < argc && *argv[i]) progname = argv[i];   /* command_name is $0 */
	} else if (!stdin_flag && i < argc) {
		file = argv[i];
	}

	if (cmdstr) {
		text = malloc(strlen(cmdstr) + 1);
		if (!text) { diag("out of memory"); return EX_USAGE; }
		strcpy(text, cmdstr);
	} else if (file) {
		FILE *f = fopen(file, "rb");
		if (!f) { diag("%s: cannot open command_file", file); return EX_NOSCRIPT; }
		if (slurp(f, &text)) { fclose(f); diag("%s: read error", file); return EX_NOSCRIPT; }
		fclose(f);
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
		 * with no status written. The commonest cause by far is a word
		 * containing a command substitution, which src/sh/exec.c
		 * surfaces from wordexp() as WRDE_CMDSUB. */
		diag("cannot execute: an unsupported construct (most often "
		     "command substitution, `$(...)'/backticks) -- see "
		     "test/sh-design.md");
		__sh_list_free(list);
		free(text);
		return EX_USAGE;
	}
	__sh_list_free(list);
	free(text);
	return status;
}
