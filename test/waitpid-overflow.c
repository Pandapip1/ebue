/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * waitpid past the static seed of the child table (CHILD_MAX_ = 256):
 * spawn 260 copies of ourselves, wait on each by pid, and check that the
 * last four -- the ones that only exist because the table grew onto the
 * heap -- report the right status too.  Uses __spawn rather than fork
 * since Wine cannot fork.
 *
 * The children exit immediately rather than sleeping first.  With 260
 * spawns ahead of the first wait they are all long gone by the time
 * waitpid runs, so this only passes if the parent still holds a process
 * handle for each: an exited process whose last handle was closed cannot
 * be reopened by pid at all.  That makes it a direct test of the table
 * keeping every handle instead of dropping the ones that did not fit.
 *
 * Also checks the wait status encoding itself: every exit code in 0..255
 * must come back as WIFEXITED with that exact code (129..192 used to be
 * mistaken for "killed by a signal"), while a child kill()ed or abort()ed
 * must come back as WIFSIGNALED with the right WTERMSIG.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int __spawn(const char *, char *const *, char *const *);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define NCHILD 260

static char *self;

/* Spawn a copy of ourselves in the role argv[1]=role, argv[2]=arg. */
static pid_t spawn_role(const char *role, const char *arg)
{
	char *args[4];
	args[0] = self; args[1] = (char *)role; args[2] = (char *)arg; args[3] = 0;
	return __spawn(self, args, 0);
}

/* Every exit code survives the round trip through the wait status. */
static void test_exit_codes(void)
{
	static const int codes[] = { 0, 1, 127, 128, 129, 130, 192, 200, 254, 255 };
	size_t n = sizeof codes / sizeof codes[0], i;
	pid_t pids[sizeof codes / sizeof codes[0]];
	int status;

	for (i = 0; i < n; i++) {
		char num[16];
		sprintf(num, "%d", codes[i]);
		pids[i] = spawn_role("--child", num);
		if (pids[i] <= 0) { printf("FAIL: spawn exit %d: errno %d\n", codes[i], errno); fails++; return; }
	}
	for (i = 0; i < n; i++) {
		status = -1;
		CHECK(waitpid(pids[i], &status, 0) == pids[i]);
		CHECK(WIFEXITED(status));
		CHECK(!WIFSIGNALED(status));
		CHECK(WEXITSTATUS(status) == codes[i]);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != codes[i])
			printf("  exit %d reported status %#x\n", codes[i], status);
	}
}

/* A child kill()ed, and a child that abort()s, are WIFSIGNALED. */
static void test_signal_deaths(void)
{
	pid_t pid;
	int status;

	pid = spawn_role("--sleep", "0");
	if (pid <= 0) { printf("FAIL: spawn --sleep: errno %d\n", errno); fails++; }
	else {
		CHECK(kill(pid, SIGTERM) == 0);
		status = -1;
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFSIGNALED(status));
		CHECK(!WIFEXITED(status));
		CHECK(WTERMSIG(status) == SIGTERM);
		CHECK(!WCOREDUMP(status));   /* SIGTERM does not dump core */
		if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
			printf("  kill(SIGTERM) reported status %#x\n", status);
	}

	pid = spawn_role("--abort", "0");
	if (pid <= 0) { printf("FAIL: spawn --abort: errno %d\n", errno); fails++; }
	else {
		status = -1;
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFSIGNALED(status));
		CHECK(!WIFEXITED(status));
		CHECK(WTERMSIG(status) == SIGABRT);
		CHECK(WCOREDUMP(status));    /* SIGABRT does */
		if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT)
			printf("  abort() reported status %#x\n", status);
	}
}

int main(int argc, char **argv)
{
	pid_t pids[NCHILD];
	int i, status;

	self = argv[0];

	/* No sleep here on purpose: every child has already exited by the
	 * time the parent waits, so reaping works only if its handle was
	 * kept rather than dropped when the table outgrew its seed. */
	if (argc == 3 && !strcmp(argv[1], "--child")) return atoi(argv[2]) & 0xff;
	if (argc == 3 && !strcmp(argv[1], "--sleep")) {
		sleep(30);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--abort")) {
		abort();
	}

	for (i = 0; i < NCHILD; i++) {
		char num[16];
		sprintf(num, "%d", i % 250 + 1);
		pids[i] = spawn_role("--child", num);
		if (pids[i] <= 0) { printf("FAIL: spawn %d: errno %d\n", i, errno); return 1; }
	}

	for (i = 0; i < NCHILD; i++) {
		status = -1;
		CHECK(waitpid(pids[i], &status, 0) == pids[i]);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == i % 250 + 1);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != i % 250 + 1)
			printf("  child %d pid %d status %#x\n", i, (int)pids[i], status);
	}

	/* Already reaped: a second wait on a tracked child is ECHILD.  (An
	 * untracked one may still be openable until NT tears the process
	 * object down, so the reopen path cannot promise the same.) */
	errno = 0; CHECK(waitpid(pids[0], &status, 0) == -1 && errno == ECHILD);

	/* A bogus pid, and a live process that is not our child. */
	errno = 0; CHECK(waitpid(4, &status, 0) == -1 && errno == ECHILD);
	errno = 0; CHECK(waitpid(getppid(), &status, WNOHANG) == -1 && errno == ECHILD);
	errno = 0; CHECK(waitpid(getpid(), &status, WNOHANG) == -1 && errno == ECHILD);

	test_exit_codes();
	test_signal_deaths();

	if (fails) printf("%d failures\n", fails);
	else printf("all waitpid-overflow tests passed\n");
	return fails ? 1 : 0;
}
