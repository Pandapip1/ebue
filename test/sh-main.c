/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Black-box tests for the `sh` *binary* (sh/main.c -> obj/sh/sh.exe):
 * argument handling, exit status, and the diagnostics it prints instead
 * of running something it would get wrong.
 *
 * Why this is a separate test from test/sh-engine.c rather than more
 * cases inside it: the two exercise different things through different
 * interfaces. sh-engine.c links the engine out of libc.a and calls
 * __sh_parse()/__sh_exec_*() in its own process -- there is no second
 * image involved, and that is exactly what makes it able to inspect an
 * AST. Everything here is only observable *from outside a process*: an
 * exit status the OS reports, bytes on that process's stderr, whether
 * argv[1] was taken as a script path or a command string. Testing that
 * requires spawning obj/sh/sh.exe for real, which in turn makes the
 * whole file depend on a binary the engine's own tests must not need
 * (`make check` would otherwise stop being able to test the engine
 * without linking the program). Keeping them apart also keeps each
 * one's failure legible: a red sh-engine.exe means the language is
 * wrong, a red sh-main.exe means the *utility* is.
 *
 * How the exe is found: this test walks up from its own argv[0]
 * (obj/test/sh-main.exe) to obj/ and down again to sh/sh.exe, rather
 * than taking a path from the environment or a -D. tools/run-tests.py
 * runs every test from a private temporary working directory, so
 * nothing relative to the cwd would resolve, and argv[0] is the only
 * thing that is true both under Wine (where it arrives as a DOS path)
 * and on the real-Windows CI leg.
 *
 * The commands the shell runs are this same binary re-exec'd in a child
 * role (--exit-child/--produce/--cat), matching test/sh-engine.c's own
 * pattern and for the same reason: this platform has no /bin/true,
 * /bin/echo or /bin/cat to build a test around.
 *
 * Nothing here forks. The shell spawns processes (src/sh/exec.c never
 * calls fork(), by design) and so does this file, via __spawn() --
 * which matters for the CI Wine legs, whose Wine has no
 * RtlCloneUserProcess and hangs rather than fails on a fork().
 *
 * Spec pages consulted (https://pubs.opengroup.org/onlinepubs/9699919799/):
 *   utilities/sh.html (SYNOPSIS, OPERANDS, STDIN, EXIT STATUS)
 *   utilities/V3_chap02.html 2.8.1 Consequences of Shell Errors
 *   utilities/V3_chap02.html 2.8.2 Exit Status of a Command
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;
int __spawn(const char *path, char *const argv[], char *const envp[]);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char sh_path[1024];
static const char *self;

/* obj/test/sh-main.exe -> obj/sh/sh.exe, keeping whatever separator the
 * platform handed us in argv[0] ('\\' under Wine and Windows, '/' if a
 * developer ran the exe by a Unix-looking path). Returns 0 on success. */
static int find_sh(const char *argv0)
{
	size_t n;
	char sep = '/';
	char *p;

	if (!argv0 || !*argv0) return -1;
	n = strlen(argv0);
	if (n + 16 >= sizeof sh_path) return -1;
	strcpy(sh_path, argv0);

	for (p = sh_path + n; p > sh_path; p--)
		if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == sh_path) return -1;
	sep = p[-1];
	p[-1] = 0;                       /* strip "/sh-main.exe" */

	for (p = sh_path + strlen(sh_path); p > sh_path; p--)
		if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == sh_path) return -1;
	p[-1] = 0;                       /* strip "/test" */

	n = strlen(sh_path);
	sprintf(sh_path + n, "%csh%csh.exe", sep, sep);
	return 0;
}

/* Runs obj/sh/sh.exe with the given NULL-terminated argument vector
 * (argv[0] is supplied here), with stdin from `infile` (or unchanged if
 * NULL) and stdout/stderr captured to fixed files in the cwd. Returns
 * the child's exit status, or -1 if it could not be run at all.
 *
 * Redirection is done by dup2()ing this process's own descriptors
 * immediately before __spawn() and putting them back straight after --
 * the same technique src/sh/exec.c uses for a real redirection, and the
 * only one available, since __spawn() inherits the table as it stands
 * rather than taking file actions. */
