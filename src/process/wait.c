/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Waiting for children.
 *
 * A pid is a process id; the handle needed to wait on it is kept in the
 * child table __spawn/fork filled in.  The exit code becomes a POSIX
 * wait status: an ordinary exit is (code << 8), so all 256 exit codes
 * survive intact.  A death by signal comes from one of exactly two
 * places, neither of which a plain exit() can imitate:
 *
 *   - __NT_SIGNAL_EXIT(sig) (see libc.h), the out-of-range status this
 *     library's kill()/abort()/raise() end a process with;
 *   - an NT exception code the kernel itself terminated the process
 *     with, which is an 0xC0000xxx/0x8000xxxx NTSTATUS.
 *
 * wait3()/wait4() are the same reaping logic as waitpid(), with an extra
 * output: a struct rusage for the one child just reaped, filled from
 * NtQueryInformationProcess(ProcessTimes) on its handle before it is
 * closed -- the only piece of struct rusage NT actually has an answer
 * for (see src/misc/resource.c for the same source feeding getrusage()).
 * Every reap, whether or not the caller asked for it, is also folded
 * into a running total so getrusage(RUSAGE_CHILDREN) has something to
 * report even when the caller only ever called wait()/waitpid().
 */
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "libc.h"

/* WIFSIGNALED, WTERMSIG == sig, plus the WCOREDUMP bit for the signals
 * whose default action on Unix is "terminate and dump core". */
static int sig_status(int sig)
{
	int core;
	switch (sig) {
	case SIGQUIT: case SIGILL: case SIGTRAP: case SIGABRT:
	case SIGBUS: case SIGFPE: case SIGSEGV: case SIGSYS:
	case SIGXCPU: case SIGXFSZ:
		core = 0x80; break;
	default:
		core = 0; break;
	}
	return (sig & 0x7f) | core;
}

/* Exposed (not static) purely so test/posix-signal.c can drive the
 * exit-code -> wait-status mapping directly, without spawning a real
 * process for every boundary case -- same reasoning as
 * __errno_from_status() in src/internal/errno.c.  Declared in libc.h,
 * not include/: this is not part of the public API. */
int __wait_encode_status(int exitcode)
{
	unsigned code = (unsigned)exitcode;

	/* Ended by this library on behalf of a signal. */
	if (__NT_IS_SIGNAL_EXIT(code) && (code & 0x7f))
		return sig_status((int)(code & 0x7f));

	/* Ended by NT itself with an exception code. */
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_STACK_OVERFLOW:      return sig_status(SIGSEGV);
	case EXCEPTION_DATATYPE_MISALIGNMENT: return sig_status(SIGBUS);
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:    return sig_status(SIGILL);
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_INVALID_OPERATION:
	case EXCEPTION_FLT_OVERFLOW:        return sig_status(SIGFPE);
	case EXCEPTION_BREAKPOINT:          return sig_status(SIGTRAP);
	case (unsigned)STATUS_CONTROL_C_EXIT:
	case DBG_CONTROL_C:
	case DBG_CONTROL_BREAK:             return sig_status(SIGINT);
	}

	return (exitcode & 0xff) << 8;       /* WIFEXITED, WEXITSTATUS */
}

/* Cumulative rusage of every child reaped so far, for RUSAGE_CHILDREN
 * (src/misc/resource.c).  100ns NT ticks, converted to a timeval only
 * when read out -- see __rusage_children(). */
static unsigned long long children_ktime100ns, children_utime100ns;

static void ticks_to_timeval(unsigned long long t100ns, struct timeval *tv)
{
	tv->tv_sec = (time_t)(t100ns / 10000000ULL);
	tv->tv_usec = (suseconds_t)((t100ns % 10000000ULL) / 10);
}

void __rusage_children(struct rusage *ru)
{
	memset(ru, 0, sizeof *ru);
	ticks_to_timeval(children_ktime100ns, &ru->ru_stime);
	ticks_to_timeval(children_utime100ns, &ru->ru_utime);
}

/* Fill *ru with the resource usage of one child, from its still-open
 * process handle -- the ru argument to wait3()/wait4() is documented as
 * "resource usage of the terminated child", not the RUSAGE_CHILDREN
 * running total.  A query failure (the handle really ought to still be
 * valid here, since nothing has closed it yet) just leaves *ru zeroed
 * rather than failing the whole wait: the pid was already reaped
 * successfully, and losing the accounting detail is not worth losing
 * that. */
