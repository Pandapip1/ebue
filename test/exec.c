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
#include <fcntl.h>
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
#define RC_OK 0

/* argv[0] values that must survive the trip through spawn.c's command
 * line builder and back out of crt1.c's program-name parser unchanged.
 * The program name is read by different rules from every other argument
 * -- backslashes in it are literal and never escape anything -- so these
 * are the cases that catch it being quoted as if it were an ordinary
 * argument. */
static const char *const argv0_cases[] = {
	"plain",
	"a b\\",             /* a space *and* a trailing backslash */
	"a\\\\b",
	"c:\\dir\\prog.exe",
	"has space",
	"",
	0
};

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

/* Spawned with a deliberately odd argv[0]; argv[2] says what it should
 * have come out as. */
static int argv0_child(int argc, char **argv)
{
	if (argc != 3) { printf("child: argc %d, wanted 3\n", argc); return RC_ARGV_MISMATCH; }
	if (strcmp(argv[0], argv[2])) {
		printf("child: argv[0] = \"%s\", wanted \"%s\"\n", argv[0], argv[2]);
		return RC_ARGV_MISMATCH;
	}
	return RC_OK;
}

/* The entries test_empty_env_entry() puts in front of the inherited
 * environment.  The empty one is second, so with the truncating bug
 * everything below it -- including whatever the child needs to start at
 * all -- disappears. */
#define ENV_DRIVE_ENTRY "=Z:=Z:\\ntlibc-test"
static const char *const envblock_probes[] = {
	"NTLIBC_EMPTY_A=1",
	"",                     /* used to terminate the block */
	"NTLIBC_EMPTY_B=2",
	"NTLIBC_EMPTY_NOEQ",    /* no '=': passed on, names nothing */
	"NTLIBC_EMPTY_C=3",
	ENV_DRIVE_ENTRY,        /* Windows' own per-drive current directory */
	0
};

/* Spawned with an envp that contains an empty entry: everything after it
 * must still have arrived. */