#define OUTFILE "sh-main-out.txt"
#define ERRFILE "sh-main-err.txt"

static int run_sh(char *const *args, const char *infile)
{
	int in = -1, out, err;
	int s0 = -1, s1, s2, pid, status;

	out = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	err = open(ERRFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (out < 0 || err < 0) { if (out >= 0) close(out); if (err >= 0) close(err); return -1; }
	if (infile) {
		in = open(infile, O_RDONLY);
		if (in < 0) { close(out); close(err); return -1; }
	}

	s1 = dup(1); s2 = dup(2);
	if (in >= 0) s0 = dup(0);
	if (in >= 0) dup2(in, 0);
	dup2(out, 1);
	dup2(err, 2);
	close(out); close(err);
	if (in >= 0) close(in);

	pid = __spawn(sh_path, args, environ);

	if (s0 >= 0) { dup2(s0, 0); close(s0); }
	dup2(s1, 1); close(s1);
	dup2(s2, 2); close(s2);

	if (pid < 0) return -1;
	if (waitpid(pid, &status, 0) != pid) return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

/* sh -c CMD [NAME] */
static int run_c(const char *cmd, const char *name)
{
	char *args[5];
	args[0] = (char *)"sh";
	args[1] = (char *)"-c";
	args[2] = (char *)cmd;
	args[3] = (char *)name;
	args[4] = 0;
	if (!name) args[3] = 0;
	return run_sh(args, 0);
}

static int slurp_into(const char *path, char *buf, size_t buflen)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (!f) { buf[0] = 0; return -1; }
	n = fread(buf, 1, buflen - 1, f);
	buf[n] = 0;
	fclose(f);
	return 0;
}

static int err_contains(const char *needle)
{
	char buf[4096];
	slurp_into(ERRFILE, buf, sizeof buf);
	return strstr(buf, needle) != 0;
}

static int out_is_empty(void)
{
	char buf[256];
	slurp_into(OUTFILE, buf, sizeof buf);
	return buf[0] == 0;
}

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	if (!f) { fails++; printf("FAIL: cannot write %s\n", path); return; }
	fputs(text, f);
	fclose(f);
}

/* ---- exit status (XCU 2.8.2, sh(1p) EXIT STATUS) --------------------- */

static void test_status_is_last_command(void)
{
	char cmd[1200];
	sprintf(cmd, "'%s' --exit-child 7", self);
	CHECK(run_c(cmd, 0) == 7);

	sprintf(cmd, "'%s' --exit-child 3; '%s' --exit-child 0", self, self);
	CHECK(run_c(cmd, 0) == 0);

	sprintf(cmd, "'%s' --exit-child 0; '%s' --exit-child 4", self, self);
	CHECK(run_c(cmd, 0) == 4);
}

static void test_empty_program_is_zero(void)
{
	/* No command is executed, so there is no "last command"; every
	 * shell exits 0, and 2.8.2 leaves nothing else it could be. */
	CHECK(run_c("", 0) == 0);
	CHECK(run_c("   \n\t", 0) == 0);
	CHECK(run_c("# nothing but a comment\n", 0) == 0);
}

static void test_andor_and_not_found(void)
{
	char cmd[1200];
	sprintf(cmd, "'%s' --exit-child 1 || '%s' --exit-child 0", self, self);
	CHECK(run_c(cmd, 0) == 0);
	sprintf(cmd, "'%s' --exit-child 0 && '%s' --exit-child 6", self, self);
	CHECK(run_c(cmd, 0) == 6);

	/* sh(1p)/2.8.2: a utility that cannot be found exits 127. */
	CHECK(run_c("no-such-utility-xyzzy", 0) == 127);
}

/* ---- the engine really is wired up through the binary ---------------- */

