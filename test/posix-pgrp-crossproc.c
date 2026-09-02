/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stock-Wine coverage for killpg/1-2's mechanism without fork(). */
/* sigset_t and the sigaction()/killpg()/setpgrp() family are
 * feature-test gated in include/signal.h and include/unistd.h; same
 * define most other test/*.c already carry for the same reason (see
 * test/posix-glob.c's comment on this exact define). */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
extern int __spawn(const char *, char *const *, char *const *);

static void handler(int sig)
{
	(void)sig;
	_exit(42);
}

static int child(const char *read_fd, const char *write_fd)
{
	struct sigaction sa;
	sigset_t mask;
	int ready_read = atoi(read_fd);
	int ready_write = atoi(write_fd);
	char byte = 1;

	close(ready_read);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	if (sigaction(SIGUSR1, &sa, 0) < 0) return 90;
	if (setpgrp() != getpid()) return 91;
	if (write(ready_write, &byte, 1) != 1) return 92;
	close(ready_write);
	sigfillset(&mask);
	sigdelset(&mask, SIGUSR1);
	for (;;) sigsuspend(&mask);
}

int main(int argc, char **argv)
{
	char read_fd[16], write_fd[16], byte;
	char *child_argv[] = { argv[0], "--child", read_fd, write_fd, 0 };
	int ready[2];
	pid_t pid, pgid;
	int status;

	if (argc > 3 && !strcmp(argv[1], "--child")) return child(argv[2], argv[3]);
	if (pipe(ready) < 0) return 1;
	snprintf(read_fd, sizeof read_fd, "%d", ready[0]);
	snprintf(write_fd, sizeof write_fd, "%d", ready[1]);
	pid = __spawn(argv[0], child_argv, environ);
	if (pid < 0) { close(ready[0]); close(ready[1]); return 1; }
	close(ready[1]);
	if (read(ready[0], &byte, 1) != 1) {
		close(ready[0]);
		waitpid(pid, &status, 0);
		return 2;
	}
	close(ready[0]);
	pgid = getpgid(pid);
	if (pgid != pid) { printf("getpgid(%d)=%d\n", pid, pgid); return 3; }
	if (killpg(pgid, SIGUSR1) < 0) return 4;
	if (waitpid(pid, &status, 0) != pid) return 5;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
		printf("child status %#x\n", status);
		return 6;
	}
	puts("posix-pgrp-crossproc: ok");
	return 0;
}
