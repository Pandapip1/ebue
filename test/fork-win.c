/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fork() relies on RtlCloneUserProcess, which Wine does not implement
 * (see the Makefile's note on *-win.c tests), so this cannot run under
 * "make check".  It is still built there to catch compile regressions;
 * run it directly under wine to see what real fork() does.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

/* stdio isn't ready yet in this tree, so failures are reported with
 * write() rather than printf() -- see the note at the top of this file. */
static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; write(1, "FAIL: " #cond "\n", sizeof("FAIL: " #cond "\n") - 1); } } while (0)

int main(void)
{
	pid_t parent_pid = getpid();
	pid_t pid;
	int global_before = 12345, status;
	static int global_static = 111;
	int stack_var = 222;

	pid = fork();
	CHECK(pid >= -1);

	if (pid == 0) {
		/* child */
		if (global_before != 12345 || global_static != 111 || stack_var != 222) {
			_exit(2);
		}
		if (getpid() == parent_pid) _exit(3);
		if (getppid() != parent_pid) _exit(4);
		global_static = 999;         /* must not be visible to the parent */
		_exit(42);
	}

	/* parent */
	CHECK(pid > 0);
	CHECK(global_static == 111);     /* the child's write didn't leak back */
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 42);

	if (!fails) write(1, "fork: all tests passed\n", 24);
	return fails != 0;
}
