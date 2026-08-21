/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * waitpid on children that overflowed the child table (CHILD_MAX_ = 256):
 * spawn 260 copies of ourselves, wait on each by pid, and check that the
 * last four -- which waitpid has to reopen by pid -- report the right
 * status too.  Uses __spawn rather than fork since Wine cannot fork.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

int __spawn(const char *, char *const *, char *const *);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define NCHILD 260

int main(int argc, char **argv)
{
	pid_t pids[NCHILD];
	int i, status;

	if (argc == 3 && !strcmp(argv[1], "--child")) {
		usleep(100000);
		return atoi(argv[2]) % 100;
	}

	for (i = 0; i < NCHILD; i++) {
		char num[16];
		char *args[4];
		sprintf(num, "%d", i);
		args[0] = argv[0]; args[1] = "--child"; args[2] = num; args[3] = 0;
		pids[i] = __spawn(argv[0], args, 0);
		if (pids[i] <= 0) { printf("FAIL: spawn %d: errno %d\n", i, errno); return 1; }
	}

	for (i = 0; i < NCHILD; i++) {
		status = -1;
		CHECK(waitpid(pids[i], &status, 0) == pids[i]);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == i % 100);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != i % 100)
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

	if (fails) printf("%d failures\n", fails);
	else printf("all waitpid-overflow tests passed\n");
	return fails ? 1 : 0;
}
