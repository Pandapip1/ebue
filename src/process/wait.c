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
 */
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
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

static int encode_status(int exitcode)
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

pid_t waitpid(pid_t pid, int *status, int options)
{
	struct __child *c;
	LARGE_INTEGER zero = 0;
	NTSTATUS st;
	PROCESS_BASIC_INFORMATION pbi;

	if (pid == -1 || pid == 0) {
		/* Any child.  With one table, scan for a done one, or wait on
		 * all live handles.  Simple approach: find the first not-yet
		 * reaped child; if WNOHANG, poll each; else wait on the first. */
		int i, any = 0;
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
		/* Not in the table.  Almost always that means it is simply not
		 * our child: the table grows on demand now (children.c), so the
		 * only way a real child misses it is an allocation failure in
		 * __child_add, whereupon __spawn/fork close the handle.  Reopen
		 * the process by pid and check that it really is ours: its
		 * InheritedFromUniqueProcessId must be us.  This can only work
		 * while the child is still running -- once it exits and nobody
		 * holds a handle, the process object is gone and the pid cannot
		 * be opened at all -- and waitpid(-1)/wait() cannot see such
		 * children either, since they only scan the table. */
		OBJECT_ATTRIBUTES oa;
		CLIENT_ID cid;
		HANDLE h;
		InitializeObjectAttributes(&oa, 0, 0, 0, 0);
		cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
		cid.UniqueThread = 0;
		st = NtOpenProcess(&h, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid);
		if (!NT_SUCCESS(st)) { errno = ECHILD; return -1; }
		st = NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof pbi, 0);
		if (!NT_SUCCESS(st) || (pid_t)pbi.InheritedFromUniqueProcessId != getpid()) {
			NtClose(h);
			errno = ECHILD;
			return -1;
		}
		st = NtWaitForSingleObject(h, 0, options & WNOHANG ? &zero : 0);
		if (st == STATUS_TIMEOUT) { NtClose(h); return 0; }
		if (!NT_SUCCESS(st)) { NtClose(h); return __set_errno_status(st); }
		st = NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof pbi, 0);
		NtClose(h);
		if (status) *status = NT_SUCCESS(st) ? encode_status((int)pbi.ExitStatus) : 0;
		return pid;
	}
	if (c->done) { if (status) *status = c->status; pid = c->pid; __child_remove(c); return pid; }

	st = NtWaitForSingleObject(c->h, 0, options & WNOHANG ? &zero : 0);
	if (st == STATUS_TIMEOUT) return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

reap:
	st = NtQueryInformationProcess(c->h, ProcessBasicInformation, &pbi, sizeof pbi, 0);
	/* Never invent a status: if the handle can't be queried, the entry is
	 * left as it is (a retry may still reach it) and the caller gets the
	 * real error rather than a fabricated "exited 0". */
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	c->status = encode_status((int)pbi.ExitStatus);
	c->done = 1;
	if (status) *status = c->status;
	pid = c->pid;
	__child_remove(c);
	return pid;
}

pid_t wait(int *status)
{
	return waitpid(-1, status, 0);
}
