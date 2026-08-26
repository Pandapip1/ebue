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
 *     (src/sh/exec.c's run_interpreted().)
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
 * Nowhere on the filesystem, and that is the property these cases are
 * built to hold on to.  The interpreter is __sh_run_script()
 * (src/sh/script.c), the sh(1p) utility called as a function inside
 * libc.a; there is no sh.exe to find, no PATH to search, and therefore
 * nothing a layout or an environment can take away.
 *
 * The earlier design did search -- beside the calling image first, PATH
 * second -- and both halves of it are actively poisoned here rather
 * than merely absent, because "no external interpreter is consulted" is
 * a claim a test can only make by making the consultation fail loudly.
 * Setup writes a decoy `sh.exe` in BOTH former candidate directories:
 * beside the calling image (the cwd, which is where the child copy
 * runs from) and in the one PATH entry.  Each decoy is a text file, so
 * it is exactly what NT cannot start -- any spawn of it comes back
 * STATUS_INVALID_IMAGE_NOT_MZ.  If a future change reintroduces either
 * search, the interpreter it finds cannot run, the marker file is never
 * written, and the case fails.  A passing run therefore says the script
 * ran with no usable external sh anywhere in reach.
 *
 * The caller in the execvp() cases is a copy of this binary made in the
 * test's own working directory, not obj/test/exec-script.exe, so that
 * "beside the calling image" is a directory this file controls -- which
 * is what lets the decoy be placed there at all.
 *
 * Cases D and E, the shell's half of the clause, drive src/sh/exec.c's
 * command search directly through __sh_parse()/__sh_exec_list() (the
 * same entry points test/sh-engine.c uses) rather than by running
 * obj/sh/sh.exe.  Same reason: the subject is the [ENOEXEC] branch of
 * 2.9.1, which lives in libc.a, and reaching it through a second
 * process would put an external binary back in the dependency list of
 * a file whose whole point is that there is not one.  The sh *utility's*
 * own argument handling is test/sh-main.c's subject, not this file's.
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
#include "../src/sh/sh.h"

extern char **environ;
int __spawn(const char *path, char *const argv[], char *const envp[]);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define RC_MARK 7          /* the script's last (only) command's status */
#define CHILD "es-child.exe"
#define SCRIPT "es-script.sh"
#define PATHDIR "es-path"     /* the only PATH entry; holds a decoy sh.exe */

/* Not an NT image, on purpose: see the header.  Any attempt to spawn
 * this as an interpreter fails, which is how a reintroduced search
 * makes itself visible instead of silently working. */
#define DECOY "#!/bin/sh\nexit 99\n"

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

/* Mode 0755, not 0644: on NT there is no execute-permission bit distinct
 * from read/write to begin with, so this is invisible to RtlCreateUserProcess
 * itself -- but fuzz/ntstubs.c's native stand-in for it is not the real
 * thing.  It answers with a real host process (see its own header comment),
 * and to get "no such program" back to the caller *before* forking a doomed
 * child it first probes access(host, X_OK) and turns a failure into
 * STATUS_OBJECT_NAME_NOT_FOUND -- i.e. [ENOENT], not [ENOEXEC].  A file this
 * test wrote at 0644 fails that host-only, Unix-only probe for a reason NT
 * has no concept of, so under that one build every case here would come
 * back "no such file" before ever reaching the image-format check the
 * clause is about -- see test/posix-unistd-exec.c's ex-text.sh, which
 * already writes at 0755 for exactly this reason. */
static int copyfile(const char *src, const char *dst)
{
	char buf[8192];
	int in, out;
	ssize_t n;
	in = open(src, O_RDONLY);
	if (in < 0) return -1;
	out = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0755);
	if (out < 0) { close(in); return -1; }
	while ((n = read(in, buf, sizeof buf)) > 0)
		if (write(out, buf, (size_t)n) != n) { close(in); close(out); return -1; }
	close(in);
	return close(out) < 0 || n < 0 ? -1 : 0;
}

