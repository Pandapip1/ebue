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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <signal.h>
#include <string.h>
#include "libc.h"
#include "plat_fd.h"
#include "plat_process.h"
#include "plat_signal.h"

static struct __child __child_seed[CHILD_MAX_]; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- libc-internal name is intentionally reserved against application collision

struct __child *__children = __child_seed;
int __child_cap = CHILD_MAX_;

static int child_grow(void)
{
	struct __child *n;
	int cap = __child_cap * 2;
	int i;

	if (__child_cap >= CHILD_CAP_LIMIT_) return -1;
	if (cap > CHILD_CAP_LIMIT_) cap = CHILD_CAP_LIMIT_;
	n = __malloc((size_t)cap * sizeof *n);
	if (!n) return -1;
	for (i = 0; i < __child_cap; i++) n[i] = __children[i];
	for (; i < cap; i++) n[i] = (struct __child){0};
	if (__children != __child_seed) __free(__children);
	__children = n;
	__child_cap = cap;
	return 0;
}

int __child_add(int pid, __plat_handle_t h, __plat_handle_t job)
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
				__children[i].job = job;
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
	if (c->h) __plat_close(c->h);
	if (c->job) __plat_close(c->job);
	c->pid = 0;
	c->h = __PLAT_HANDLE_NULL;
	c->job = __PLAT_HANDLE_NULL;
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
		if (resume && __children[i].stopsig && __children[i].h) {
			/* exit.html: SIGHUP before SIGCONT.  Sent for real only
			 * where kill() can actually deliver it as a real signal
			 * instead of destroying the child -- see the big comment
			 * below __child_resume_stopped(). */
			if (__plat_sig_deliverable_to_other_process())
				kill(__children[i].pid, SIGHUP);
			__plat_process_resume(__children[i].h);
		}
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
 * The SIGCONT half is unconditional.  The clause's purpose is that no
 * stopped process is left with nobody able to continue it -- and here
 * that outcome is not merely untidy but terminal: the suspend count
 * lives in the kernel, the only handle to the child dies with this
 * process, and NtOpenProcess by pid is the last thing an unrelated
 * program would think to do, so a child left suspended is suspended for
 * good.  Resuming clears that completely.
 *
 * The SIGHUP half (clear_stops(), above) is sent for real only where the
 * platform can actually deliver it as a real signal, applying the
 * child's OWN disposition, rather than destroy the child outright --
 * __plat_sig_deliverable_to_other_process() (src/internal/plat_signal.h)
 * is that per-platform capability check, done once per stopped child,
 * before the SIGCONT for that same child.  On Linux, kill(child, SIGHUP)
 * IS the real thing: signal.c's kill() reaches its own last-resort arm,
 * and src/signal/linux/plat_signal.c's __plat_kill_terminate() turns
 * that into a genuine pidfd_send_signal(2) of SIGHUP, decoded back out
 * of the __ENCODE_SIGNAL_EXIT() encoding kill() built, applying whatever
 * real kernel-level disposition the child itself last synced
 * (__plat_sig_sync_kernel(), plat_signal.h) -- an ignored disposition is
 * a genuine no-op, and SIG_DFL runs the real default action (Term),
 * exactly what the clause asks for when nothing more specific is known.
 * A process-level function-pointer handler is not one of the two
 * dispositions ever synced to the kernel (plat_signal.h's own comment on
 * __plat_sig_sync_kernel()), so it does not run from a SIGHUP delivered
 * this way -- that would need the named-pipe listener kill() tries
 * first and Linux does not implement yet (src/signal/linux/
 * sigdelivery.c) -- but SIG_DFL is still the right fallback answer, not
 * a wrong one: a target that never told the kernel otherwise has no
 * disposition more specific for this to honor. On NT there is no
 * kernel-signal path at all: this library cannot deliver a real signal
 * to another process there (see kill()'s own comment), so
 * kill(child, SIGHUP) is NtTerminateProcess -- it would unconditionally
 * destroy a child whose real disposition might well have survived it,
 * which is a strictly worse answer than the one the clause is trying to
 * buy, so NT skips it and sends only the SIGCONT half.
 *
 * The coverage is wider than exit() because everything funnels through
 * __exit_internal(): _exit() and _Exit(), abort(), the default "terminate"
 * action __raise_internal() takes for an uncaught signal, and -- since
 * exception_handler() turns a mapped NT exception into exactly that --
 * a parent that dies of a SIGSEGV too.  What does not reach it is a
 * process ended without this library running at all: an exception code
 * exception_handler() passes on, or someone else's NtTerminateProcess.
 * That leaves the same residue any POSIX system leaves for SIGKILL, and
 * it is the reason a stop is worth keeping short. */
void __child_resume_stopped(void) { clear_stops(1); }

void __child_forget_stops(void) { clear_stops(0); }

// NOLINTEND(misc-include-cleaner)
