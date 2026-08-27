/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Cross-process coverage for POSIX record locks.  The vendored fork/11-1
 * case uses fork(), which stock Wine cannot provide; spawning this same
 * executable gives the lock a distinct process owner while preserving the
 * inherited descriptor and exercises the same F_GETLK/F_SETLK contract. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
extern int __spawn(const char *, char *const *, char *const *);

static int child_probe(int fd)
{
	struct flock fl;

	memset(&fl, 0, sizeof fl);
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 1;
	fl.l_len = 99;
	if (fcntl(fd, F_GETLK, &fl) < 0) return 90;
	if (fl.l_type != F_WRLCK) return 91;
	if (fcntl(fd, F_SETLK, &fl) != -1) return 92;
	if (errno != EACCES && errno != EAGAIN) return 93;
	return 42;
}

int main(int argc, char **argv)
{
	struct flock fl;
	char fdarg[24];
	char *child_argv[4];
	pid_t pid;
	int fd, status;

	if (argc == 3 && !strcmp(argv[1], "--child"))
		return child_probe(atoi(argv[2]));

	fd = open("t-fcntl-crossproc.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0 || write(fd, "lock", 4) != 4) return 1;
	memset(&fl, 0, sizeof fl);
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_len = 100;
	if (fcntl(fd, F_SETLK, &fl) < 0) return 2;
	/* fork() temporarily remakes descriptor handles to change their
	 * inheritability.  Exercise that path before handing this one to the
	 * spawned child so the lock cannot accidentally disappear there. */
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || fcntl(fd, F_SETFD, 0) < 0)
		return 8;

	snprintf(fdarg, sizeof fdarg, "%d", fd);
	child_argv[0] = argv[0];
	child_argv[1] = "--child";
	child_argv[2] = fdarg;
	child_argv[3] = NULL;
	pid = __spawn(argv[0], child_argv, environ);
	if (pid < 0) return 3;
	if (waitpid(pid, &status, 0) != pid) return 4;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
		printf("child status %#x\n", status);
		return 5;
	}

	if (close(fd) < 0 || unlink("t-fcntl-crossproc.txt") < 0) return 7;
	puts("posix-fcntl-lock-crossproc: ok");
	return 0;
}