static void test_redirection_and_pipeline(void)
{
	char cmd[1600], buf[256];

	sprintf(cmd, "'%s' --produce hello > cap1.txt", self);
	CHECK(run_c(cmd, 0) == 0);
	slurp_into("cap1.txt", buf, sizeof buf);
	CHECK(strcmp(buf, "hello") == 0);

	sprintf(cmd, "'%s' --produce piped | '%s' --cat > cap2.txt", self, self);
	CHECK(run_c(cmd, 0) == 0);
	slurp_into("cap2.txt", buf, sizeof buf);
	CHECK(strcmp(buf, "piped") == 0);

	sprintf(cmd, "{ '%s' --produce grouped ; } > cap3.txt", self);
	CHECK(run_c(cmd, 0) == 0);
	slurp_into("cap3.txt", buf, sizeof buf);
	CHECK(strcmp(buf, "grouped") == 0);
}

/* ---- diagnostics: >0, on stderr, naming what is unsupported ---------- */

static void test_syntax_error(void)
{
	/* 2.8.1: a syntax error in a non-interactive shell exits >0, and
	 * it has to say so -- silently exiting nonzero is what makes a
	 * broken build script impossible to debug. */
	CHECK(run_c("| foo", 0) == 2);
	CHECK(err_contains("syntax error"));
	CHECK(out_is_empty());          /* diagnostics never go to stdout */
}

static void test_refuses_reserved_words(void)
{
	/* `case` is what is left of this list: the construct has no
	 * grammar, so the word still reaches the executor as a command
	 * name and would exit 127 about a fiction. */
	/* `case x in y) ;; esac` is rejected one layer earlier -- ')' is a
	 * lexer-level operator, so it is a *syntax* error and never
	 * reaches the refusal list.  `case word` is the form that parses
	 * cleanly and therefore the one that has to be refused by name. */
	CHECK(run_c("case foo", 0) == 2);
	CHECK(err_contains("case"));
	CHECK(out_is_empty());

	CHECK(run_c("esac", 0) == 2);
	CHECK(err_contains("case"));

	CHECK(run_c("case x in y) ;; esac", 0) == 2);
	CHECK(err_contains("syntax error"));

	/* A quoted word is not a reserved word: it is an ordinary command
	 * name that simply does not exist, so the honest answer is 127.
	 * XCU 2.10.1 rule 1's note -- "because at this point
	 * <quotation-mark> characters are retained in the token, quoted
	 * strings cannot be recognized as reserved words". */
	CHECK(run_c("'case'", 0) == 127);
	CHECK(run_c("'if'", 0) == 127);
	CHECK(run_c("'fi'", 0) == 127);

	/* `if`/`while`/`until`/`for` came off the refusal list when stage
	 * 6b gave them a grammar, so these now *run*: the command inside
	 * is what fails, with a true 127 about a name the script really
	 * did write.  A misplaced terminator is a syntax error (2) from
	 * the parser, which is what keeps the refusal property those words
	 * used to get from the list. */
	CHECK(run_c("for i in a b; do y; done", 0) == 127);
	/* 0, not 127: `x` is not found, so the condition is false and
	 * 2.9.4's "or zero, if none was executed" applies -- the 127 is
	 * the *condition's* status and must not become the command's. */
	CHECK(run_c("if x; then y; fi", 0) == 0);
	CHECK(run_c("while x; do y; done", 0) == 0);
	CHECK(run_c("if x; then y; else exit 4; fi", 0) == 4);
	CHECK(run_c("fi", 0) == 2);
	CHECK(err_contains("syntax error"));
	CHECK(run_c("done", 0) == 2);
	CHECK(err_contains("syntax error"));
}

/* The compound commands really work through the binary, not just
 * in-process: test/sh-engine.c drives __sh_exec_list() directly, and
 * "the engine is wired into sh.exe" is a separate claim. */
