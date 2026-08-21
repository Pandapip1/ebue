/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The exec family, runnable under Wine (no fork needed): the parent
 * __spawn()s itself in an "--exec-*" role, that child calls execv/execve
 * on itself in an "--argv*" role, and the exec'd image checks what it
 * received.  exec never returns on success and exits with the exec'd
 * program's status, so the parent just inspects the spawned child's exit
 * code.
 *
 * BUG (live, expected to FAIL TO LINK): execvp/execvpe (src/process/exec.c:46)
 * call __find_program, which src/internal/libc.h:117 declares but no
 * source file defines.  execv/execve/execvp all live in exec.o, so any
 * reference to any exec function pulls the object in and the link fails
 * with "unresolved reference to '__find_program'".  Once that is defined
 * this test must also pass at runtime.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;
int __spawn(const char *path, char *const argv[], char *const envp[]);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Arguments exercising every quoting rule spawn.c's append_arg implements
 * and crt1.c's split_cmdline undoes. */
static const char *const tricky[] = {
	"plain",
	"two words",
	"",
	"say \"hi\"",
	"trailing\\",
	"two\\\\backslashes",
	"\\\"escaped quote",
	"tab\there",
	"c:\\dir with space\\",
	"\"",
	"end\\\\\"",
	0
};

#define RC_ARGV_MISMATCH 7
#define RC_ENV_MISMATCH 8
#define RC_EXEC_RETURNED 99

static char **build_argv(const char *self, const char *role)
{
	static char *v[32];
	int i, n = 0;
	v[n++] = (char *)self;
	v[n++] = (char *)role;
	for (i = 0; tricky[i]; i++) v[n++] = (char *)tricky[i];
	v[n] = 0;
	return v;
}

/* The exec'd image: verify argv (and, for --argv-env, the environment). */
static int argv_child(int argc, char **argv)
{
	int i;
	for (i = 0; tricky[i]; i++) {
		if (i + 2 >= argc || strcmp(argv[i + 2], tricky[i])) {
			printf("child: argv[%d] = \"%s\", wanted \"%s\"\n", i + 2,
			       i + 2 < argc ? argv[i + 2] : "(missing)", tricky[i]);
			return RC_ARGV_MISMATCH;
		}
	}
	if (i + 2 != argc) {
		printf("child: argc %d, wanted %d\n", argc, i + 2);
		return RC_ARGV_MISMATCH;
	}
	if (!strcmp(argv[1], "--argv-env")) {
		const char *v = getenv("NTLIBC_TEST_ENV");
		if (!v || strcmp(v, "hello world")) {
			printf("child: NTLIBC_TEST_ENV = %s\n", v ? v : "(unset)");
			return RC_ENV_MISMATCH;
		}
		if (getenv("NTLIBC_TEST_ABSENT")) return RC_ENV_MISMATCH;
	}
	return 0;
}

/* The intermediate child: exec self in an --argv role. */
static int exec_child(const char *self, const char *role)
{
	if (!strcmp(role, "--exec-v")) {
		execv(self, build_argv(self, "--argv"));
	} else if (!strcmp(role, "--exec-vp")) {
		/* self contains a slash or is an absolute path: used as-is */
		execvp(self, build_argv(self, "--argv"));
	} else if (!strcmp(role, "--exec-ve")) {
		char *envp[3];
		char sysroot[512];
		const char *sr = getenv("SystemRoot");
		envp[0] = (char *)"NTLIBC_TEST_ENV=hello world";
		if (sr) { snprintf(sysroot, sizeof sysroot, "SystemRoot=%s", sr); envp[1] = sysroot; }
		else envp[1] = (char *)"SystemRoot=C:\\Windows";
		envp[2] = 0;
		setenv("NTLIBC_TEST_ABSENT", "1", 1);   /* must not leak past an explicit envp */
		execve(self, build_argv(self, "--argv-env"), envp);
	} else if (!strcmp(role, "--exec-exit")) {
		char *v[4];
		v[0] = (char *)self; v[1] = (char *)"--exit"; v[2] = (char *)"200"; v[3] = 0;
		execv(self, v);
	} else if (!strcmp(role, "--exec-missing")) {
		errno = 0;
		if (execv("./no-such-program-here.exe", build_argv(self, "--argv")) != -1) return 1;
		return errno == ENOENT ? 0 : 2;
	}
	printf("child: exec returned, errno %d\n", errno);
	return RC_EXEC_RETURNED;
}

static int run_role(const char *self, const char *role)
{
	char *argv[3];
	int pid, status = -1;
	argv[0] = (char *)self;
	argv[1] = (char *)role;
	argv[2] = 0;
	fflush(stdout);
	pid = __spawn(self, argv, environ);
	CHECK(pid > 0);
	if (pid <= 0) return -1;
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status));
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--exit")) return atoi(argv[2]);
	if (argc > 1 && (!strcmp(argv[1], "--argv") || !strcmp(argv[1], "--argv-env")))
		return argv_child(argc, argv);
	if (argc > 1 && !strncmp(argv[1], "--exec-", 7)) return exec_child(argv[0], argv[1]);

	CHECK(run_role(argv[0], "--exec-v") == 0);
	CHECK(run_role(argv[0], "--exec-vp") == 0);
	CHECK(run_role(argv[0], "--exec-ve") == 0);
	/* the exec'd image's exit code is what exec's caller exits with */
	CHECK(run_role(argv[0], "--exec-exit") == 200);
	/* a missing program fails with ENOENT and exec returns */
	CHECK(run_role(argv[0], "--exec-missing") == 0);

	if (!fails) printf("exec: all tests passed\n");
	return fails != 0;
}
