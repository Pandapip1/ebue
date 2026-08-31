/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Black-box tests for the first tier of ntlibc's POSIX standard
 * utilities: `true`, `false` and `test`/`[` (XCU true(1p), false(1p),
 * test(1p)).  Grouped into one file rather than three, the way
 * src/sh/builtin.c itself groups them as "the trivial four" -- their
 * logic is small enough, and their whole point here is proving the
 * shared-core architecture (src/internal/util.h) rather than exercising
 * a large surface each.
 *
 * Two things get checked for each utility: the standalone obj/bin/*.exe
 * (spawned as a real process, same technique test/sh-main.c uses for
 * obj/sh/sh.exe -- see that file's header for why a second process is
 * unavoidable here) and the shell built-in (via `obj/sh/sh.exe -c`),
 * confirming both callers of __util_*_main() (src/internal/util.h)
 * agree, not just that one of them works.
 *
 * `test`'s `[` spelling is exercised without a separate obj/bin/[.exe
 * (there isn't one -- see bin/test.c's own comment on why): __spawn()
 * takes the child's argv[0] independently of the path being executed,
 * exactly like execve() does, so obj/bin/test.exe run with argv[0] "["
 * and a trailing "]" argument exercises __util_test_main()'s bracket
 * check for real, the same as an actual [.exe would.
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

static char obj_root[1024];

/* obj/test/util-trivial.exe -> obj -- same walk-up-from-argv[0] technique
 * as test/sh-main.c's find_sh(), generalized to stop one level higher so
 * both obj/bin/*.exe and obj/sh/sh.exe are reachable from it. */
static int find_obj_root(const char *argv0)
{
	size_t n;
	char *p;

	if (!argv0 || !*argv0) return -1;
	n = strlen(argv0);
	if (n >= sizeof obj_root) return -1;
	strcpy(obj_root, argv0);

	for (p = obj_root + n; p > obj_root; p--)
		if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == obj_root) return -1;
	p[-1] = 0;                       /* strip "/util-trivial.exe" */

	for (p = obj_root + strlen(obj_root); p > obj_root; p--)
		if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == obj_root) return -1;
	p[-1] = 0;                       /* strip "/test" */

	return 0;
}

static void path_for(char *out, size_t outlen, const char *rel)
{
	char sep = strchr(obj_root, '\\') ? '\\' : '/';
	char relcopy[256], *p;

	strncpy(relcopy, rel, sizeof relcopy - 1);
	relcopy[sizeof relcopy - 1] = 0;
	if (sep == '\\')
		for (p = relcopy; *p; p++) if (*p == '/') *p = '\\';
	snprintf(out, outlen, "%s%c%s", obj_root, sep, relcopy);
}

#define OUTFILE "util-trivial-out.txt"
#define ERRFILE "util-trivial-err.txt"

/* Spawns `path` with the given argv (argv[0] supplied by the caller, so
 * a caller can exercise a program under an argv[0] other than its real
 * path -- see this file's header on how that stands in for a missing
 * obj/bin/[.exe), capturing stdout/stderr to fixed files exactly the
 * way test/sh-main.c's run_sh() does. Returns the exit status, or -1 if
 * the child could not be started at all. */
