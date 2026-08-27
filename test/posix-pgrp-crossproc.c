/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stock-Wine coverage for killpg/1-2's mechanism without fork(). */
#include <signal.h>
#include <stdio.h>
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

static int child(void)
{
	struct sigaction sa;
	sigset_t mask;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	if (sigaction(SIGUSR1, &sa, 0) < 0) return 90;
	if (setpgrp() != getpid()) return 91;
	sigfillset(&mask);
	sigdelset(&mask, SIGUSR1);
	for (;;) sigsuspend(&mask);
}

int main(int argc, char **argv)
{
	char *child_argv[] = { argv[0], "--child", 0 };
	pid_t pid, pgid;
	int status;

	if (argc > 1 && !strcmp(argv[1], "--child")) return child();
	pid = __spawn(argv[0], child_argv, environ);
	if (pid < 0) return 1;
	usleep(300000);
	pgid = getpgid(pid);
	if (pgid != pid) { printf("getpgid(%d)=%d\n", pid, pgid); return 2; }
	if (killpg(pgid, SIGUSR1) < 0) return 3;
	if (waitpid(pid, &status, 0) != pid) return 4;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
		printf("child status %#x\n", status);
		return 5;
	}
	puts("posix-pgrp-crossproc: ok");
	return 0;
}
