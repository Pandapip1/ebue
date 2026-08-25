/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX's [ENOEXEC] shell fallback: can a shell script be executed at
 * all on this system, and does the thing that runs it get the arguments
 * the standard says it does.
 *
 * Two clauses, one mechanism, both tested here:
 *
 *   - XSH exec.html DESCRIPTION: "In the cases where the other members
 *     of the exec family of functions would fail and set errno to
 *     [ENOEXEC], the execlp() and execvp() functions shall execute a
 *     command interpreter and the environment of the executed command
 *     shall be as if the process invoked the sh utility using execl()
 *     as follows:  execl(<shell path>, arg0, file, arg1, ...,
 *     (char *)0);"   (src/process/exec.c's shell_fallback().)
 *
 *   - XCU 2.9.1 Command Search and Execution: "If the execl() function
 *     fails due to an error equivalent to the [ENOEXEC] error ... the
 *     shell shall execute a command equivalent to having a shell
 *     invoked with the pathname resulting from the search as its first
 *     operand, with any remaining arguments passed to the new shell",
 *     and, for a command name containing a <slash>, the same fallback
 *     "with the command name as its first operand".
 *     (src/sh/exec.c's spawn_interpreted().)
 *
 * Why this is not a conformance nicety on NT.  RtlCreateUserProcess
 * cannot start a script image -- it returns STATUS_INVALID_IMAGE_NOT_MZ
 * or STATUS_INVALID_IMAGE_FORMAT, which src/process/spawn.c maps to
 * ENOEXEC -- so before this fallback existed there was no route by
 * which a shell script could be executed on this platform.  XRAT (XCU
 * C.2.9.1) names that as the clause's purpose: it "requires that the
 * shell can execute shell scripts directly, even if the underlying
 * system does not support the common #! interpreter convention".
 *
 * ---- What is asserted, and what would make it vacuous ---------------
 *
 * The failure mode this file is written against is a test that never
 * reaches the fallback and passes anyway: `execvp()` returning 0 proves
 * nothing here, because execvp() on this platform *always* returns by
 * not returning (src/process/exec.c: execve() spawns, waits, and
 * _exit()s with the child's status), and a status of 0 is what a
 * do-nothing path would also produce.
 *
 * So every case asserts on a *side effect the script itself produced*:
 * the script's only command is a copy of this binary in a child role
 * that writes a marker file naming its own arguments and exits 7.  A
 * missing marker file fails the case even if the exit status looks
 * right; a marker file with the wrong contents fails it even if the
 * script ran.  The interpreter cannot fake either one.
 *
 * ---- Where the interpreter comes from -------------------------------
 *
 * src/process/interpreter.c looks beside the calling image first and on
 * PATH second, and the two cases below are built so that each mechanism
 * is the *only* one that can work in its case: case A puts obj/sh on
 * PATH and no sh.exe beside the caller, case B does the reverse.  A
 * single case covering both would pass with either mechanism broken.
 *
 * The caller in every case is a copy of this binary made in the test's
 * own working directory, not obj/test/exec-script.exe, precisely so
 * that "beside the calling image" is a directory this file controls.
 *
 * Runtime note: the [ENOEXEC] itself originates in
 * RtlCreateUserProcess, so the real-Windows legs are its authority --
 * Wine is sound for the search and the argv construction but not for
 * that boundary.  Nothing here is fenced or profile-limited: if a
 * runtime were to start executing scripts natively the spawn would
 * succeed instead, the fallback would not be reached, and the marker
 * file would still say the script ran with the right arguments.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char **environ;
int __spawn(const char *path, char *const argv[], char *const envp[]);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define RC_MARK 7          /* the script's last (only) command's status */
#define CHILD "es-child.exe"
#define SCRIPT "es-script.sh"
#define PATHDIR "es-path"     /* the only PATH entry; deliberately holds no sh.exe */

/* ---- roles ----------------------------------------------------------
 *
 * --mark FILE ARG...   write FILE, one argument per line, exit RC_MARK.
 *                      This is what the script runs; its output is the
 *                      proof the script ran and with what.
 * --run NAME ARG...    execvp(NAME, {NAME, ARG...}).  Only returns on
 *                      failure, in which case exit 100+errno.
 */
static int role_mark(int argc, char **argv)
{
	FILE *f = fopen(argv[2], "w");
	int i;
	if (!f) return 90;
	for (i = 3; i < argc; i++) fprintf(f, "%s\n", argv[i]);
	return fclose(f) ? 91 : RC_MARK;
}

static int role_run(int argc, char **argv)
{
	(void)argc;
	errno = 0;
	execvp(argv[2], argv + 2);
	return 100 + errno;
}

/* ---- helpers --------------------------------------------------------- */

static int copyfile(const char *src, const char *dst)
{
	char buf[8192];
	int in, out;
	ssize_t n;
	in = open(src, O_RDONLY);
	if (in < 0) return -1;
	out = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (out < 0) { close(in); return -1; }
	while ((n = read(in, buf, sizeof buf)) > 0)
		if (write(out, buf, (size_t)n) != n) { close(in); close(out); return -1; }
	close(in);
	return close(out) < 0 || n < 0 ? -1 : 0;
}

static int writefile(const char *p, const char *data)
{
	size_t n = strlen(data);
	int fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) return -1;
	if (n && write(fd, data, n) != (ssize_t)n) { close(fd); return -1; }
	return close(fd);
}

/* Read a whole file into a fixed buffer; returns 0 on success. */
static int slurp(const char *p, char *buf, size_t cap)
{
	int fd = open(p, O_RDONLY);
	ssize_t n;
	if (fd < 0) return -1;
	n = read(fd, buf, cap - 1);
	close(fd);
	if (n < 0) return -1;
	buf[n] = 0;
	return 0;
}

static int run(char *const *args)
{
	int pid = __spawn(args[0], args, environ), status;
	if (pid < 0) return -1;
	if (waitpid(pid, &status, 0) < 0) return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
}

/* obj/test/exec-script.exe -> obj/sh/sh.exe, keeping argv[0]'s own
 * separator.  Same walk as test/sh-main.c's find_sh(), and for the same
 * reason: the tests run from a private temporary directory, so nothing
 * relative to the cwd resolves, and argv[0] is the only path that is
 * true under both Wine and the real-Windows legs. */
static int find_sh(const char *argv0, char *out, size_t cap)
{
	size_t n;
	char sep;
	char *p;

	if (!argv0 || !*argv0) return -1;
	n = strlen(argv0);
	if (n + 16 >= cap) return -1;
	strcpy(out, argv0);
	for (p = out + n; p > out; p--) if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == out) return -1;
	sep = p[-1];
	p[-1] = 0;                                  /* strip "/exec-script.exe" */
	for (p = out + strlen(out); p > out; p--) if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == out) return -1;
	p[-1] = 0;                                  /* strip "/test" */
	sprintf(out + strlen(out), "%csh%csh.exe", sep, sep);
	return 0;
}

