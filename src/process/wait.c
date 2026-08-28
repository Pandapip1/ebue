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

/* These two are ordinary process-lifetime globals, and
 * RtlCloneUserProcess copies the address space that holds them -- so a
 * fork()ed child would otherwise start life reporting the *parent's*
 * reaped-children times as its own, through both
 * getrusage(RUSAGE_CHILDREN) and times()'s tms_cutime/tms_cstime.
 * fork.html: "The child process values of tms_utime, tms_stime,
 * tms_cutime, and tms_cstime shall be set to 0."  The first two come
 * from the child's own NT process object, which really is fresh (its
 * KERNEL_USER_TIMES has its own CreateTime and zeroed Kernel/User
 * time); these two have no kernel source at all -- KERNEL_USER_TIMES
 * has no child-time fields -- so nothing but this resets them.
 * src/process/fork.c calls it on the STATUS_PROCESS_CLONED arm. */
void __rusage_children_reset(void)
{
	children_ktime100ns = 0;
	children_utime100ns = 0;
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

/* The pending stop-or-continue report for a child, if any.
 *
 * wait.html, WUNTRACED: "The status of any child processes specified by
 * pid that are stopped, and whose status has not yet been reported since
 * they stopped, shall also be reported to the requesting process."
 * WCONTINUED says the same for a child "that has continued from a job
 * control stop".  waitid.html's WSTOPPED and WCONTINUED are the same two
 * clauses; WSTOPPED and WUNTRACED are even the same bit
 * (<sys/wait.h>), so one lookup serves both interfaces and they cannot
 * come to disagree about a child.
 *
 * A stop sent by this parent is recorded immediately by kill().  A child
 * that stopped itself cannot write this private table, so it publishes an
 * auto-reset named event before suspending; discover_self_stops() consumes
 * that event and records the same status here.
 *
 * `want` is a pid, or 0 for any child.  `which` is WSTOPPED (== WUNTRACED)
 * and/or WCONTINUED.  Consuming is the caller's job -- it clears
 * c->jobstat, which is what "has not yet been reported" turns on, unless
 * WNOWAIT asked for the report to stay available. */
static struct __child *job_report(pid_t want, int which)
{
	int i;

	for (i = 0; i < __child_cap; i++) {
		struct __child *c = &__children[i];
		int js = c->jobstat;

		if (!c->pid || !js) continue;
		if (want && c->pid != want) continue;
		if (!(which & (WIFCONTINUED(js) ? WCONTINUED : WSTOPPED))) continue;
		return c;
	}
	return 0;
}

static void discover_self_stops(pid_t want)
{
	int i;

	for (i = 0; i < __child_cap; i++) {
		struct __child *c = &__children[i];
		int sig;

		if (!c->pid || c->done || c->stopsig) continue;
		if (want && c->pid != want) continue;
		sig = __sig_consume_child_stop(c->pid);
		if (!sig) continue;
		c->stopsig = sig;
		c->jobstat = __W_STOPPED(sig);
		__sigchld_job_control(c, sig);
	}
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
	LARGE_INTEGER poll = -100000; /* 10ms: self-stop markers are not handles in the wait set */
	NTSTATUS st;
	PROCESS_BASIC_INFORMATION pbi;

	/* wait.html ERRORS (waitpid() only): "[EINVAL] The value of the
	 * options argument is not valid."  wait()/wait3()/wait4() always
	 * pass a value of their own choosing (0, or a caller-supplied
	 * options through wait3/wait4, which share this same contract), so
	 * checking here covers all of them uniformly. */
	if (options & ~(WNOHANG | WUNTRACED | WCONTINUED)) { errno = EINVAL; return -1; }

	retry:
	/* A stopped or continued child, ahead of any exit wait.  The order
	 * matters and is not a preference: a stopped child never becomes
	 * signalled, so waiting on its handle first would block forever
	 * holding a status the caller asked for and that is sitting right
	 * here.  The entry is not removed -- the child has not exited and
	 * is still to be waited for -- only the report is consumed. */
	if (options & (WUNTRACED | WCONTINUED)) {
		if (options & WUNTRACED)
			discover_self_stops(pid == -1 || pid == 0 ? 0 : pid < 0 ? -pid : pid);
		c = job_report(pid == -1 || pid == 0 ? 0 : pid < 0 ? -pid : pid,
		               options & (WUNTRACED | WCONTINUED));
		if (c) {
			if (status) *status = c->jobstat;
			if (ru) memset(ru, 0, sizeof *ru);
			if (!nowait) c->jobstat = 0;
			return c->pid;
		}
	}

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
			st = NtWaitForSingleObject(__children[i].h, 0,
			                           options & WNOHANG ? &zero :
			                           options & WUNTRACED ? &poll : 0);
			if (st == STATUS_TIMEOUT) continue;
			if (!NT_SUCCESS(st)) return __set_errno_status(st);
			c = &__children[i];
			goto reap;
		}
		if (!any) { errno = ECHILD; return -1; }
		if (options & WNOHANG) return 0;
		if (options & WUNTRACED) goto retry;
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

	st = NtWaitForSingleObject(c->h, 0,
	                           options & WNOHANG ? &zero :
	                           options & WUNTRACED ? &poll : 0);
	if (st == STATUS_TIMEOUT) {
		if (options & WNOHANG) return 0;
		goto retry;
	}
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
 * WSTOPPED and WCONTINUED are real, and share every part of their
 * implementation with waitpid()'s WUNTRACED/WCONTINUED -- see
 * job_report() above.  Parent-sent stops are recorded directly; a
 * self-stopping child publishes the named marker discovered above.
 *
 * What that same lack does cost: a child suspended by anything *else*
 * -- a debugger, another program calling NtSuspendProcess on it -- is
 * unreportable, because nothing notifies this process and no state
 * exists to poll.  That half stays impossible, and it is also not what
 * the clause asks for ("any child that has stopped upon receipt of a
 * signal").
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

	if (options & WEXITED) {
		/* WSTOPPED is WUNTRACED and WCONTINUED is WCONTINUED (the
		 * same bits, <sys/wait.h>), so the flags pass straight
		 * through and do_waitpid() applies its own job_report() ahead
		 * of the exit wait.  WEXITED itself has no waitpid() spelling
		 * -- it is what waitpid() always does -- and must be masked
		 * off, since do_waitpid() rejects any bit it does not know. */
		pid = do_waitpid(want, &status, options & (WNOHANG | WSTOPPED | WCONTINUED),
		                 0, options & WNOWAIT ? 1 : 0);
		if (pid < 0) return -1;
		/* "If WNOHANG was specified and status is not available, 0
		 * shall be returned" (RETURN VALUE).  DESCRIPTION also
		 * requires infop to be distinguishable in that case;
		 * POSIX.1-2017 leaves it implementation-defined whether infop
		 * is written, and zeroing it (si_signo == 0, si_pid == 0) is
		 * what makes "nothing happened" detectable by a caller that
		 * only has the 0 return to go on. */
		if (pid == 0) {
			if (infop) memset(infop, 0, sizeof *infop);
			return 0;
		}
	} else {
		/* No WEXITED: the caller wants a stop or continue report and
		 * explicitly not an exit, so do_waitpid() must not be entered
		 * at all -- it would wait on the process handle and reap a
		 * child whose death this call did not ask to hear about.
		 * Read the report directly instead. */
		struct __child *c = job_report(want < 0 ? 0 : want, options & (WSTOPPED | WCONTINUED));

		if (c) {
			status = c->jobstat;
			pid = c->pid;
			if (!(options & WNOWAIT)) c->jobstat = 0;
		} else {
			/* Nothing pending, and nothing can make one pending
			 * later: the only thing that stops or continues a child
			 * here is this process calling kill(), and a blocked
			 * waitid() is not calling kill().  So the wait POSIX
			 * describes would be an unconditional hang.  With WNOHANG
			 * this is plainly "no status available", a 0 return;
			 * without it, ECHILD is both terminating and true --
			 * there is no child that can ever satisfy this request. */
			if (infop) memset(infop, 0, sizeof *infop);
			if (options & WNOHANG) return 0;
			errno = ECHILD;
			return -1;
		}
	}

	if (infop) {
		memset(infop, 0, sizeof *infop);
		/* "the si_signo member shall be set equal to SIGCHLD"
		 * (DESCRIPTION). */
		infop->si_signo = SIGCHLD;
		infop->si_pid = pid;
		/* The child inherited this process's immutable token identity, so
		 * its token-derived real user ID is this process's too. */
		infop->si_uid = getuid();
		if (WIFEXITED(status)) {
			infop->si_code = CLD_EXITED;
			/* For CLD_EXITED, si_status is the exit status the child
			 * passed to _exit(); for every other code it is the
			 * signal number.  All of them come straight out of the
			 * same wait status waitpid() would hand back, so waitid
			 * and waitpid can never disagree about a child. */
			infop->si_status = WEXITSTATUS(status);
		} else if (WIFSTOPPED(status)) {
			/* "the signal that caused the process to terminate, stop,
			 * or continue" -- here the stop signal kill() was called
			 * with, which is SIGSTOP or one of the three terminal
			 * stops (see sig_stops() in src/signal/signal.c). */
			infop->si_code = CLD_STOPPED;
			infop->si_status = WSTOPSIG(status);
		} else if (WIFCONTINUED(status)) {
			/* A continue has no signal of its own to carry: SIGCONT is
			 * the only thing that produces one. */
			infop->si_code = CLD_CONTINUED;
			infop->si_status = SIGCONT;
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