static void fill_child_rusage(HANDLE h, struct rusage *ru)
{
	KERNEL_USER_TIMES kt;
	NTSTATUS st;

	memset(ru, 0, sizeof *ru);
	if (!h) return;
	st = NtQueryInformationProcess(h, ProcessTimes, &kt, sizeof kt, 0);
	if (!NT_SUCCESS(st)) return;
	ticks_to_timeval((unsigned long long)kt.KernelTime, &ru->ru_stime);
	ticks_to_timeval((unsigned long long)kt.UserTime, &ru->ru_utime);
	children_ktime100ns += (unsigned long long)kt.KernelTime;
	children_utime100ns += (unsigned long long)kt.UserTime;
}

/* One reaping engine for wait/waitpid/wait3/wait4/waitid.
 *
 * `nowait` is waitid()'s WNOWAIT: "Keep the process whose status is
 * returned in infop in a waitable state" (waitid.html DESCRIPTION).
 * It is expressible here because reaping is two separable steps -- the
 * status is recorded in the table entry (c->done/c->status), and
 * __child_remove() is what closes the handle and frees the slot.  A
 * WNOWAIT call does the first and skips the second, so the child is
 * genuinely still waitable afterwards rather than merely reported as
 * such.
 *
 * That leaves a table entry with pid != 0 && done == 1, a state no
 * caller could reach before waitid existed (every other path calls
 * __child_remove immediately after setting done, and __child_add
 * clears it).  The any-child scan below therefore has to look for
 * already-known statuses first; without that it would skip such an
 * entry and report ECHILD for a child whose status POSIX requires it
 * to hand back again. */
static pid_t do_waitpid(pid_t pid, int *status, int options, struct rusage *ru, int nowait)
{
	struct __child *c;
	LARGE_INTEGER zero = 0;
	NTSTATUS st;
	PROCESS_BASIC_INFORMATION pbi;

	/* wait.html ERRORS (waitpid() only): "[EINVAL] The value of the
	 * options argument is not valid."  wait()/wait3()/wait4() always
	 * pass a value of their own choosing (0, or a caller-supplied
	 * options through wait3/wait4, which share this same contract), so
	 * checking here covers all of them uniformly. */
	if (options & ~(WNOHANG | WUNTRACED | WCONTINUED)) { errno = EINVAL; return -1; }

	if (pid == -1 || pid == 0) {
		/* Any child.  With one table, scan for a done one, or wait on
		 * all live handles.  Simple approach: find the first not-yet
		 * reaped child; if WNOHANG, poll each; else wait on the first. */
		int i, any = 0;
		/* An entry left done-but-unreaped by an earlier WNOWAIT: its
		 * status is already known, so it is available right now and
		 * needs no wait at all. */
		for (i = 0; i < __child_cap; i++)
			if (__children[i].pid && __children[i].done) {
				c = &__children[i];
				if (status) *status = c->status;
				pid = c->pid;
				if (ru) memset(ru, 0, sizeof *ru);
				if (!nowait) __child_remove(c);
				return pid;
			}
		for (i = 0; i < __child_cap; i++) {
			if (!__children[i].pid || __children[i].done) continue;
			any = 1;
			st = NtWaitForSingleObject(__children[i].h, 0, options & WNOHANG ? &zero : 0);
			if (st == STATUS_TIMEOUT) continue;
			if (!NT_SUCCESS(st)) return __set_errno_status(st);
			c = &__children[i];
			goto reap;
		}
		if (!any) { errno = ECHILD; return -1; }
		if (options & WNOHANG) return 0;
		/* Every remaining child is live; wait on the first live one. */
		for (i = 0; i < __child_cap; i++)
			if (__children[i].pid && !__children[i].done) {
				c = &__children[i];
				st = NtWaitForSingleObject(c->h, 0, 0);
				if (!NT_SUCCESS(st)) return __set_errno_status(st);
				goto reap;
			}
		errno = ECHILD;
		return -1;
	}

	if (pid < 0) pid = -pid;   /* process groups are single processes here */
	c = __child_find(pid);
	if (!c) {
		/* Not in the table means not waitable, full stop.
		 *
		 * There used to be a fallback here that reopened the pid with
		 * NtOpenProcess and accepted the process if its
		 * InheritedFromUniqueProcessId was us, to cover a child lost to an
		 * allocation failure in __child_add.  It rested on the assumption
		 * that an exited process with no handles left to it cannot be
		 * opened at all -- true under Wine, false on Windows, where the
		 * kernel process object outlives the last handle.  So on real
		 * Windows waitpid() reopened an already-reaped child and handed
		 * back its pid and exit status a second time, where POSIX requires
		 * ECHILD: a reaped child has ceased to exist and is no longer a
		 * child of this process (wait.html DESCRIPTION/ERRORS).
		 * test/waitpid-overflow.c:143 caught it on the real-Windows CI leg
		 * once wineserver was taught to keep exited pids openable too.
		 *
		 * A child that never made it into the table is therefore
		 * unwaitable, which is the honest outcome: __child_add failing is
		 * already a hard error at spawn time, and waitpid(-1)/wait() could
		 * never see such a child either, since they only scan the table. */
		errno = ECHILD;
		return -1;
	}
	if (c->done) { if (status) *status = c->status; pid = c->pid; if (ru) memset(ru, 0, sizeof *ru); if (!nowait) __child_remove(c); return pid; }

	st = NtWaitForSingleObject(c->h, 0, options & WNOHANG ? &zero : 0);
	if (st == STATUS_TIMEOUT) return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

reap:
	st = NtQueryInformationProcess(c->h, ProcessBasicInformation, &pbi, sizeof pbi, 0);
	/* Never invent a status: if the handle can't be queried, the entry is
	 * left as it is (a retry may still reach it) and the caller gets the
	 * real error rather than a fabricated "exited 0". */
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	c->status = __wait_encode_status((int)pbi.ExitStatus);
	c->done = 1;
	if (status) *status = c->status;
	pid = c->pid;
	if (ru) fill_child_rusage(c->h, ru);
	else { struct rusage tmp; fill_child_rusage(c->h, &tmp); }
	/* fill_child_rusage() has already folded this child's times into the
	 * RUSAGE_CHILDREN running total, so a WNOWAIT call must not leave the
	 * entry in a state where a later real reap folds them in a second
	 * time.  It does not: c->done is set above, and every path that sees
	 * done == 1 returns the recorded status without touching the handle
	 * again. */
	if (!nowait) __child_remove(c);
	return pid;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
	return do_waitpid(pid, status, options, 0, 0);
}

