/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The child table: one entry per living-or-unreaped child, holding the
 * pid and the process HANDLE waitpid needs to wait on it and read its
 * exit code.
 *
 * The handle is the only thing that keeps a child reapable.  A pid on
 * Windows is a name for a process *object*, and the object goes away as
 * soon as the last handle to it does; once that happens NtOpenProcess by
 * CLIENT_ID cannot find the pid again (and the number may even be reused
 * for something else).  So a table that can fill up is not a table that
 * merely loses track of extra children -- dropping the handle destroys
 * the only way to ever learn how the child exited.  The table therefore
 * grows on demand instead of overflowing.
 *
 * It starts as a static array, so the common case -- and, importantly,
 * any fork/spawn done before or without the allocator -- never calls
 * malloc at all; only the 257th concurrently-unreaped child does.  Growth
 * allocates from the process heap (src/malloc/malloc.c: RtlAllocateHeap
 * on __peb->ProcessHeap), which is ordinary address space, so the grown
 * table makes the trip through RtlCloneUserProcess with everything else
 * fork() copies -- see fork.c's header comment.  The pointer __children
 * itself is just a global variable, and globals are memory too, so the
 * clone sees the same pointer aimed at its own copy of the same bytes.
 *
 * If growth fails there is nothing better to do than what the fixed table
 * used to do: __child_add returns -1 and the caller closes the handle,
 * losing the child rather than losing the fork.
 *
 * Note that __child_find returns a pointer *into* the table, which a
 * later growth may move.  Every caller uses it and is done with it before
 * any further __child_add, so this is safe as written; anything that
 * wants to hold one across a fork/spawn must remember the pid instead.
 */
#include <string.h>
#include "libc.h"

static struct __child __child_seed[CHILD_MAX_];

struct __child *__children = __child_seed;
int __child_cap = CHILD_MAX_;

static int child_grow(void)
{
	struct __child *n;
	int cap = __child_cap * 2;

	if (__child_cap >= CHILD_CAP_LIMIT_) return -1;
	if (cap > CHILD_CAP_LIMIT_) cap = CHILD_CAP_LIMIT_;
	n = __malloc((size_t)cap * sizeof *n);
	if (!n) return -1;
	memcpy(n, __children, (size_t)__child_cap * sizeof *n);
	memset(n + __child_cap, 0, (size_t)(cap - __child_cap) * sizeof *n);
	if (__children != __child_seed) __free(__children);
	__children = n;
	__child_cap = cap;
	return 0;
}

int __child_add(int pid, HANDLE h)
{
	int i;
	/* SA_NOCLDWAIT: this child must never be something wait()/waitpid()
	 * can find (src/signal/signal.c's __sigchld_nocldwait()).  Reporting
	 * failure here, same as a table that could not grow, makes both
	 * callers (fork.c, spawn.c) take the degrade path they already have:
	 * close the handle and let the child run untracked. */
	if (__sigchld_nocldwait()) return -1;
	for (;;) {
		for (i = 0; i < __child_cap; i++)
			if (!__children[i].pid) {
				__children[i].pid = pid;
				__children[i].h = h;
				__children[i].done = 0;
				__children[i].status = 0;
				__children[i].stopsig = 0;
				__children[i].jobstat = 0;
				return 0;
			}
		if (child_grow() < 0) return -1;
	}
}

struct __child *__child_find(int pid)
{
	int i;
	for (i = 0; i < __child_cap; i++)
		if (__children[i].pid == pid) return &__children[i];
	return 0;
}

void __child_remove(struct __child *c)
{
	if (c->h) NtClose(c->h);
	c->pid = 0;
	c->h = 0;
}

static void clear_stops(int resume)
{
	int i;
	for (i = 0; i < __child_cap; i++) {
		if (!__children[i].pid) continue;
		/* Best effort: a status here means the child is already gone,
		 * which is the outcome this is trying to reach anyway.  Resume
		 * only a child that is actually stopped; jobstat may instead hold
		 * an already-running child's pending WCONTINUED report. */
		if (resume && __children[i].stopsig && __children[i].h)
			NtResumeProcess(__children[i].h);
		__children[i].stopsig = 0;
		/* A forked child did not cause either a sibling's stop or its
		 * continue, so neither inherited report belongs to it.  Clearing
		 * both fields also makes the helper's name describe the complete
		 * job-control state rather than only the kernel suspension. */
		__children[i].jobstat = 0;
	}
}

/* exit.html CONSEQUENCES OF PROCESS TERMINATION: "if the exit of the
 * process causes a process group to become orphaned, and if any member
 * of the newly-orphaned process group is stopped, then a SIGHUP signal
 * followed by a SIGCONT signal shall be sent to each process".  Every
 * process is its own process group of one here (src/unistd/ids.c), so a
 * child this process stopped is orphaned the instant this process ends,
 * and the clause applies to all of them.
 *
 * The SIGCONT half is done, the SIGHUP half deliberately is not, and the
 * asymmetry is not a shortcut.  The clause's purpose is that no stopped
 * process is left with nobody able to continue it -- and here that
 * outcome is not merely untidy but terminal: the suspend count lives in
 * the kernel, the only handle to the child dies with this process, and
 * NtOpenProcess by pid is the last thing an unrelated program would
 * think to do, so a child left suspended is suspended for good.
 * Resuming clears that completely.  SIGHUP would not add to it: this
 * library cannot deliver a catchable signal to another process (see
 * kill() in src/signal/signal.c), so kill(child, SIGHUP) is
 * NtTerminateProcess -- it would unconditionally destroy a child that on
 * a real system may well have caught SIGHUP and carried on, which is a
 * strictly worse answer than the one the clause is trying to buy.
 *
 * The coverage is wider than exit() because everything funnels through
 * __nt_exit(): _exit() and _Exit(), abort(), the default "terminate"
 * action __raise_internal() takes for an uncaught signal, and -- since
 * exception_handler() turns a mapped NT exception into exactly that --
 * a parent that dies of a SIGSEGV too.  What does not reach it is a
 * process ended without this library running at all: an exception code
 * exception_handler() passes on, or someone else's NtTerminateProcess.
 * That leaves the same residue any POSIX system leaves for SIGKILL, and
 * it is the reason a stop is worth keeping short. */
void __child_resume_stopped(void) { clear_stops(1); }

void __child_forget_stops(void) { clear_stops(0); }