static int run(const char *path, char *const *args)
{
	int out, err;
	int s1, s2, pid, status;

	out = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	err = open(ERRFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (out < 0 || err < 0) { if (out >= 0) close(out); if (err >= 0) close(err); return -1; }

	s1 = dup(1); s2 = dup(2);
	dup2(out, 1);
	dup2(err, 2);
	close(out); close(err);

	pid = __spawn(path, args, environ);

	dup2(s1, 1); close(s1);
	dup2(s2, 2); close(s2);

	if (pid < 0) return -1;
	if (waitpid(pid, &status, 0) != pid) return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
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

static char true_path[1024], false_path[1024], test_path[1024], sh_path[1024];

/* ---- true(1p) / false(1p) --------------------------------------------- */

static void test_true_standalone(void)
{
	char *argv[] = { (char *)"true", 0 };
	CHECK(run(true_path, argv) == 0);
}

static void test_true_ignores_arguments(void)
{
	/* true(1p): "... ignoring its arguments" -- garbage operands must
	 * not change the outcome or produce a diagnostic. */
	char *argv[] = { (char *)"true", (char *)"-x", (char *)"--help", (char *)"anything", 0 };
	CHECK(run(true_path, argv) == 0);
	CHECK(!err_contains(":"));
}

static void test_false_standalone(void)
{
	char *argv[] = { (char *)"false", 0 };
	CHECK(run(false_path, argv) != 0);
}

/* ---- test(1p) / [(1p) ---------------------------------------------- */

static void test_test_argc_cases(void)
{
	char *a0[] = { (char *)"test", 0 };
	char *a1t[] = { (char *)"test", (char *)"nonempty", 0 };
	char *a1f[] = { (char *)"test", (char *)"", 0 };
	char *a2neg_null[] = { (char *)"test", (char *)"!", (char *)"", 0 };
	char *a2unary[] = { (char *)"test", (char *)"-n", (char *)"x", 0 };
	char *a3eq[] = { (char *)"test", (char *)"foo", (char *)"=", (char *)"foo", 0 };
	char *a3ne[] = { (char *)"test", (char *)"foo", (char *)"=", (char *)"bar", 0 };

	/* "0 arguments: Exit false (1)." */
	CHECK(run(test_path, a0) == 1);
	/* "1 argument: Exit true (0) if $1 is not null; otherwise, exit
	 * false." */
	CHECK(run(test_path, a1t) == 0);
	CHECK(run(test_path, a1f) == 1);
	/* "If $1 is '!', exit true if $2 is null, false if $2 is not
	 * null." */
	CHECK(run(test_path, a2neg_null) == 0);
	/* 2-argument unary primary. */
	CHECK(run(test_path, a2unary) == 0);
	/* 3-argument binary primary. */
	CHECK(run(test_path, a3eq) == 0);
	CHECK(run(test_path, a3ne) == 1);
}

static void test_test_malformed_is_diagnosed(void)
{
	/* ">1 An error occurred" -- 2, with a diagnostic, never a silent
	 * false. */
	char *argv[] = { (char *)"test", (char *)"-Q", (char *)"x", 0 };
	CHECK(run(test_path, argv) == 2);
	CHECK(err_contains("test:"));
}

static void test_bracket_form(void)
{
	/* No standalone [.exe exists (see this file's header) -- exercise
	 * __util_test_main()'s "[" branch by spawning obj/bin/test.exe
	 * under argv[0] "[", the same way a real [.exe would be invoked. */
	char *ok[] = { (char *)"[", (char *)"1", (char *)"=", (char *)"1", (char *)"]", 0 };
	char *missing[] = { (char *)"[", (char *)"1", (char *)"=", (char *)"1", 0 };

	CHECK(run(test_path, ok) == 0);
	CHECK(run(test_path, missing) == 2);
	CHECK(err_contains("missing"));
}

/* ---- the shell built-ins agree with the standalone executables ------- */

static int run_sh_c(const char *cmd)
{
	char *argv[] = { (char *)"sh", (char *)"-c", (char *)cmd, 0 };
	return run(sh_path, argv);
}

static void test_builtins_match_standalone(void)
{
	CHECK(run_sh_c("true") == 0);
	CHECK(run_sh_c("false") != 0);
	CHECK(run_sh_c("test 1 = 1") == 0);
	CHECK(run_sh_c("test 1 = 2") == 1);
	CHECK(run_sh_c("[ -n x ]") == 0);
}

static void cleanup_artifacts(void)
{
	unlink(OUTFILE);
	unlink(ERRFILE);
}

int main(int argc, char **argv)
{
	(void)argc;

	if (find_obj_root(argv[0]) != 0) {
		printf("SKIP util-trivial: cannot locate obj/ from argv[0] \"%s\"\n",
			argv[0] ? argv[0] : "(null)");
		return 77;
	}
	path_for(true_path, sizeof true_path, "bin/true.exe");
	path_for(false_path, sizeof false_path, "bin/false.exe");
	path_for(test_path, sizeof test_path, "bin/test.exe");
	path_for(sh_path, sizeof sh_path, "sh/sh.exe");

	if (access(true_path, R_OK) != 0 || access(false_path, R_OK) != 0 ||
	    access(test_path, R_OK) != 0 || access(sh_path, R_OK) != 0) {
		printf("SKIP util-trivial: one or more of true/false/test/sh binaries is missing\n");
		return 77;
	}

	test_true_standalone();
	test_true_ignores_arguments();
	test_false_standalone();

	test_test_argc_cases();
	test_test_malformed_is_diagnosed();
	test_bracket_form();

	test_builtins_match_standalone();

	cleanup_artifacts();

	if (fails) { printf("util-trivial: failures: %d\n", fails); return 1; }
	printf("util-trivial: all ok (true, false, test/[ -- standalone and builtin)\n");
	return 0;
}