static int envblock_child(void)
{
	static const char *const want[][2] = {
		{ "NTLIBC_EMPTY_A", "1" },
		{ "NTLIBC_EMPTY_B", "2" },
		{ "NTLIBC_EMPTY_C", "3" },
		{ 0, 0 }
	};
	int i;
	for (i = 0; want[i][0]; i++) {
		const char *v = getenv(want[i][0]);
		if (!v || strcmp(v, want[i][1])) {
			printf("child: %s = %s, wanted %s\n", want[i][0], v ? v : "(unset)", want[i][1]);
			return RC_ENV_MISMATCH;
		}
	}
	/* An entry with no '=' is passed through the way execve does on
	 * Linux; it simply names no variable. */
	if (getenv("NTLIBC_EMPTY_NOEQ")) {
		printf("child: NTLIBC_EMPTY_NOEQ names a variable\n");
		return RC_ENV_MISMATCH;
	}
	/* An entry whose *name* is empty because it starts with '=' is a
	 * real Windows entry, not a malformed one, and must not have been
	 * mistaken for the empty entry and dropped. */
	for (i = 0; environ[i]; i++)
		if (!strcmp(environ[i], ENV_DRIVE_ENTRY)) break;
	if (!environ[i]) {
		printf("child: \"%s\" did not survive the trip\n", ENV_DRIVE_ENTRY);
		return RC_ENV_MISMATCH;
	}
	return RC_OK;
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

static int spawn_status(const char *self, char *const av[], char *const ev[])
{
	int pid, status = -1;
	fflush(stdout);
	pid = __spawn(self, av, ev);
	if (pid <= 0) return -1;
	if (waitpid(pid, &status, 0) != pid) return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* A command line longer than a UNICODE_STRING can describe must be
 * refused, not silently cut down to whatever its length wrapped to. */
static void test_cmdline_limit(const char *self)
{
	static const size_t sizes[] = { 1000, 16000, 32000 };
	char *big, lenbuf[32];
	char *av[5];
	size_t i;

	/* Sizes that fit still round trip exactly. */
	for (i = 0; i < sizeof sizes / sizeof *sizes; i++) {
		big = malloc(sizes[i] + 1);
		CHECK(big != 0);
		if (!big) return;
		memset(big, 'x', sizes[i]);
		big[sizes[i]] = 0;
		snprintf(lenbuf, sizeof lenbuf, "%lu", (unsigned long)sizes[i]);
		av[0] = (char *)self; av[1] = (char *)"--arglen";
		av[2] = lenbuf; av[3] = big; av[4] = 0;
		CHECK(spawn_status(self, av, environ) == RC_OK);
		free(big);
	}

	/* Past the limit: a clean E2BIG, and no process started. */
	big = malloc(40001);
	CHECK(big != 0);
	if (!big) return;
	memset(big, 'x', 40000);
	big[40000] = 0;
	snprintf(lenbuf, sizeof lenbuf, "40000");
	av[0] = (char *)self; av[1] = (char *)"--arglen";
	av[2] = lenbuf; av[3] = big; av[4] = 0;
	errno = 0;
	CHECK(__spawn(self, av, environ) == -1);
	CHECK(errno == E2BIG);
	errno = 0;
	CHECK(execv(self, av) == -1);
	CHECK(errno == E2BIG);
	free(big);
}

/* An empty envp entry must not cut the environment block short.
 *
 * The probes go in *front* of the whole inherited environment rather
 * than replacing it.  An earlier version of this test handed the child
 * six entries -- the probes and a hand-built SystemRoot -- and nothing
 * else; that passed under Wine and failed on real Windows, because a
 * real NT process needs considerably more of an environment than Wine
 * does to get as far as main.  Wine's RtlCreateProcessParametersEx
 * substitutes an empty DllPath when none is given (dlls/ntdll/env.c,
 * "if (!DllPath) DllPath = &null_str") and its loader is content with
 * that, whereas NT's inherits the parent's and its loader treats a path
 * it cannot use as fatal (ReactOS dll/ntdll/ldr/ldrinit.c,
 * LdrpInitializeProcess: "We need a valid path" ->
 * LdrpInitFailure(STATUS_INVALID_PARAMETER)); the DLL search path NT
 * builds is in turn %PATH%-derived.  Passing the real environment
 * through means the child gets PATH, SystemRoot, SystemDrive, windir,
 * TEMP and the per-drive "=C:=C:\dir" entries -- whatever it actually
 * needs -- while the probes still prove the point: the empty entry is
 * the *second* thing in the block, so if it truncated, everything after
 * it (all three NTLIBC_EMPTY_* variables and the entire inherited
 * environment) would be gone. */
static void test_empty_env_entry(const char *self)
{
	char **ev;
	char *av[3];
	int i, n = 0, nenv = 0, nprobe = 0, pid, status = -1;

	for (nprobe = 0; envblock_probes[nprobe]; nprobe++) ;
	for (nenv = 0; environ[nenv]; nenv++) ;
	ev = malloc((size_t)(nprobe + nenv + 1) * sizeof *ev);
	CHECK(ev != 0);
	if (!ev) return;
	for (i = 0; i < nprobe; i++) ev[n++] = (char *)envblock_probes[i];
	for (i = 0; i < nenv; i++) ev[n++] = environ[i];
	ev[n] = 0;

	av[0] = (char *)self; av[1] = (char *)"--envblock"; av[2] = 0;
	fflush(stdout);
	errno = 0;
	pid = __spawn(self, av, ev);
	if (pid <= 0) {
		fails++;
		printf("FAIL %s:%d: spawn with an empty env entry failed, errno %d\n",
		       __FILE__, __LINE__, errno);
	} else if (waitpid(pid, &status, 0) != pid) {
		fails++;
		printf("FAIL %s:%d: waitpid on the env-block child failed, errno %d\n",
		       __FILE__, __LINE__, errno);
	} else if (!WIFEXITED(status) || WEXITSTATUS(status) != RC_OK) {
		/* Say what actually happened rather than just "not RC_OK".  A
		 * child that died before main carries the low byte of an NT
		 * status here -- 0x42 for STATUS_DLL_INIT_FAILED (0xc0000142),
		 * 0x35 for STATUS_DLL_NOT_FOUND -- which is distinguishable
		 * from this test's own small RC_* codes. */
		fails++;
		printf("FAIL %s:%d: env-block child: wait status 0x%08x, exited %d, code %d (0x%02x)\n",
		       __FILE__, __LINE__, (unsigned)status,
		       WIFEXITED(status) ? 1 : 0,
		       WIFEXITED(status) ? WEXITSTATUS(status) : -1,
		       WIFEXITED(status) ? (unsigned)WEXITSTATUS(status) : 0u);
	}
	free(ev);
}

/* A failed exec must leave the process image alone -- including the
 * close-on-exec descriptors, which stay the caller's until exec
 * succeeds. */
static void test_failed_exec_keeps_cloexec(const char *self)
{
	char buf[4];
	char *av[2];
	int fd = open(self, O_RDONLY | O_CLOEXEC);

	CHECK(fd >= 0);
	if (fd < 0) return;
	CHECK(read(fd, buf, 2) == 2);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);

	av[0] = (char *)"./no-such-program-xyz.exe"; av[1] = 0;
	errno = 0;
	CHECK(execv("./no-such-program-xyz.exe", av) == -1);
	CHECK(errno == ENOENT);

	errno = 0;
	CHECK(read(fd, buf, 2) == 2);   /* EBADF before the fix */
	CHECK(errno != EBADF);
	close(fd);
}

/* argv[0] is quoted by the program-name rules, so it comes back byte for
 * byte; the values those rules cannot express are refused outright. */
static void test_argv0_roundtrip(const char *self)
{
	char *av[4];
	int i;

	for (i = 0; argv0_cases[i]; i++) {
		av[0] = (char *)argv0_cases[i];
		av[1] = (char *)"--argv0";
		av[2] = (char *)argv0_cases[i];
		av[3] = 0;
		if (spawn_status(self, av, environ) != RC_OK) {
			fails++;
			printf("FAIL %s:%d: argv[0] \"%s\" did not round trip\n",
			       __FILE__, __LINE__, argv0_cases[i]);
		}
	}

	/* A quote in the program name has no encoding: fail, do not mangle. */
	av[0] = (char *)"a\"b"; av[1] = (char *)"--argv0"; av[2] = av[0]; av[3] = 0;
	errno = 0;
	CHECK(__spawn(self, av, environ) == -1);
	CHECK(errno == EINVAL);
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--exit")) return atoi(argv[2]);
	if (argc > 1 && !strcmp(argv[1], "--argv0")) return argv0_child(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "--envblock")) return envblock_child();
	if (argc > 1 && !strcmp(argv[1], "--arglen"))
		return argc == 4 && strlen(argv[3]) == (size_t)atol(argv[2]) ? RC_OK : RC_ARGV_MISMATCH;
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

	test_empty_env_entry(argv[0]);
	test_failed_exec_keeps_cloexec(argv[0]);
	test_argv0_roundtrip(argv[0]);
	/* Last: an execv() that is *meant* to fail with E2BIG will, if the
	 * length check is ever lost, succeed instead -- and a successful
	 * execv never comes back, so anything after it would not run. */
	test_cmdline_limit(argv[0]);

	if (!fails) printf("exec: all tests passed\n");
	return fails != 0;
}