static void test_compound_commands_run(void)
{
	char cmd[1600], buf[256];

	CHECK(run_c("if true; then exit 3; fi", 0) == 3);
	CHECK(run_c("if false; then exit 3; else exit 4; fi", 0) == 4);
	CHECK(run_c("if false; then exit 3; fi", 0) == 0);
	CHECK(run_c("while false; do exit 9; done", 0) == 0);
	CHECK(run_c("until true; do exit 9; done", 0) == 0);
	CHECK(run_c("for f in a b c; do test \"$f\" = c; done", 0) == 0);
	CHECK(run_c("for f in a b c; do exit 7; done", 0) == 7);

	sprintf(cmd, "for f in a b; do '%s' --produce \"$f\"; done > cap5.txt", self);
	CHECK(run_c(cmd, 0) == 0);
	slurp_into("cap5.txt", buf, sizeof buf);
	CHECK(strcmp(buf, "ab") == 0);

	/* XCU 2.9.4: "for name" with no "in" list is "in \"$@\"".  Stage 6b
	 * refused this up front for want of positional parameters; stage 7
	 * has them, so it runs -- and with none set it runs the body zero
	 * times and exits 0, which is 2.9.4's "if there are no items, the
	 * exit status shall be zero" rather than the old refusal's 2. */
	CHECK(run_c("for f; do exit 9; done", 0) == 0);
}

static void test_refuses_unimplemented_builtins(void)
{
	/* The dangerous one: `export` as an external command would fail
	 * with 127 while the variable silently stayed unexported. */
	CHECK(run_c("export X=1", 0) == 2);
	CHECK(err_contains("export"));

	/* `export` is still on the list; `set` and `shift` came off it in
	 * stage 7 and must now *work* rather than merely stop being
	 * refused -- the difference between "the list shrank" and "the
	 * list shrank and the utility works". */
	CHECK(run_c("set -- a b c; test \"$#\" = 3", 0) == 0);
	CHECK(run_c("set -- a b c; shift; test \"$1\" = b", 0) == 0);
	/* An over-shift is nonzero *and* says so -- shift(1p)'s STDERR is
	 * "used only for diagnostic messages", and a silent nonzero status
	 * is what makes a broken script impossible to debug (2.8.1).  The
	 * status alone is also produced by src/sh/param.c's own range
	 * guard, so it cannot tell whether the built-in checked at all. */
	CHECK(run_c("set -- a; shift 5", 0) > 0);
	CHECK(err_contains("shift"));
	/* n exactly one past $# is the boundary the built-in's own range
	 * check is for: src/sh/param.c refuses it too, so the *status* is
	 * the same either way and only the diagnostic tells whether the
	 * built-in looked. */
	CHECK(run_c("set -- a b; shift 3", 0) > 0);
	CHECK(err_contains("shift"));

	/* A name comes off sh/main.c's refusal list exactly when
	 * src/sh/builtin.c grows a real implementation of it, and each one
	 * that has must *not* be caught by the list any more.  `exit 3`
	 * was refused with 2 until stage 6a; asserting it exits 3 now is
	 * the difference between "the list shrank" and "the list shrank
	 * and the utility works". */
	CHECK(run_c("cd .", 0) == 0);
	CHECK(run_c("exit 3", 0) == 3);
	CHECK(run_c(":", 0) == 0);
	CHECK(run_c("true", 0) == 0);
	CHECK(run_c("false", 0) == 1);
	CHECK(run_c("test 1 -eq 1", 0) == 0);
	CHECK(run_c("test 1 -eq 2", 0) == 1);
	CHECK(run_c("[ -d . ]", 0) == 0);
}

static void test_refuses_special_parameters(void)
{
	/* What is left of this refusal after stage 7.  $1..$9, ${10}, $@,
	 * $* and $# are expanded for real now (see
	 * test_positional_parameters_from_argv() below); the special
	 * parameters of XCU 2.5.2 that are still not implemented are
	 * refused, because a `$?` left in place as the two literal
	 * characters "$?" is silent corruption of a script's meaning
	 * rather than a missing feature it can notice. */
	CHECK(run_c("no-such-utility-xyzzy $!", 0) == 2);
	CHECK(err_contains("$!"));
	CHECK(run_c("no-such-utility-xyzzy $$", 0) == 2);
	CHECK(run_c("no-such-utility-xyzzy $-", 0) == 2);
	/* ${#NAME} is string length, a different expansion this shell does
	 * not implement -- it must stay refused rather than being
	 * mistaken for the ${#} that stage 7 does implement. */
	CHECK(run_c("no-such-utility-xyzzy ${#x}", 0) == 2);
	CHECK(run_c("no-such-utility-xyzzy > $!", 0) == 2);

	/* Single-quoted, it is literal text, not an expansion at all. */
	CHECK(run_c("no-such-utility-xyzzy '$!'", 0) == 127);

	/* $? came off this list in stage 7b and must now *work*, not
	 * merely stop being refused. */
	CHECK(run_c("false; test \"$?\" = 1", 0) == 0);
	CHECK(run_c("exit 7", 0) == 7);
}

