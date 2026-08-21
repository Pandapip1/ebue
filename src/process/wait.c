/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Waiting for children.
 *
 * A pid is a process id; the handle needed to wait on it is kept in the
 * child table __spawn/fork filled in.  The exit code becomes a POSIX
 * wait status: an ordinary exit is (code << 8); a process ntlibc's kill
 * ended carries 128 + signo as its exit code, which is turned back into
 * "killed by signo" so that WIFSIGNALED/WTERMSIG report it.
 */
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include "libc.h"

static int encode_status(int exitcode)
{
	/* kill() here exits a process with 128 + signo; recognise that. */
	if (exitcode > 128 && exitcode < 128 + 65) {
		int sig = exitcode - 128;
		return sig & 0x7f;               /* WIFSIGNALED, WTERMSIG == sig */
	}
	if (exitcode == (int)STATUS_CONTROL_C_EXIT || (unsigned)exitcode == 0xC000013A)
		return SIGINT & 0x7f;
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
		for (i = 0; i < CHILD_MAX_; i++) {
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
		for (i = 0; i < CHILD_MAX_; i++)
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
		/* Not in the table.  Either it is not our child, or __spawn/fork
		 * could not record it because the table (CHILD_MAX_) was full.
		 * Reopen the process by pid and check that it really is ours:
		 * its InheritedFromUniqueProcessId must be us.  waitpid(-1) and
		 * wait() cannot see such children; they only scan the table. */
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
