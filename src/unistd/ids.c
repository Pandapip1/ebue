/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* There is one user as far as this library is concerned. */
#include <unistd.h>
#include <errno.h>
#include "libc.h"

uid_t getuid(void) { return 1000; }
uid_t geteuid(void) { return 1000; }
gid_t getgid(void) { return 1000; }
gid_t getegid(void) { return 1000; }
int setuid(uid_t u) { (void)u; return 0; }
int seteuid(uid_t u) { (void)u; return 0; }
int setgid(gid_t g) { (void)g; return 0; }
int setegid(gid_t g) { (void)g; return 0; }
int setreuid(uid_t r, uid_t e) { (void)r; (void)e; return 0; }
int setregid(gid_t r, gid_t e) { (void)r; (void)e; return 0; }
/* The supplementary group list this library reports is one entry long
 * and holds the effective group ID -- getgroups.html leaves it
 * implementation-defined whether the effective gid appears there, and
 * with one identity there is nothing else to put in it.  gidsetsize is
 * an argument, though, and gets read as one: [EINVAL] is a *shall*-fail
 * for a gidsetsize "non-zero and less than the number of group IDs that
 * would have been returned", which every negative value is, given a
 * count of 1.  gidsetsize 0 asks for the count alone and must not touch
 * grouplist -- callers pass a null pointer for that form. */
int getgroups(int n, gid_t *g)
{
	const int held = 1;
	if (n != 0 && n < held) { errno = EINVAL; return -1; }
	if (n != 0) g[0] = getegid();
	return held;
}
pid_t getpgrp(void) { return 1; }

/* Does a process with this process ID exist, as far as this process can
 * tell?  getpgid.html and getsid.html both make "[ESRCH] There is no
 * process with a process ID equal to pid" a *shall*-fail, and that
 * clause is about the existence of a process rather than about
 * sessions: it binds a one-session implementation exactly as much as
 * any other, which is why the two getters below answer a fixed 1 (the
 * single process group / single session this platform has) but only
 * for a pid that names something.
 *
 * Existence is decided the way kill() and getpriority() already decide
 * it (src/signal/signal.c, src/misc/resource.c), in three steps:
 *
 *   - pid 0 and this process's own pid are the caller, which exists by
 *     construction -- "If pid is equal to 0, getpgid() shall return the
 *     process group ID of the calling process".  Answered without an NT
 *     call, so the common form of both calls stays free of one.
 *   - a pid in the child table (src/process/children.c) is a process
 *     this one created and has not yet reaped.  This arm is not an
 *     optimisation of the NtOpenProcess below, because the two answer
 *     different questions: POSIX existence lasts until wait() collects
 *     the pid -- an exited-but-unreaped child is still a process, and
 *     the table entry src/process/wait.c holds open is precisely that
 *     state -- while openability by CLIENT_ID is a property of the NT
 *     process *object*, which the two platforms disagree about once it
 *     has exited (see wait.c's reopen-by-pid discussion: Wine and
 *     Windows differ on whether an exited pid can be opened at all).
 *     Consulting the table first is what keeps a zombie child from
 *     being reported nonexistent on the platform that says no.
 *   - anything else is put to the object manager by CLIENT_ID.
 *     STATUS_INVALID_CID -- and any other failure that is not a refusal
 *     -- means there is no such process.  STATUS_ACCESS_DENIED means
 *     the process is there and merely not ours to open, which is
 *     existence, not [ESRCH]; it cannot become [EPERM] here either,
 *     since that clause of both pages is about a process in a
 *     *different session* and there is only the one.
 *
 * A negative pid names no process (kill() answers ESRCH for one too),
 * and is rejected without troubling NT: NtOpenProcess takes an unsigned
 * CLIENT_ID, so sign-extending -1 into it would ask about pid
 * 0xffffffffffffffff instead of reporting the error.
 */
static int pid_exists(pid_t p)
{
	HANDLE h;
	NTSTATUS st;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;

	if (p == 0 || p == getpid()) return 1;
	if (p < 0) return 0;
	if (__child_find((int)p)) return 1;

	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	cid.UniqueProcess = (HANDLE)(ULONG_PTR)p;
	cid.UniqueThread = 0;
	st = NtOpenProcess(&h, PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid);
	if (!NT_SUCCESS(st)) return st == STATUS_ACCESS_DENIED;
	NtClose(h);
	return 1;
}

pid_t getpgid(pid_t p)
{
	if (!pid_exists(p)) { errno = ESRCH; return -1; }
	return 1;
}
int setpgid(pid_t a, pid_t b) { (void)a; (void)b; return 0; }
pid_t setpgrp(void) { return 1; }
pid_t setsid(void) { return 1; }
pid_t getsid(pid_t p)
{
	if (!pid_exists(p)) { errno = ESRCH; return -1; }
	return 1;
}
int chown(const char *p, uid_t u, gid_t g) { (void)p; (void)u; (void)g; return 0; }
int fchown(int f, uid_t u, gid_t g) { (void)f; (void)u; (void)g; return 0; }
int lchown(const char *p, uid_t u, gid_t g) { (void)p; (void)u; (void)g; return 0; }
int fchownat(int d, const char *p, uid_t u, gid_t g, int f) { (void)d; (void)p; (void)u; (void)g; (void)f; return 0; }
int nice(int n) { (void)n; return 0; }
int chroot(const char *p) { (void)p; errno = EPERM; return -1; }
int issetugid(void) { return 0; }
char *getlogin(void)
{
	extern char *getenv(const char *);
	char *u = getenv("USERNAME");
	return u ? u : getenv("USER");
}
int getlogin_r(char *buf, size_t n)
{
	char *l = getlogin();
	size_t i;
	if (!l) return ENXIO;
	for (i = 0; l[i] && i + 1 < n; i++) buf[i] = l[i];
	if (l[i]) return ERANGE;
	buf[i] = 0;
	return 0;
}