/* XCU 2.9.5 through the binary: a function really is defined, called
 * with its own positional parameters, and can `return`.  test/
 * sh-engine.c drives the engine directly; that the *utility* wires it
 * up is a separate claim, and it is the one a script handed to
 * obj/sh/sh.exe depends on. */
static void test_functions_through_the_binary(void)
{
	char cmd[1600], buf[256];

	CHECK(run_c("f() { return 3; }; f", 0) == 3);
	CHECK(run_c("f() { test \"$1\" = hi; }; f hi", 0) == 0);
	CHECK(run_c("f() { test \"$1\" = hi; }; f no", 0) == 1);
	/* Recursion, and `for` with no `in` over the function's own
	 * arguments -- 2.9.4's "in \"$@\"" meeting 2.9.5's temporary
	 * positional parameters. */
	CHECK(run_c("r() { if test \"$#\" = 0; then return 5; fi; shift; r \"$@\"; }; "
	            "r a b c", 0) == 5);

	/* And the bytes, not just the status: a function that never ran
	 * its body exits 0 exactly like one that ran it correctly. */
	sprintf(cmd, "p() { for a; do '%s' --produce \"$a\"; done; }; p x y > cap6.txt", self);
	CHECK(run_c(cmd, 0) == 0);
	slurp_into("cap6.txt", buf, sizeof buf);
	CHECK(strcmp(buf, "xy") == 0);

	/* A definition whose body uses something the preflight refuses is
	 * refused at the definition, before anything runs -- the property
	 * sh/main.c's header calls refuse-before-running-anything. */
	unlink("preflight2.txt");
	sprintf(cmd, "'%s' --produce ran > preflight2.txt; f() { export X=1; }", self);
	CHECK(run_c(cmd, 0) == 2);
	CHECK(err_contains("export"));
	CHECK(slurp_into("preflight2.txt", buf, sizeof buf) != 0);
}

/* sh(1p) OPERANDS and XCU 2.5.1: "[p]ositional parameters are initially
 * assigned when the shell is invoked (see sh)."  This is the one thing
 * only the *binary* can be asked -- test/sh-engine.c has no argv to
 * install -- so which operand becomes $0 and which become $1 on is
 * tested here, for all three invocation forms. */