pid_t wait(int *status)
{
	return do_waitpid(-1, status, 0, 0, 0);
}

/* waitid --
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/waitid.html
 *
 * The reaping itself is do_waitpid() above, unchanged: waitid() differs
 * from waitpid() only in how the caller names the child (idtype/id
 * rather than a signed pid) and in how the result is reported (a
 * siginfo_t rather than a packed int).  Both are translations, so
 * neither the child-table walk nor the exit-status decoding is
 * duplicated here.
 *
 * idtype:
 *   P_ALL   "wait for any children and id is ignored" -- do_waitpid's
 *           pid == -1.
 *   P_PID   "wait for the child with a process ID equal to (pid_t)id".
 *   P_PGID  "wait for any child with a process group ID equal to
 *           (pid_t)id".  Every process is its own process group of one
 *           on this platform (src/unistd/ids.c, and kill()'s own
 *           writeup in src/signal/signal.c makes the same argument),
 *           so a process group id *is* a process id here and this is
 *           the P_PID case with the same number -- not an
 *           approximation of it.
 *   P_PIDFD is a Linux extension, not in POSIX, and there are no pidfds
 *           here; it is rejected with EINVAL along with any other
 *           value.
 *
 * options: "Applications shall specify at least one of the flags
 * WEXITED, WSTOPPED, or WCONTINUED" (DESCRIPTION), so a call naming
 * none of them is [EINVAL].
 *
 * WSTOPPED and WCONTINUED are accepted and can never fire, and that is
 * a property of NT rather than of this implementation:
 *
 *   - a child cannot be stopped.  kill(pid, SIGSTOP) here is
 *     NtTerminateProcess(h, __NT_SIGNAL_EXIT(SIGSTOP)) (see kill() in
 *     src/signal/signal.c) -- it ends the child rather than suspending
 *     it, because NT has no job control and no signal delivery to
 *     suspend into.
 *   - even a process suspended by other means could not be reported.
 *     An NT process object transitions to signalled exactly once, on
 *     termination; there is no waitable stop or continue transition
 *     for NtWaitForSingleObject to return, and NtSuspendProcess is not
 *     part of the surface this library declares.
 *
 * So the two flags are honoured to the letter -- a caller that passes
 * WSTOPPED|WEXITED gets exit notifications and no stop notifications,
 * which is exactly correct on a system where children never stop --
 * and CLD_STOPPED/CLD_CONTINUED are never produced.  See
 * test/posix-sysmisc.c for the fenced tests that state what a
 * stop/continue notification would have to look like.
 *
 * WNOWAIT is real, not accepted-and-ignored: it maps onto do_waitpid's
 * `nowait`, which records the status in the child table without
 * releasing the entry, so the child remains waitable and a following
 * wait/waitpid/waitid returns the same status again.
 */