static int writefile(const char *p, const char *data)
{
	size_t n = strlen(data);
	int fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0755);
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
	char self[1024], cwd[1024], pathdir[1100];
	char script[512];
	char *av[8];
	int st;

	if (argc > 2 && !strcmp(argv[1], "--mark")) return role_mark(argc, argv);
	if (argc > 2 && !strcmp(argv[1], "--run")) return role_run(argc, argv);

	if (!argv[0] || strlen(argv[0]) >= sizeof self) { printf("FAIL argv[0]\n"); return 1; }
	strcpy(self, argv[0]);
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

	/* The two decoys.  One beside the calling image (the cwd, where the
	 * CHILD copy runs from), one in the only PATH entry -- the two
	 * places the superseded search looked, in that order.  Neither can
	 * be started as a process, so any case that ends up consulting one
	 * fails instead of quietly working. */
	if (writefile("sh.exe", DECOY) < 0) { printf("FAIL write decoy sh.exe\n"); return 1; }
	if (writefile(PATHDIR "/sh.exe", DECOY) < 0) { printf("FAIL write path decoy\n"); return 1; }

	/* ============================================================
	 * A. execvp() on a name with no <slash>, found through PATH.
	 *
	 * PATH holds exactly one entry, PATHDIR, which holds the script and
	 * the PATH decoy.  The cwd decoy is beside the caller.  So both of
	 * the places an external interpreter was ever looked for hold a
	 * file that cannot be started, and the script must still run.
	 * ============================================================ */
	CHECK(setenv("PATH", pathdir, 1) == 0);

	av[0] = CHILD; av[1] = (char *)"--run"; av[2] = (char *)SCRIPT;
	av[3] = (char *)"out-a.txt"; av[4] = (char *)"A1"; av[5] = (char *)"A2"; av[6] = 0;
	st = run(av);
	CHECK(st == RC_MARK);
	if (st != RC_MARK && st >= 100)
		printf("note: case A execvp failed, errno %d\n", st - 100);
	check_marker("out-a.txt", SCRIPT, "A1", "A2", 1);

	/* ============================================================
	 * B. The same call with nothing at all on PATH.
	 *
	 * PATH is emptied, so no PATH search of any kind can succeed --
	 * not for the interpreter, and not for anything else.  The script
	 * is named through the cwd copy instead (the caller's own working
	 * directory, which PATH has no say over).  This is the case that
	 * separates "the interpreter is in libc.a" from "the interpreter is
	 * somewhere PATH could reach": there is nowhere left to look.
	 * ============================================================ */
	CHECK(setenv("PATH", "", 1) == 0);

	av[2] = (char *)"./" SCRIPT;
	av[3] = (char *)"out-b.txt"; av[4] = (char *)"B1"; av[5] = (char *)"B2";
	st = run(av);
	CHECK(st == RC_MARK);
	if (st != RC_MARK && st >= 100)
		printf("note: case B execvp failed, errno %d\n", st - 100);
	check_marker("out-b.txt", "./" SCRIPT, "B1", "B2", 0);

	CHECK(setenv("PATH", pathdir, 1) == 0);

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
	 * shell".  PATH holds PATHDIR, so the search finds the script
	 * there; the decoy sh.exe in the same directory is what the search
	 * would find if the shell still looked for an interpreter.
	 *
	 * Driven through __sh_parse()/__sh_exec_list() -- src/sh/exec.c's
	 * own entry points, the ones test/sh-engine.c uses -- rather than
	 * by running obj/sh/sh.exe.  The subject is the [ENOEXEC] branch
	 * inside that executor, which is in libc.a; see the header.
	 * ============================================================ */
	{
		char cmd[512];
		struct sh_list *list;
		char err[256];
		int status;

		snprintf(cmd, sizeof cmd, "%s out-d.txt D1 D2", SCRIPT);
		list = __sh_parse(cmd, err, sizeof err);
		CHECK(list != 0);
		if (list) {
			status = -1;
			CHECK(__sh_exec_list(list, &status) == 0);
			CHECK(status == RC_MARK);
			if (status != RC_MARK) printf("note: case D exited %d\n", status);
			__sh_list_free(list);
		}
		check_marker("out-d.txt", SCRIPT, "D1", "D2", 1);

		/* E. The same clause's <slash> branch: "a shell invoked with
		 * the command name as its first operand". */
		snprintf(cmd, sizeof cmd, "./%s out-e.txt E1 E2", SCRIPT);
		list = __sh_parse(cmd, err, sizeof err);
		CHECK(list != 0);
		if (list) {
			status = -1;
			CHECK(__sh_exec_list(list, &status) == 0);
			CHECK(status == RC_MARK);
			if (status != RC_MARK) printf("note: case E exited %d\n", status);
			__sh_list_free(list);
		}
		check_marker("out-e.txt", "./" SCRIPT, "E1", "E2", 0);
	}

	unlink(CHILD);
	unlink("sh.exe");
	unlink(SCRIPT);
	unlink(PATHDIR "/" SCRIPT);
	unlink(PATHDIR "/sh.exe");
	rmdir(PATHDIR);

	if (fails) { printf("exec-script: failures: %d\n", fails); return 1; }
	printf("exec-script: all tests passed\n");
	return 0;
}