static void test_positional_parameters_from_argv(void)
{
	char *args[8];
	char script[1200];

	/* -c: "sh -c command_string [command_name [argument...]]" --
	 * command_name is $0 and the rest are $1 on.  `test` exits 1 for a
	 * false comparison and 0 for a true one, so a wrong value is a
	 * failed run rather than a silent pass. */
	args[0] = (char *)"sh";
	args[1] = (char *)"-c";
	args[2] = (char *)"test \"$0\" = zero && test \"$#\" = 2 && "
	                  "test \"$1\" = one && test \"$2\" = two";
	args[3] = (char *)"zero";
	args[4] = (char *)"one";
	args[5] = (char *)"two";
	args[6] = 0;
	CHECK(run_sh(args, 0) == 0);

	/* With no command_name at all there are no positional parameters
	 * and $0 stays the shell's own name -- what must not happen is the
	 * command_string itself becoming $0 or $1. */
	CHECK(run_c("test \"$#\" = 0", 0) == 0);

	/* An argument containing a space survives as one parameter: it
	 * arrives through argv, not through any re-splitting. */
	args[2] = (char *)"test \"$1\" = 'a b' && test \"$#\" = 1";
	args[3] = (char *)"zero";
	args[4] = (char *)"a b";
	args[5] = 0;
	CHECK(run_sh(args, 0) == 0);

	/* A script file: sh(1p) makes command_file "$0" and the operands
	 * after it $1 on. */
	sprintf(script, "test \"$#\" = 2 && test \"$1\" = alpha && "
	                "test \"$2\" = beta && test \"$0\" = script3.sh\n");
	write_file("script3.sh", script);
	args[0] = (char *)"sh";
	args[1] = (char *)"script3.sh";
	args[2] = (char *)"alpha";
	args[3] = (char *)"beta";
	args[4] = 0;
	CHECK(run_sh(args, 0) == 0);

	/* "-x" after the command_file is an operand, not an option: it is
	 * $2, and the shell must not have tried to interpret it. */
	args[2] = (char *)"alpha";
	args[3] = (char *)"-x";
	args[4] = 0;
	write_file("script3.sh", "test \"$2\" = -x && test \"$#\" = 2\n");
	CHECK(run_sh(args, 0) == 0);

	/* A script on standard input: every operand is a positional
	 * parameter, since there is no command_file to consume one. */
	write_file("script4.sh", "test \"$#\" = 2 && test \"$1\" = p && test \"$2\" = q\n");
	args[0] = (char *)"sh";
	args[1] = (char *)"-s";
	args[2] = (char *)"p";
	args[3] = (char *)"q";
	args[4] = 0;
	CHECK(run_sh(args, "script4.sh") == 0);
}

static void test_refuses_async(void)
{
	char cmd[1200];
	sprintf(cmd, "'%s' --exit-child 0 &", self);
	CHECK(run_c(cmd, 0) == 2);
	CHECK(err_contains("&"));
}

static void test_refuses_before_running_anything(void)
{
	/* The whole point of refusing at parse time: a program whose first
	 * half is runnable and whose second half is not must not have its
	 * first half executed. */
	char cmd[1600], buf[64];

	unlink("preflight.txt");
	sprintf(cmd, "'%s' --produce ran > preflight.txt; export X=1", self);
	CHECK(run_c(cmd, 0) == 2);
	CHECK(slurp_into("preflight.txt", buf, sizeof buf) != 0);
}

/* ---- argument handling (sh(1p) SYNOPSIS/OPERANDS) -------------------- */

static void test_usage_errors(void)
{
	char *args[3];

	args[0] = (char *)"sh"; args[1] = (char *)"-Z"; args[2] = 0;
	CHECK(run_sh(args, 0) == 2);
	CHECK(err_contains("usage"));

	args[0] = (char *)"sh"; args[1] = (char *)"-c"; args[2] = 0;
	CHECK(run_sh(args, 0) == 2);
}

static void test_command_name_becomes_dollar_zero(void)
{
	/* sh(1p): with -c, "command_name" is assigned to $0, which is what
	 * a shell prefixes its diagnostics with. */
	CHECK(run_c("case foo", "mybuild") == 2);
	CHECK(err_contains("mybuild: "));
}

static void test_script_file(void)
{
	char script[1200];
	char *args[5];

	sprintf(script, "'%s' --exit-child 5\n", self);
	write_file("script1.sh", script);

	args[0] = (char *)"sh"; args[1] = (char *)"script1.sh"; args[2] = 0;
	CHECK(run_sh(args, 0) == 5);

	/* Operands after command_file are the positional parameters. They
	 * are accepted and, since no supported program text can reference
	 * one (see sh/main.c), otherwise unused -- what must not happen is
	 * them being mistaken for more options or for another file. */
	args[2] = (char *)"alpha"; args[3] = (char *)"-x"; args[4] = 0;
	CHECK(run_sh(args, 0) == 5);

	/* "--" ends option parsing. */
	args[1] = (char *)"--"; args[2] = (char *)"script1.sh"; args[3] = 0;
	CHECK(run_sh(args, 0) == 5);

	/* An unopenable command_file: >0, with a diagnostic. */
	args[1] = (char *)"no-such-script.sh"; args[2] = 0;
	CHECK(run_sh(args, 0) == 127);
	CHECK(err_contains("no-such-script.sh"));
}