int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)
{
	int status = 0;
	pid_t pid, want;

	if (options & ~(WEXITED | WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT)) {
		errno = EINVAL;
		return -1;
	}
	if (!(options & (WEXITED | WSTOPPED | WCONTINUED))) {
		errno = EINVAL;
		return -1;
	}

	switch (idtype) {
	case P_ALL:  want = -1; break;
	case P_PID:
	case P_PGID: want = (pid_t)id; break;
	default:     errno = EINVAL; return -1;
	}
	/* do_waitpid reads pid == 0 as "any child" and pid < 0 as a process
	 * group; P_PID/P_PGID with an id of 0 must not silently become
	 * P_ALL, and a negative id is not a process id at all. */
	if (idtype != P_ALL && want <= 0) { errno = ECHILD; return -1; }

	/* WEXITED is the only one of the three that this platform can ever
	 * satisfy (see the banner), so a call asking *only* for stop or
	 * continue notifications can never have one available.  With
	 * WNOHANG that is "no status available", which waitid.html says is
	 * a 0 return; without it, POSIX would have this block forever
	 * waiting for a transition that cannot occur, and reporting ECHILD
	 * is both terminating and true -- there is no child that can ever
	 * satisfy this request. */
	if (!(options & WEXITED)) {
		if (infop) memset(infop, 0, sizeof *infop);
		if (options & WNOHANG) return 0;
		errno = ECHILD;
		return -1;
	}

	pid = do_waitpid(want, &status, options & WNOHANG, 0, options & WNOWAIT ? 1 : 0);
	if (pid < 0) return -1;

	/* "If WNOHANG was specified and status is not available, 0 shall be
	 * returned" (RETURN VALUE).  DESCRIPTION also requires infop to be
	 * distinguishable in that case; POSIX.1-2017 leaves it
	 * implementation-defined whether infop is written, and zeroing it
	 * (si_signo == 0, si_pid == 0) is what makes "nothing happened"
	 * detectable by a caller that only has the 0 return to go on. */
	if (pid == 0) {
		if (infop) memset(infop, 0, sizeof *infop);
		return 0;
	}

	if (infop) {
		memset(infop, 0, sizeof *infop);
		/* "the si_signo member shall be set equal to SIGCHLD"
		 * (DESCRIPTION). */
		infop->si_signo = SIGCHLD;
		infop->si_pid = pid;
		/* The child inherited this process's credentials -- NT has no
		 * per-process uid at all and src/unistd/ids.c reports one
		 * fixed value for everything -- so the child's real user id is
		 * this process's. */
		infop->si_uid = getuid();
		if (WIFEXITED(status)) {
			infop->si_code = CLD_EXITED;
			/* For CLD_EXITED, si_status is the exit status the child
			 * passed to _exit(); for the two death-by-signal codes it
			 * is the signal number.  Both come straight out of the
			 * wait status __wait_encode_status() already produced, so
			 * waitid and waitpid can never disagree about a child. */
			infop->si_status = WEXITSTATUS(status);
		} else {
			infop->si_code = WCOREDUMP(status) ? CLD_DUMPED : CLD_KILLED;
			infop->si_status = WTERMSIG(status);
		}
		/* si_utime/si_stime are not POSIX members of the SIGCHLD
		 * siginfo (waitid.html names only si_pid, si_uid, si_signo,
		 * si_status and si_code); they are left zero here rather than
		 * filled, and wait3()/wait4()'s struct rusage is the supported
		 * way to get a reaped child's times. */
	}
	return 0;
}

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
pid_t wait3(int *status, int options, struct rusage *ru)
{
	return do_waitpid(-1, status, options, ru, 0);
}

pid_t wait4(pid_t pid, int *status, int options, struct rusage *ru)
{
	return do_waitpid(pid, status, options, ru, 0);
}
#endif
