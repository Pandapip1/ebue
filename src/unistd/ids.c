/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* There is one user as far as this library is concerned. */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

uid_t getuid(void) { return 1000; }
uid_t geteuid(void) { return 1000; }
gid_t getgid(void) { return 1000; }
gid_t getegid(void) { return 1000; }
/* The one identity this library has, and the only id any set*id() call
 * can be asked for and granted.  It is what getuid()/geteuid()/
 * getgid()/getegid() above answer and what src/stat/stat.c puts in
 * st_uid/st_gid. */
#define ID_SELF 1000

/* Which ids exist at all, as opposed to which ids may be assumed.
 *
 * setuid.html ERRORS separates two shall-fail answers, and a stub that
 * returns 0 gives neither:
 *   "[EINVAL] The value of the uid argument is invalid and not supported
 *    by the implementation."
 *   "[EPERM] The process does not have appropriate privileges and uid
 *    does not match the real user ID or the saved set-user-ID."
 * seteuid.html, setgid.html and setegid.html carry the identical pair
 * for their own id, and setreuid.html/setregid.html the equivalent
 * ("[EINVAL] The value of the ruid or euid argument is invalid or
 * out-of-range").
 *
 * Answering 0 to setuid(0) is the dangerous one.  Every
 * privilege-dropping idiom in Unix software is
 *     if (setuid(pw->pw_uid) != 0) abort();
 * and a call that reports success without dropping anything turns
 * "refuse to run privileged" into "run believing the drop happened".
 * sysconf(_SC_SAVED_IDS) is -1 here (src/unistd/sysconf.c), so there is
 * no saved set-user-ID for a request to match either: the [EPERM]
 * precondition holds for every id but ID_SELF.
 *
 * The [EINVAL]/[EPERM] line has to be drawn somewhere, because with one
 * identity *no* other id can be assumed and the two clauses would
 * otherwise collapse into one.  Drawn here at the top half of uid_t:
 * uid_t and gid_t are 32-bit unsigned (include/alltypes.h.in), POSIX
 * already reserves (uid_t)-1 in that half as setreuid()'s "leave this
 * one alone" marker, and this implementation reserves the rest of it as
 * not-an-id.  That is what makes the range a *supported-value* question
 * rather than a privilege question: a caller arriving with (uid_t)-2 has
 * almost always mishandled the -1 marker or mixed a signed id with an
 * unsigned one, and [EINVAL] says so, where [EPERM] would tell it the
 * value was a real id it merely could not have.  Ids below the line are
 * well-formed and simply not this process's, which is [EPERM].
 *
 * Note the asymmetry with getgroups() below: this is a *choice about the
 * id space*, documented here so it is not mistaken for a platform fact.
 */
static int id_supported(uid_t id) { return id <= 0x7fffffffu; }

/* The shared body of all six: reject what is not an id, refuse what is
 * an id but not ours, and grant the request that is already true. */
static int set_one_id(uid_t id)
{
	if (!id_supported(id)) { errno = EINVAL; return -1; }
	if (id != (uid_t)ID_SELF) { errno = EPERM; return -1; }
	return 0;
}

/* setreuid.html DESCRIPTION: "If ruid or euid is -1, the corresponding
 * effective or real user ID of the current process shall be left
 * unchanged" -- so (uid_t)-1 is the one value in the reserved half that
 * these two must accept, and it asks for nothing. */
static int set_two_ids(uid_t r, uid_t e)
{
	if (r != (uid_t)-1 && set_one_id(r) < 0) return -1;
	if (e != (uid_t)-1 && set_one_id(e) < 0) return -1;
	return 0;
}

