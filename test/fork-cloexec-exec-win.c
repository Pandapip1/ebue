/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ad hoc reproduction for the fork()+cloexec-handle-reuse bug reported
 * downstream: fork.c's mark_fds_inheritable() only marks non-O_CLOEXEC
 * handles inheritable before RtlCloneUserProcess, so a close-on-exec
 * descriptor's *handle* is never copied into the clone -- but the fd
 * *table entry* for it is, since __fds is ordinary memory duplicated
 * whole with the rest of the address space. That leaves the freed
 * handle number available for immediate reuse by the very next
 * handle-creating call in the child: if the child then execve()s,
 * __spawn's RtlCreateUserProcess can hand back exactly that number for
 * the new grandchild's process handle, and exec.c's
 * __fd_close_all_cloexec() -- which runs right after the spawn,
 * treating the stale table entry as a real descriptor -- closes it out
 * from under __children's tracking. The following waitpid() inside
 * execve() then fails with STATUS_INVALID_HANDLE (EBADF), even though
 * the grandchild ran to completion.
 *
 * Needs a real fork(), so like fork-win.c this only runs where
 * RtlCloneUserProcess actually works: not stock Wine (unimplemented,
 * per the Makefile's *-win.c note), but does run under a Wine build
 * patched to implement RtlCloneUserProcess, and on real Windows CI.
 */
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

static int fails, bug_hits, other_fail;
static void say(const char *s) { write(1, s, strlen(s)); }
#define CHECK(cond) do { if (!(cond)) { fails++; say("FAIL: " #cond "\n"); } } while (0)

#define N_ITERS 300

int main(int argc, char **argv)
{
	int i;
	char path[64];

	if (argc > 1 && !strcmp(argv[1], "--grandchild")) {
		/* Proves it actually ran, independent of what the forked
		 * parent's own waitpid sees. */
		int fd = open("fork-cloexec-exec-win.ran", O_CREAT | O_WRONLY | O_APPEND, 0644);
		if (fd >= 0) { write(fd, "x", 1); close(fd); }
		return 0;
	}

	unlink("fork-cloexec-exec-win.ran");

	for (i = 0; i < N_ITERS; i++) {
		int cloexec_fd, status;
		pid_t c, w;

		if (i % 20 == 0) { char pg[32]; snprintf(pg, sizeof pg, "iter %d\n", i); say(pg); }
		snprintf(path, sizeof path, "fork-cloexec-exec-win.tmp%d", i);
		cloexec_fd = open(path, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);
		CHECK(cloexec_fd >= 0);

		c = fork();
		CHECK(c >= -1);
		if (c == 0) {
			char *av[3];
			av[0] = argv[0]; av[1] = (char *)"--grandchild"; av[2] = 0;
			errno = 0;
			execv(argv[0], av);
			/* execve()'s own internal waitpid() failed, or the spawn
			 * itself failed -- either way exec "returned", which it
			 * must not on success. */
			_exit(errno == EBADF ? 42 : 43);
		}

		w = waitpid(c, &status, 0);
		CHECK(w == c);
		if (w == c && WIFEXITED(status)) {
			int code = WEXITSTATUS(status);
			if (code == 42) bug_hits++;
			else if (code != 0) other_fail++;
		} else if (w == c) {
			other_fail++;
		}
		if (cloexec_fd >= 0) close(cloexec_fd);
		unlink(path);
	}

	{
		struct stat st;
		int ran = stat("fork-cloexec-exec-win.ran", &st) == 0;
		char msg[256];
		snprintf(msg, sizeof msg,
		         "fork-cloexec-exec-win: %d/%d bug-signature failures (EBADF from exec's "
		         "internal waitpid), %d other failures, grandchild-ran-marker present: %d\n",
		         bug_hits, N_ITERS, other_fail, ran);
		say(msg);
	}
	unlink("fork-cloexec-exec-win.ran");

	if (!fails && !bug_hits && !other_fail) say("fork-cloexec-exec-win: all tests passed\n");
	return fails != 0 || bug_hits != 0 || other_fail != 0;
}