/* Every case ends here: the marker file must exist, and must name
 * exactly the arguments the clause says the script's command was to be
 * given.  `want0` is matched as a suffix because $0 is the *resolved*
 * pathname (an absolute one for a PATH search, "./" + name for the
 * <slash> form), which the clause fixes only up to naming the file. */
static void check_marker(const char *file, const char *want0,
                         const char *want1, const char *want2, int want_resolved)
{
	char buf[2048];
	char *line[3], *p;
	int i;

	if (slurp(file, buf, sizeof buf) < 0) {
		fails++;
		printf("FAIL %s: the script did not run -- no marker file %s\n", __FILE__, file);
		return;
	}
	for (i = 0, p = buf; i < 3; i++) {
		char *nl = strchr(p, '\n');
		if (!nl) break;
		*nl = 0;
		line[i] = p;
		p = nl + 1;
	}
	if (i < 3) {
		fails++;
		printf("FAIL %s: marker %s has %d lines, wanted 3\n", __FILE__, file, i);
		unlink(file);
		return;
	}
	/* $0 by suffix: the clause fixes it only up to naming the file (an
	 * absolute pathname for the PATH search, "./name" for the <slash>
	 * form), so what is asserted is that it ends in the name given. */
	{
		size_t l0 = strlen(line[0]), lw = strlen(want0);
		int ok0 = l0 >= lw && strcmp(line[0] + l0 - lw, want0) == 0;
		CHECK(ok0);
		if (!ok0) printf("note: %s $0 was \"%s\", wanted a name ending \"%s\"\n",
		                 file, line[0], want0);
	}
	/* And, for the PATH-search cases, that it is the *resolved* path and
	 * not the bare name the caller passed.  This is the assertion that
	 * fails if the fallback hands the shell `file` as given: the shell's
	 * command_file operand is opened relative to the shell's own current
	 * directory, so a bare name would be resolved a second time, by
	 * different rules, against a different place than the search found
	 * it in.  exec.html says "file is the process image file". */
	if (want_resolved) {
		int abs0 = strchr(line[0], '/') != 0 || strchr(line[0], '\\') != 0;
		CHECK(abs0);
		if (!abs0) printf("note: %s $0 was the bare name \"%s\", not the resolved path\n",
		                  file, line[0]);
	}
	CHECK(strcmp(line[1], want1) == 0);
	CHECK(strcmp(line[2], want2) == 0);
	if (strcmp(line[1], want1) || strcmp(line[2], want2))
		printf("note: %s operands were \"%s\" \"%s\", wanted \"%s\" \"%s\"\n",
		       file, line[1], line[2], want1, want2);
	unlink(file);
}