int setuid(uid_t u) { return set_one_id(u); }
int seteuid(uid_t u) { return set_one_id(u); }
int setgid(gid_t g) { return set_one_id((uid_t)g); }
int setegid(gid_t g) { return set_one_id((uid_t)g); }
int setreuid(uid_t r, uid_t e) { return set_two_ids(r, e); }
int setregid(gid_t r, gid_t e) { return set_two_ids((uid_t)r, (uid_t)e); }
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
/* setpgid()'s [ESRCH] asks a *narrower* question than pid_exists()
 * above, so it gets its own helper rather than that one.  getpgid.html
 * and getsid.html fail for a pid that names no process at all;
 * setpgid.html fails for a pid that names no process *of the caller's*:
 * "[ESRCH] The value of the pid argument does not match the process ID
 * of the calling process or of a child process of the calling process."
 * A pid belonging to some unrelated process therefore has two different
 * right answers on this page and that one -- getpgid() must answer for
 * it, setpgid() must refuse it -- so reusing pid_exists() here would be
 * wrong in exactly the case the clause is about, not merely wasteful.
 *
 * The narrowing is what removes the NT call: pid_exists()'s third arm
 * puts an unknown pid to the object manager because "does this process
 * exist" is a question about the whole machine, but "is this a child of
 * mine" is answerable entirely from this process's own bookkeeping.
 * The child table (src/process/children.c) *is* that set: __child_add()
 * records every pid fork()/__spawn() creates and __child_remove() drops
 * it when wait() collects it, which is precisely POSIX's lifetime for
 * "a child process of the calling process" -- an exited-but-unreaped
 * child is still one, and a reaped one is not.  So setpgid() makes no
 * NT call for any argument.
 *
 * pid 0 is the caller by DESCRIPTION ("if pid is 0, the process ID of
 * the calling process shall be used"), and is answered before the table
 * is consulted for a second reason: __child_find(0) would match the
 * first *free* slot, since a free slot is one whose pid is 0.
 *
 * The two failures are checked pid-first because pgid cannot be fully
 * resolved until pid is: "if pgid is 0, the process ID of the indicated
 * process shall be used", and there is no indicated process to take it
 * from when pid names nothing of ours.
 *
 * [EINVAL] is then the range check its clause opens with -- "The value
 * of the pgid argument is less than 0, or is not a value supported by
 * the implementation" -- and only that.  Deciding which non-negative
 * pgids are "supported" would take the process-group model this file
 * deliberately does not have (getpgrp() answers a fixed 1 for every
 * process, and test/posix-unistd-ids.c's setsid()/setpgrp() fences
 * record that as a chosen fiction rather than an oversight), so a
 * request naming some other group is still granted as the no-op it has
 * always been.  posix_spawn()'s POSIX_SPAWN_SETPGROUP reaches the
 * opposite conclusion from the same sentence (src/process/posix_spawn.c
 * refuses any pgroup but getpgrp()'s) because it has to decide, at
 * process creation, whether it can honour a flag it was handed; nothing
 * about that binds the plain no-op case here.
 */
static int pid_is_self_or_child(pid_t p)
{
	if (p == 0 || p == getpid()) return 1;
	if (p < 0) return 0;
	return __child_find((int)p) != 0;
}

int setpgid(pid_t pid, pid_t pgid)
{
	if (!pid_is_self_or_child(pid)) { errno = ESRCH; return -1; }
	if (pgid < 0) { errno = EINVAL; return -1; }
	return 0;
}
pid_t setpgrp(void) { return 1; }
pid_t setsid(void) { return 1; }
pid_t getsid(pid_t p)
{
	if (!pid_exists(p)) { errno = ESRCH; return -1; }
	return 1;
}
/* There is nothing for the chown family to set -- NT has no POSIX owner
 * or group, and st_uid/st_gid are the fixed ID_SELF -- but "there is no
 * ownership to change" is not "there is no path to resolve".
 *
 * chown.html ERRORS, all shall-fail:
 *   "[ENOENT] A component of path does not name an existing file or path
 *    is an empty string."
 *   "[ENOTDIR] A component of the path prefix names an existing file
 *    that is neither a directory nor a symbolic link to a directory ..."
 * lchown.html repeats both.  fchown.html: "[EBADF] The fildes argument
 * is not an open file descriptor."  chown.html's fchownat() section adds
 * [EBADF] for a dirfd that is neither AT_FDCWD nor a valid descriptor
 * and [ENOTDIR] for one that is not a directory.
 *
 * chown("does-not-exist", ...) returning 0 is not a statement about
 * ownership, it is a statement that the file exists, and it is false: an
 * installer chowning a list of files it has just laid down loses its
 * only report that one of them is missing, and `chown()` failing with
 * ENOENT is a standard existence probe.  So the path is resolved and the
 * object opened for FILE_READ_ATTRIBUTES, which is exactly the evidence
 * those clauses ask for and nothing more; the handle is closed again
 * without a write of any kind.
 *
 * __ntpath_at() produces the empty-path [ENOENT], the dirfd [EBADF]/
 * [ENOTDIR] and the path-prefix [ENOTDIR] itself (src/internal/path.c),
 * so only the final open is left to this function.
 *
 * fchownat()'s [EINVAL] for an unrecognised flag is a *may*-fail on
 * chown.html, unlike unlinkat()'s, and accepting the bits is the
 * behaviour test/posix-unistd-ids.c pins; only AT_SYMLINK_NOFOLLOW is
 * read out of them.  FILE_OPEN_FOR_BACKUP_INTENT, and neither
 * FILE_DIRECTORY_FILE nor FILE_NON_DIRECTORY_FILE, so that the call
 * works on a directory and on a regular file alike. */
static int chown_resolve(int dirfd, const char *path, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	OBJECT_ATTRIBUTES *oa;
	HANDLE h;
	NTSTATUS st;
	ULONG options;

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT |
		(flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0);
	oa = &np.oa;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, oa, &io, FILE_SHARE_VALID_FLAGS, options);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	NtClose(h);
	return 0;
}

int fchownat(int d, const char *p, uid_t u, gid_t g, int f)
{
	(void)u; (void)g;
	return chown_resolve(d, p, f);
}
int chown(const char *p, uid_t u, gid_t g) { return fchownat(AT_FDCWD, p, u, g, 0); }
int lchown(const char *p, uid_t u, gid_t g) { return fchownat(AT_FDCWD, p, u, g, AT_SYMLINK_NOFOLLOW); }
int fchown(int f, uid_t u, gid_t g) { (void)u; (void)g; return __fd_get(f) ? 0 : -1; }
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
