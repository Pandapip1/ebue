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
 * than taking a path from the environment or a -D. tools/runtests.sh
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
	CHECK(run_c("if x; then y; fi", 0) == 2);
	CHECK(err_contains("if"));
	CHECK(out_is_empty());

	CHECK(run_c("for i in a b; do y; done", 0) == 2);
	CHECK(err_contains("for"));

	/* A quoted word is not a reserved word: it is an ordinary command
	 * name that simply does not exist, so the honest answer is 127. */
	CHECK(run_c("'if'", 0) == 127);
}

static void test_refuses_unimplemented_builtins(void)
{
	CHECK(run_c("exit 3", 0) == 2);
	CHECK(err_contains("exit"));

	/* The dangerous one: `export` as an external command would fail
	 * with 127 while the variable silently stayed unexported. */
	CHECK(run_c("export X=1", 0) == 2);
	CHECK(err_contains("export"));

	/* `cd` *is* implemented (src/sh/exec.c), so it must not be caught
	 * by the refusal list. */
	CHECK(run_c("cd .", 0) == 0);
}

static void test_refuses_special_parameters(void)
{
	/* src/wordexp/wordexp.c expands only $NAME/${NAME}; "$1" would
	 * otherwise reach the command as the two literal characters. */
	CHECK(run_c("no-such-utility-xyzzy \"$1\"", 0) == 2);
	CHECK(err_contains("$1"));
	CHECK(run_c("no-such-utility-xyzzy $@", 0) == 2);
	CHECK(run_c("no-such-utility-xyzzy ${#x}", 0) == 2);
	CHECK(run_c("no-such-utility-xyzzy > $?", 0) == 2);

	/* Single-quoted, it is literal text, not an expansion at all. */
	CHECK(run_c("no-such-utility-xyzzy '$1'", 0) == 127);
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
	CHECK(run_c("if x; then y; fi", "mybuild") == 2);
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
		 * status (tools/runtests.sh): without the binary there is
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
	test_refuses_unimplemented_builtins();
	test_refuses_special_parameters();
	test_refuses_async();
	test_refuses_before_running_anything();

	test_usage_errors();
	test_command_name_becomes_dollar_zero();
	test_script_file();
	test_stdin_script();

	if (fails) { printf("sh-main: failures: %d\n", fails); return 1; }
	printf("sh-main: all ok (the sh binary: -c, script file, stdin, exit status, diagnostics)\n");
	return 0;
}