static void test_stdin_script(void)
{
	char script[2400];
	char *args[3];

	sprintf(script, "# a script on stdin\n'%s' --produce fromstdin > cap4.txt\n'%s' --exit-child 9\n",
		self, self);
	write_file("script2.sh", script);

	args[0] = (char *)"sh"; args[1] = 0;
	CHECK(run_sh(args, "script2.sh") == 9);

	/* -s asks for the same thing explicitly. */
	args[1] = (char *)"-s"; args[2] = 0;
	CHECK(run_sh(args, "script2.sh") == 9);

	{
		char buf[64];
		slurp_into("cap4.txt", buf, sizeof buf);
		CHECK(strcmp(buf, "fromstdin") == 0);
	}
}

/* Every fixture and capture file this suite creates, removed on the way
 * out.  tools/run-tests.py gives each test its own mktemp -d working
 * directory, so leaving them behind costs that path nothing -- but a
 * developer running obj/test/sh-main.exe straight out of the checkout
 * (which is how it gets debugged) drops eight untracked files into the
 * repository root, and the gate's `reuse` stage then fails the whole
 * tree for files with no SPDX header.  A test that turns an unrelated
 * gate stage red depending on where it was run from is a test that
 * cannot be trusted to mean what it says, so it cleans up after itself
 * rather than relying on the directory it happens to be in. */
static void cleanup_artifacts(void)
{
	static const char *const files[] = {
		OUTFILE, ERRFILE,
		"cap1.txt", "cap2.txt", "cap3.txt", "cap4.txt",
		"cap5.txt", "preflight.txt", "script1.sh", "script2.sh",
		"script3.sh", "script4.sh", "cap6.txt", "preflight2.txt",
		0
	};
	size_t i;
	for (i = 0; files[i]; i++) unlink(files[i]);
}

/* ---- child roles ------------------------------------------------------
 *
 * argv[1] selects a role, in which case main() below does that instead
 * of running the suite -- see this file's header, and test/sh-engine.c's
 * child_role() for the same idea. */
static int child_role(int argc, char **argv)
{
	if (!strcmp(argv[1], "--exit-child") && argc > 2)
		return atoi(argv[2]);
	if (!strcmp(argv[1], "--produce") && argc > 2) {
		fputs(argv[2], stdout);
		fflush(stdout);
		return 0;
	}
	if (!strcmp(argv[1], "--cat")) {
		char buf[512];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) fwrite(buf, 1, n, stdout);
		fflush(stdout);
		return 0;
	}
	return -1;
}

int main(int argc, char **argv)
{
	if (argc > 1) { int r = child_role(argc, argv); if (r >= 0) return r; }

	self = argv[0];
	if (find_sh(argv[0]) != 0 || access(sh_path, R_OK) != 0) {
		/* 77 is this suite's "ran, but declined to check something"
		 * status (tools/run-tests.py): without the binary there is
		 * nothing here to test, and reporting a pass would be a lie. */
		printf("SKIP sh-main: cannot locate the sh binary (tried \"%s\" from argv[0] \"%s\")\n",
			sh_path, argv[0] ? argv[0] : "(null)");
		return 77;
	}

	test_status_is_last_command();
	test_empty_program_is_zero();
	test_andor_and_not_found();

	test_redirection_and_pipeline();

	test_syntax_error();
	test_refuses_reserved_words();
	test_compound_commands_run();
	test_refuses_unimplemented_builtins();
	test_refuses_special_parameters();
	test_positional_parameters_from_argv();
	test_functions_through_the_binary();
	test_refuses_async();
	test_refuses_before_running_anything();

	test_usage_errors();
	test_command_name_becomes_dollar_zero();
	test_script_file();
	test_stdin_script();

	cleanup_artifacts();

	if (fails) { printf("sh-main: failures: %d\n", fails); return 1; }
	printf("sh-main: all ok (the sh binary: -c, script file, stdin, exit status, diagnostics)\n");
	return 0;
}