int main(int argc, char **argv)
{
	char self[1024], shexe[1024], cwd[1024], pathbuf[2600], pathdir[1100];
	char script[512];
	char *av[8];
	int st;

	if (argc > 2 && !strcmp(argv[1], "--mark")) return role_mark(argc, argv);
	if (argc > 2 && !strcmp(argv[1], "--run")) return role_run(argc, argv);

	if (!argv[0] || strlen(argv[0]) >= sizeof self) { printf("FAIL argv[0]\n"); return 1; }
	strcpy(self, argv[0]);
	if (find_sh(self, shexe, sizeof shexe) < 0) { printf("FAIL find_sh\n"); return 1; }
	if (access(shexe, F_OK) != 0) { printf("FAIL no %s\n", shexe); return 1; }
	if (!getcwd(cwd, sizeof cwd)) { printf("FAIL getcwd\n"); return 1; }

	if (copyfile(self, CHILD) < 0) { printf("FAIL copy self\n"); return 1; }

	/* The script.  Its first line is the #! NT cannot honour -- which
	 * is the whole point -- and is a comment to the shell that ends up
	 * reading it.  Its one command writes the marker naming $0, $1, $2,
	 * so the marker proves both that the script ran and that the
	 * operands arrived in the order the clauses specify. */
	snprintf(script, sizeof script,
	         "#!/bin/sh\n./%s --mark \"$1\" \"$0\" \"$2\" \"$3\"\n", CHILD);
	if (writefile(SCRIPT, script) < 0) { printf("FAIL write script\n"); return 1; }
	/* A second copy under a directory that is the *only* PATH entry the
	 * cases below use.  Keeping the PATH copy out of the cwd is what
	 * makes case B a real test of the beside-the-image mechanism: if
	 * the cwd were on PATH, the sh.exe copied into it for that case
	 * would also be reachable by the PATH search, and either mechanism
	 * alone would pass. */
	if (mkdir(PATHDIR, 0755) < 0) { printf("FAIL mkdir " PATHDIR "\n"); return 1; }
	if (writefile(PATHDIR "/" SCRIPT, script) < 0) { printf("FAIL write path script\n"); return 1; }
	snprintf(pathdir, sizeof pathdir, "%s\\" PATHDIR, cwd);

	/* ============================================================
	 * A. execvp(), interpreter found on PATH.
	 *
	 * PATH holds the cwd (so the script itself is found by the search)
	 * and obj/sh (so "sh" is).  No sh.exe exists beside the calling
	 * image, so interpreter_path()'s first candidate must miss and the
	 * PATH candidate must be the one that works.
	 * ============================================================ */
	{
		char shdir[1024];
		char *q;
		strcpy(shdir, shexe);
		for (q = shdir + strlen(shdir); q > shdir; q--)
			if (q[-1] == '/' || q[-1] == '\\') { q[-1] = 0; break; }
		snprintf(pathbuf, sizeof pathbuf, "%s;%s", pathdir, shdir);
	}
	CHECK(setenv("PATH", pathbuf, 1) == 0);

	av[0] = CHILD; av[1] = (char *)"--run"; av[2] = (char *)SCRIPT;
	av[3] = (char *)"out-a.txt"; av[4] = (char *)"A1"; av[5] = (char *)"A2"; av[6] = 0;
	st = run(av);
	CHECK(st == RC_MARK);
	if (st != RC_MARK && st >= 100)
		printf("note: case A execvp failed, errno %d\n", st - 100);
	check_marker("out-a.txt", SCRIPT, "A1", "A2", 1);

	/* ============================================================
	 * B. execvp(), interpreter found beside the calling image.
	 *
	 * PATH now holds only the cwd, so no "sh" is reachable through it;
	 * the only sh.exe is the copy made next to CHILD.  This is the
	 * installed layout (`make install` puts sh.exe and every other
	 * program in one $bindir) and the mechanism that does not depend on
	 * PATH at all.
	 * ============================================================ */
	CHECK(copyfile(shexe, "sh.exe") == 0);
	CHECK(setenv("PATH", pathdir, 1) == 0);

	av[3] = (char *)"out-b.txt"; av[4] = (char *)"B1"; av[5] = (char *)"B2";
	st = run(av);
	CHECK(st == RC_MARK);
	if (st != RC_MARK && st >= 100)
		printf("note: case B execvp failed, errno %d\n", st - 100);
	check_marker("out-b.txt", SCRIPT, "B1", "B2", 1);

	/* ============================================================
	 * C. execvp() on a name containing a <slash>: no PATH search
	 * happens at all, so this reaches __spawn() and [ENOEXEC] directly.
	 * exec.html scopes the fallback by which function was called, not
	 * by how the name resolved -- "the execlp() and execvp() functions
	 * shall execute a command interpreter", with no exception for a
	 * file argument containing a <slash>.
	 * ============================================================ */
	av[2] = (char *)"./" SCRIPT;
	av[3] = (char *)"out-c.txt"; av[4] = (char *)"C1"; av[5] = (char *)"C2";
	st = run(av);
	CHECK(st == RC_MARK);
	if (st != RC_MARK && st >= 100)
		printf("note: case C execvp failed, errno %d\n", st - 100);
	check_marker("out-c.txt", "./" SCRIPT, "C1", "C2", 0);

	/* ============================================================
	 * D. The shell's own command search (XCU 2.9.1), PATH branch:
	 * "a shell invoked with the pathname resulting from the search as
	 * its first operand, with any remaining arguments passed to the new
	 * shell".  PATH still holds only the cwd, and sh.exe is beside
	 * itself there, so the shell re-invokes its own image.
	 * ============================================================ */
	{
		char cmd[512];
		snprintf(cmd, sizeof cmd, "%s out-d.txt D1 D2", SCRIPT);
		av[0] = (char *)"sh.exe"; av[1] = (char *)"-c"; av[2] = cmd; av[3] = 0;
		st = run(av);
		CHECK(st == RC_MARK);
		if (st != RC_MARK) printf("note: case D sh -c exited %d\n", st);
		check_marker("out-d.txt", SCRIPT, "D1", "D2", 1);

		/* E. The same clause's <slash> branch: "a shell invoked with
		 * the command name as its first operand". */
		snprintf(cmd, sizeof cmd, "./%s out-e.txt E1 E2", SCRIPT);
		av[2] = cmd;
		st = run(av);
		CHECK(st == RC_MARK);
		if (st != RC_MARK) printf("note: case E sh -c exited %d\n", st);
		check_marker("out-e.txt", "./" SCRIPT, "E1", "E2", 0);
	}

	unlink(CHILD);
	unlink("sh.exe");
	unlink(SCRIPT);
	unlink(PATHDIR "/" SCRIPT);
	rmdir(PATHDIR);

	if (fails) { printf("exec-script: failures: %d\n", fails); return 1; }
	printf("exec-script: all tests passed\n");
	return 0;
}
