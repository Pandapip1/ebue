/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of the <unistd.h> identity,
 * process-group, session, scheduling and host-name interfaces --
 * test/POSIX-GAP-ACCOUNTING.md's "Implemented, not clause-audited
 * (357)" first row, worked here for the two families that row's
 * predecessor sweep only ever gave a *first assertion* to:
 *
 *   src/unistd/ids.c    getuid geteuid getgid getegid getgroups
 *                       setuid seteuid setgid setegid setreuid setregid
 *                       getpgrp getpgid setpgid setpgrp setsid getsid
 *                       chown fchown lchown fchownat nice
 *   src/unistd/sleep.c  alarm pause
 *   src/unistd/gethostname.c  gethostname
 *
 * The distinction this file exists to draw, and the reason the earlier
 * sweep's "N/A -- one user, one session" is *not* repeated wholesale:
 * a degenerate identity model makes an *effect* unobservable, and that
 * is a fair N/A.  It does not make an *argument check* unobservable.
 * Every page below carries shall-fail [EINVAL]/[EPERM]/[EBADF]/[ESRCH]
 * clauses that constrain what the call may do with a bad argument no
 * matter how many users the platform has, and those are what this file
 * asserts.  Where the implementation answers "success" to a request it
 * demonstrably did not carry out, that is fenced BUG; where the effect
 * itself has nothing on NT to attach to, N/A with the mechanism named.
 *
 * Fence vocabulary is test/posix-termios.c's:
 *   #if 0 / * BUG: ... * /     spec violation, probed, not fixed here
 *   #if 0 / * UNIMPL: ... * /  genuinely not implemented
 *   #if 0 / * N/A: ... * /     a real NT mechanism makes it inapplicable
 *
 * Oracle: none of these functions makes an NT call except
 * gethostname() (a getenv) and the fd lookups they *should* be making
 * and do not.  Wine is therefore a sound oracle for essentially all of
 * it -- what is being measured is ntlibc's own C, not NT's behaviour.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ============================================================
 * getuid / geteuid / getgid / getegid
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getuid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/geteuid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getgid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getegid.html
 * ============================================================ */

/* All four pages carry the identical RETURN VALUE and ERRORS text:
 * getuid.html RETURN VALUE "The getuid() function shall always be
 * successful and no return value is reserved to indicate the error."
 * ERRORS "No errors are defined."  There is no invalid return value and
 * no argument, so the whole testable content of these four pages is
 * (a) they return, (b) they do not disturb errno, and (c) they are
 * stable across calls -- an id that changed under a caller who never
 * asked for a change would break every "compare against getuid()"
 * idiom in the library (src/misc/pwd.c, src/stat/stat.c). */
static void test_getid_always_successful(void)
{
	uid_t u, eu;
	gid_t g, eg;

	errno = 0;
	u = getuid(); eu = geteuid(); g = getgid(); eg = getegid();
	CHECK(errno == 0);		/* "No errors are defined." */

	/* stable: a second call answers the same thing */
	CHECK(getuid() == u);
	CHECK(geteuid() == eu);
	CHECK(getgid() == g);
	CHECK(getegid() == eg);

	/* getuid.html DESCRIPTION: "shall return the real user ID of the
	 * calling process" -- a real uid_t value, not the (uid_t)-1 that
	 * chown.html reserves for "do not change". */
	CHECK(u != (uid_t)-1);
	CHECK(eu != (uid_t)-1);
	CHECK(g != (gid_t)-1);
	CHECK(eg != (gid_t)-1);

	/* N/A: geteuid.html's real-vs-effective distinction.  NT has no
	 * set-user-ID bit and no saved set-user-ID -- an NT process's
	 * token is not switched by executing a file -- so nothing on this
	 * platform can ever make the effective id differ from the real
	 * one.  src/unistd/ids.c answers 1000 to all four by construction.
	 * That the two agree is asserted (test/posix-unistd.c's
	 * test_access_real_effective_uid_identical() already leans on it);
	 * that they *can* differ is unconstructible, not unimplemented. */
	CHECK(u == eu);
	CHECK(g == eg);
}

/* ============================================================
 * getgroups
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getgroups.html
 * ============================================================ */
static void test_getgroups(void)
{
	gid_t list[NGROUPS_MAX + 1];
	int n, i;

	/* DESCRIPTION: "If gidsetsize is 0, getgroups() shall return the
	 * number of group IDs that it would otherwise return without
	 * modifying the array pointed to by grouplist." */
	memset(list, 0x5a, sizeof list);
	errno = 0;
	n = getgroups(0, list);
	CHECK(n >= 0);
	for (i = 0; i < (int)(sizeof list / sizeof list[0]); i++)
		CHECK(list[i] == (gid_t)0x5a5a5a5a);	/* array untouched */

	/* RETURN VALUE: "the number of supplementary group IDs shall be
	 * returned"; DESCRIPTION: "If the effective group ID of the process
	 * is returned with the supplementary group IDs, the value returned
	 * shall always be greater than or equal to one and less than or
	 * equal to the value of {NGROUPS_MAX}+1." */
	CHECK(n >= 1 && n <= NGROUPS_MAX + 1);

	/* DESCRIPTION: "The actual number of group IDs stored in the array
	 * shall be returned", and with room for all of them the same count
	 * must come back. */
	memset(list, 0x5a, sizeof list);
	errno = 0;
	CHECK(getgroups(NGROUPS_MAX, list) == n);
	CHECK(errno == 0);

	/* DESCRIPTION: "It is implementation-defined whether getgroups()
	 * also returns the effective group ID in the grouplist array." --
	 * so the *contents* are latitude.  What is not latitude is that the
	 * first n entries were actually written: "The values of array
	 * entries with indices greater than or equal to the value returned
	 * are undefined" says nothing about the ones below it. */
	for (i = 0; i < n; i++)
		CHECK(list[i] != (gid_t)0x5a5a5a5a);

	/* src/unistd/ids.c answers a single group equal to getegid(); that
	 * is the implementation-defined choice the clause above permits,
	 * and it must at least be self-consistent. */
	if (n == 1) CHECK(list[0] == getegid());

#if 0	/* BUG: getgroups() succeeds for a negative gidsetsize.
	 * getgroups.html ERRORS: "The getgroups() function *shall* fail
	 * if: [EINVAL] The gidsetsize argument is non-zero and less than
	 * the number of group IDs that would have been returned."  -1 is
	 * non-zero and is less than the 1 this implementation would have
	 * returned, so the clause applies exactly and it is a shall-fail,
	 * not a may-fail.
	 *
	 * Mechanism: src/unistd/ids.c:20 is
	 *     int getgroups(int n, gid_t *g) { if (n > 0) g[0] = 1000; return 1; }
	 * -- n is used only to decide whether to store, never to decide
	 * whether to fail.  A negative gidsetsize therefore reports "1
	 * group ID stored" into an array it did not write, and a caller
	 * that trusts the return value reads uninitialised memory.  That
	 * is the same shape as the tcgetpgrp()/tcsetpgrp() [EBADF] defect
	 * test/posix-unistd.c already fences: an argument check that was
	 * never written, entirely separable from the single-identity
	 * design (the count this implementation returns is 1 either way).
	 * Probed on this tree: getgroups(-1, list) returns 1, errno
	 * untouched.  Re-enable when gidsetsize is validated. */
	errno = 0;
	CHECK(getgroups(-1, list) == -1 && errno == EINVAL);
#endif

	/* Not fenced, and deliberately so: [EINVAL] for a *positive*
	 * gidsetsize smaller than the count is unconstructible here rather
	 * than unimplemented.  The count is 1, and the smallest positive
	 * gidsetsize is 1, so no positive value is ever "less than the
	 * number of group IDs that would have been returned".  A one-group
	 * process cannot exhibit that error, on any implementation. */
}

/* ============================================================
 * setuid / seteuid / setgid / setegid / setreuid / setregid
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setuid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/seteuid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setgid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setegid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setreuid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setregid.html
 * ============================================================ */
static void test_setid_family(void)
{
	uid_t u = getuid();
	gid_t g = getgid();

	/* setuid.html DESCRIPTION: "If the process does not have
	 * appropriate privileges, but uid is equal to the real user ID or
	 * the saved set-user-ID, setuid() shall set the effective user ID
	 * to uid"; RETURN VALUE "Upon successful completion, 0 shall be
	 * returned."  Setting an id to the value it already has is the one
	 * request this platform can honestly grant, and it must succeed. */
	CHECK(setuid(u) == 0);
	CHECK(seteuid(u) == 0);
	CHECK(setgid(g) == 0);
	CHECK(setegid(g) == 0);

	/* setreuid.html DESCRIPTION: "If ruid or euid is -1, the
	 * corresponding effective or real user ID of the current process
	 * shall be left unchanged" -- the no-op call, which must succeed
	 * and change nothing. */
	CHECK(setreuid((uid_t)-1, (uid_t)-1) == 0);
	CHECK(setregid((gid_t)-1, (gid_t)-1) == 0);
	CHECK(setreuid(u, u) == 0);
	CHECK(setregid(g, g) == 0);

	/* setuid.html RETURN VALUE, read the other way: a *successful*
	 * setuid() has set the effective user ID to uid, so geteuid() must
	 * agree with what was just asked for.  After the identity calls
	 * above -- all of which asked for the id already in force -- the
	 * getters must still answer that id. */
	CHECK(getuid() == u && geteuid() == u);
	CHECK(getgid() == g && getegid() == g);

	/* setuid.html DESCRIPTION: "The setuid() function shall not affect
	 * the supplementary group list in any way", and setgid.html: "Any
	 * supplementary group IDs of the calling process shall remain
	 * unchanged."  Observable here through getgroups(). */
	{
		gid_t before[NGROUPS_MAX + 1], after[NGROUPS_MAX + 1];
		int nb = getgroups(NGROUPS_MAX, before);
		CHECK(setuid(u) == 0);
		CHECK(setgid(g) == 0);
		CHECK(getgroups(NGROUPS_MAX, after) == nb);
		if (nb > 0) CHECK(!memcmp(before, after, (size_t)nb * sizeof(gid_t)));
	}

#if 0	/* BUG: every set*id() call reports success for a request it did
	 * not carry out, including requests POSIX requires it to *refuse*.
	 *
	 * setuid.html ERRORS: "The setuid() function *shall* fail, return
	 * -1, and set errno to the corresponding value if one or more of
	 * the following are true: ... [EPERM] The process does not have
	 * appropriate privileges and uid does not match the real user ID
	 * or the saved set-user-ID."  seteuid.html, setgid.html,
	 * setegid.html carry the identical clause for their own id, and
	 * setreuid.html/setregid.html carry the equivalent one ("[EPERM]
	 * The current process does not have appropriate privileges, and
	 * ... an attempt was made to change the effective user ID to a
	 * value other than the real user ID or the saved set-user-ID").
	 *
	 * sysconf(_SC_SAVED_IDS) is -1 on this platform
	 * (src/unistd/sysconf.c:21), so there is no saved set-user-ID to
	 * match either, and uid 0 is not the real user ID (1000) -- the
	 * clause's precondition holds exactly.
	 *
	 * Mechanism: src/unistd/ids.c:12-19 are six one-line
	 *     int setuid(uid_t u) { (void)u; return 0; }
	 * stubs.  The argument is cast to void and 0 is returned
	 * unconditionally.  Probed on this tree: setuid(0), seteuid(0),
	 * setgid(0), setegid(0), setreuid(0,0), setregid(0,0) all return 0
	 * with errno untouched, and getuid()/geteuid()/getgid()/getegid()
	 * still answer 1000 afterwards.
	 *
	 * Classified BUG rather than N/A, and this is the distinction this
	 * file's banner draws.  "One user, so the *effect* is
	 * unobservable" is a sound N/A for the success path and
	 * test/POSIX-GAP-ACCOUNTING.md's degenerate-stub table already
	 * records it there.  It is not an argument for reporting success
	 * to `setuid(0)`: that answer is a claim the caller acts on --
	 * every privilege-dropping idiom in Unix software is
	 * `if (setuid(pw->pw_uid) != 0) abort();`, and a stub that says 0
	 * turns "refuse to run unprivileged" into "run believing the drop
	 * happened".  Returning -1/[EPERM] for any id that is not the
	 * current one is both what the page requires and what the
	 * single-identity model actually means.  Re-enable when the six
	 * stubs validate their argument. */
	errno = 0; CHECK(setuid(0) == -1 && errno == EPERM);
	errno = 0; CHECK(seteuid(0) == -1 && errno == EPERM);
	errno = 0; CHECK(setgid(0) == -1 && errno == EPERM);
	errno = 0; CHECK(setegid(0) == -1 && errno == EPERM);
	errno = 0; CHECK(setreuid(0, 0) == -1 && errno == EPERM);
	errno = 0; CHECK(setregid(0, 0) == -1 && errno == EPERM);
#endif

#if 0	/* BUG: the same six accept an id that is not a value the
	 * implementation supports at all.  setuid.html ERRORS: "[EINVAL]
	 * The value of the uid argument is invalid and not supported by
	 * the implementation" -- also a shall-fail, and the four seteuid/
	 * setgid/setegid pages plus setreuid.html/setregid.html ("[EINVAL]
	 * The value of the ruid or euid argument is invalid or
	 * out-of-range") say the same.
	 *
	 * (uid_t)-2 is chosen because (uid_t)-1 is the reserved
	 * "unchanged" marker setreuid.html gives a meaning to, so -2 is
	 * the nearest value that is unambiguously an id and unambiguously
	 * not one this library models.  Probed: all six return 0.
	 * Same mechanism and same fix as the fence above. */
	errno = 0; CHECK(setuid((uid_t)-2) == -1 && errno == EINVAL);
	errno = 0; CHECK(setgid((gid_t)-2) == -1 && errno == EINVAL);
#endif
}

/* ============================================================
 * getpgrp / getpgid / getsid / setpgid / setpgrp / setsid
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getpgrp.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getpgid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getsid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setpgid.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setpgrp.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/setsid.html
 * ============================================================ */
static void test_process_group_and_session(void)
{
	pid_t self = getpid();

	/* getpgrp.html RETURN VALUE: "The getpgrp() function shall always
	 * be successful and no return value is reserved to indicate an
	 * error."  ERRORS: "No errors are defined." */
	errno = 0;
	CHECK(getpgrp() > 0);
	CHECK(errno == 0);

	/* getpgid.html DESCRIPTION: "If pid is equal to 0, getpgid() shall
	 * return the process group ID of the calling process" -- so
	 * getpgid(0) and getpgrp() must be the same number, and so must
	 * getpgid(getpid()). */
	CHECK(getpgid(0) == getpgrp());
	CHECK(getpgid(self) == getpgrp());

	/* getsid.html DESCRIPTION: "If pid is (pid_t)0, it specifies the
	 * calling process."  Same identity for the session getter. */
	CHECK(getsid(0) == getsid(self));

	/* RETURN VALUE on both pages: a process group ID, with (pid_t)-1
	 * reserved for the error return -- so a successful answer is never
	 * -1. */
	CHECK(getpgid(0) != (pid_t)-1);
	CHECK(getsid(0) != (pid_t)-1);

	/* setpgid.html DESCRIPTION: "As a special case, if pid is 0, the
	 * process ID of the calling process shall be used.  Also, if pgid
	 * is 0, the process ID of the indicated process shall be used."
	 * RETURN VALUE: "Upon successful completion, setpgid() shall
	 * return 0."  Joining the group the process is already in is the
	 * request this platform can grant. */
	CHECK(setpgid(0, getpgrp()) == 0);
	CHECK(setpgid(self, getpgrp()) == 0);

	/* setpgrp.html RETURN VALUE: "Upon completion, setpgrp() shall
	 * return the process group ID."  ERRORS: "No errors are defined."
	 * -- so whatever it returns must be the same number getpgrp()
	 * reports afterwards. */
	errno = 0;
	CHECK(setpgrp() == getpgrp());
	CHECK(errno == 0);

	/* setsid.html RETURN VALUE: "the value of the new process group ID
	 * of the calling process" -- so it must agree with the getters. */
	CHECK(setsid() == getpgrp());
	CHECK(getsid(0) == getpgrp());

#if 0	/* BUG: getpgid() and getsid() answer for a process that does not
	 * exist.  getpgid.html ERRORS: "The getpgid() function *shall*
	 * fail if: ... [ESRCH] There is no process with a process ID equal
	 * to pid."  getsid.html carries the identical shall-fail clause.
	 *
	 * Mechanism: src/unistd/ids.c:21,25 are
	 *     pid_t getpgid(pid_t p) { (void)p; return 1; }
	 *     pid_t getsid(pid_t p)  { (void)p; return 1; }
	 * -- pid is discarded, so there is no pid for which either can
	 * fail.  Unlike the "one session" argument, this needs no session
	 * model at all to get right: src/process/children.c already tracks
	 * every process this one created and src/process/wait.c already
	 * distinguishes a live child from an unknown pid, which is exactly
	 * the lookup [ESRCH] describes.  Probed on this tree: both return
	 * 1 for pid 999999, errno untouched.
	 * Re-enable when both validate pid. */
	errno = 0;
	CHECK(getpgid(999999) == (pid_t)-1 && errno == ESRCH);
	errno = 0;
	CHECK(getsid(999999) == (pid_t)-1 && errno == ESRCH);
#endif

#if 0	/* BUG: setpgid() accepts a negative pgid and an unrelated pid.
	 * setpgid.html ERRORS, both shall-fail:
	 *   "[EINVAL] The value of the pgid argument is less than 0, or is
	 *    not a value supported by the implementation."
	 *   "[ESRCH] The value of the pid argument does not match the
	 *    process ID of the calling process or of a child process of
	 *    the calling process."
	 *
	 * Mechanism: src/unistd/ids.c:22 is
	 *     int setpgid(pid_t a, pid_t b) { (void)a; (void)b; return 0; }
	 * -- neither argument is looked at.  The [EINVAL] half is a pure
	 * range check on a signed value and needs no process-group model
	 * whatsoever; the [ESRCH] half needs the same child lookup the
	 * getpgid fence above describes.  Probed on this tree:
	 * setpgid(0, -1) and setpgid(999999, 0) both return 0.
	 * Re-enable when setpgid() validates its arguments. */
	errno = 0;
	CHECK(setpgid(0, -1) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(setpgid(999999, 0) == -1 && errno == ESRCH);
#endif

#if 0	/* UNIMPL: setsid() cannot report [EPERM], because this platform
	 * has no state in which the clause's precondition could become
	 * true and no state it could move to if it did.
	 *
	 * setsid.html DESCRIPTION: "The setsid() function shall create a
	 * new session, if the calling process is not a process group
	 * leader.  Upon return the calling process shall be the session
	 * leader of this new session ... The process group ID of the
	 * calling process shall be set equal to the process ID of the
	 * calling process."  ERRORS: "[EPERM] The calling process is
	 * already a process group leader".
	 *
	 * Read together, those two make a state machine with a testable
	 * transition: the first setsid() succeeds and leaves the process a
	 * group leader (pgid == pid), so the *second* must fail with
	 * [EPERM].  src/unistd/ids.c:24 is `pid_t setsid(void) { return 1; }`
	 * -- it always answers 1, never sets the process group ID to
	 * getpid(), and so never enters the state that would make the
	 * second call fail.  Probed: setsid() twice, both return 1, and
	 * getpgrp() is 1 while getpid() is not.
	 *
	 * UNIMPL rather than N/A, and rather than BUG: N/A would need NT
	 * to have no way to express "this process leads its own group",
	 * but the whole model here is a *chosen* fiction of one fixed
	 * session (src/unistd/ids.c's banner, src/termios/termios.c's) --
	 * "I chose not to" is UNIMPL by this project's own rule.  BUG
	 * would overstate it: unlike the set*id fences above there is no
	 * single call whose answer is a lie in isolation, only a
	 * transition that never happens.  Re-enable if session state is
	 * ever modelled. */
	CHECK(setsid() == getpid());
	CHECK(getpgrp() == getpid());
	errno = 0;
	CHECK(setsid() == (pid_t)-1 && errno == EPERM);
#endif

#if 0	/* UNIMPL: setpgrp() does not set the process group ID to the
	 * process ID.  setpgrp.html DESCRIPTION: "If the calling process
	 * is not already a session leader, setpgrp() sets the process
	 * group ID of the calling process to the process ID of the calling
	 * process."  ERRORS: "No errors are defined", so unlike setsid()
	 * there is not even a failure return to hide behind -- the call
	 * has exactly one specified effect and it does not happen.
	 * src/unistd/ids.c:23 is `pid_t setpgrp(void) { return 1; }`.
	 * Same UNIMPL reasoning as the setsid() fence above; the
	 * self-consistency that *is* checkable (setpgrp() == getpgrp())
	 * is asserted unfenced above. */
	CHECK(setpgrp() == getpid());
#endif

	/* N/A, with the mechanism: setpgid()'s [EACCES] ("the child
	 * process has successfully executed one of the exec functions")
	 * and its two remaining [EPERM] clauses, and the [EPERM] clauses
	 * of getpgid()/getsid() ("not in the same session as the calling
	 * process").  All four presuppose a second process in a
	 * *different* session or process group from the caller.  This
	 * platform has exactly one session and one process group, fixed at
	 * 1 for every process (src/unistd/ids.c), and NT has no session or
	 * process-group object for src/process/spawn.c to put a child into
	 * a different one of -- a Job object is the nearest NT construct
	 * and it is not a process group: it has no leader, no session, and
	 * no relationship to a controlling terminal.  So the *precondition*
	 * of each of those clauses is unconstructible rather than merely
	 * unimplemented, which is what separates them from the [ESRCH] and
	 * [EINVAL] fences above -- those need only a pid range check. */
}

/* ============================================================
 * chown / fchown / lchown / fchownat
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/chown.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/fchown.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/lchown.html
 * ============================================================ */
static void test_chown_family(void)
{
	struct stat before, after;
	int fd;

	fd = open("chf.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (fd < 0) return;
	CHECK(stat("chf.txt", &before) == 0);

	/* chown.html RETURN VALUE: "Upon successful completion, these
	 * functions shall return 0."  DESCRIPTION: "If owner or group is
	 * specified as (uid_t)-1 or (gid_t)-1, respectively, the
	 * corresponding ID of the file shall not be changed."
	 *
	 * The success path is already asserted by test/posix-unistd.c's
	 * test_id_session_stubs(); what is added here is the clause
	 * *pair* that a stub can still be held to -- a successful chown to
	 * the ids the file already has must leave st_uid/st_gid exactly
	 * where they were, and so must the (uid_t)-1 form. */
	CHECK(chown("chf.txt", getuid(), getgid()) == 0);
	CHECK(chown("chf.txt", (uid_t)-1, (gid_t)-1) == 0);
	CHECK(lchown("chf.txt", (uid_t)-1, (gid_t)-1) == 0);
	CHECK(fchown(fd, (uid_t)-1, (gid_t)-1) == 0);
	CHECK(fchownat(AT_FDCWD, "chf.txt", (uid_t)-1, (gid_t)-1, 0) == 0);
	CHECK(stat("chf.txt", &after) == 0);
	CHECK(after.st_uid == before.st_uid && after.st_gid == before.st_gid);

	/* chown.html DESCRIPTION: "If the specified file is a regular
	 * file, one or more of the S_IXUSR, S_IXGRP, or S_IXOTH bits of
	 * the file mode are set, and the process does not have appropriate
	 * privileges, the set-user-ID (S_ISUID) and set-group-ID (S_ISGID)
	 * bits of the file mode shall be cleared upon successful return."
	 *
	 * N/A, with the mechanism: NTFS has no set-user-ID or
	 * set-group-ID bit and src/stat/chmod.c stores no shadow for one
	 * -- the only mode bit NTFS gives meaning to is
	 * FILE_ATTRIBUTE_READONLY (that file's own banner, and
	 * test/posix-unistd.c's chmod tests).  st_mode can therefore never
	 * come back with S_ISUID set, so "shall be cleared" has nothing to
	 * clear.  Asserted in the only direction that is observable: the
	 * bits are absent before and after. */
	CHECK((before.st_mode & (S_ISUID | S_ISGID)) == 0);
	CHECK((after.st_mode & (S_ISUID | S_ISGID)) == 0);

	CHECK(close(fd) == 0);

#if 0	/* BUG: the whole chown family reports success for a path that
	 * does not exist, for the empty string, and for a path whose
	 * prefix is a regular file -- and fchown()/fchownat() for a
	 * descriptor that was never opened.
	 *
	 * chown.html ERRORS, all shall-fail:
	 *   "[ENOENT] A component of path does not name an existing file
	 *    or path is an empty string."
	 *   "[ENOTDIR] A component of the path prefix names an existing
	 *    file that is neither a directory nor a symbolic link to a
	 *    directory ..."
	 * lchown.html repeats both verbatim.  fchown.html: "The fchown()
	 * function *shall* fail if: [EBADF] The fildes argument is not an
	 * open file descriptor."  chown.html's fchownat() section:
	 * "[EBADF] The path argument does not specify an absolute path and
	 * the fd argument is neither AT_FDCWD nor a valid file descriptor
	 * open for reading or searching" and "[ENOTDIR] The path argument
	 * is not an absolute path and fd is a file descriptor associated
	 * with a non-directory file."
	 *
	 * Mechanism: src/unistd/ids.c:26-29 are four one-line stubs that
	 * cast every argument to void and return 0.  Not one of them
	 * touches __ntpath_at() or __fd_get(), which is how every other
	 * path- or fd-taking call in the library produces these errnos.
	 *
	 * Classified BUG rather than N/A on this file's banner rule.  The
	 * degenerate-stub argument -- "NT has no uid/gid to set, so the
	 * effect is unobservable" -- is sound and
	 * test/POSIX-GAP-ACCOUNTING.md records it.  It says nothing about
	 * *path resolution*: `chown("does-not-exist", ...)` returning 0 is
	 * not a statement about ownership, it is a statement that the file
	 * exists, and it is false.  Callers use exactly that: `chown()`
	 * failing with ENOENT is a standard existence probe, and an
	 * installer that chowns a list of files it has just laid down
	 * loses its only report that one of them is missing.  The check
	 * costs an __ntpath_at()/NtQueryFullAttributesFile() the stub
	 * already has every ingredient for.  Probed on this tree: all
	 * seven calls below return 0 with errno untouched.
	 * Re-enable when the four stubs resolve their path/descriptor. */
	errno = 0; CHECK(chown("chown-no-such-file", getuid(), getgid()) == -1 && errno == ENOENT);
	errno = 0; CHECK(chown("", getuid(), getgid()) == -1 && errno == ENOENT);
	errno = 0; CHECK(lchown("chown-no-such-file", getuid(), getgid()) == -1 && errno == ENOENT);
	errno = 0; CHECK(chown("chf.txt/under-a-file", getuid(), getgid()) == -1 && errno == ENOTDIR);
	errno = 0; CHECK(fchown(4096, getuid(), getgid()) == -1 && errno == EBADF);
	errno = 0; CHECK(fchownat(4096, "rel", getuid(), getgid(), 0) == -1 && errno == EBADF);
	errno = 0; CHECK(fchownat(AT_FDCWD, "chown-no-such-file", getuid(), getgid(), 0) == -1 && errno == ENOENT);
#endif

	/* fchownat()'s [EINVAL] ("The value of the flag argument is not
	 * valid") is a *may*-fail on chown.html, not a shall-fail, so
	 * accepting an undefined flag bit is permitted here and is not
	 * fenced -- unlike unlinkat(), whose flag masking was a bug and is
	 * fixed (test/posix-unistd.c's test_unlinkat() asserts it), because
	 * unlink.html makes its [EINVAL] a shall-fail and because there the
	 * accepted bit changed what the call *did*.  fchownat() does
	 * nothing either way. */
	CHECK(fchownat(AT_FDCWD, "chf.txt", (uid_t)-1, (gid_t)-1, 0x9999) == 0);

	CHECK(unlink("chf.txt") == 0);

	/* N/A, with the mechanism, for the rest of chown.html's ERRORS:
	 * [EACCES] and [EPERM] need a file whose owner differs from the
	 * caller's effective user ID, which one fixed identity cannot
	 * produce; [EROFS] needs a read-only mount; [ELOOP] needs a
	 * symbolic-link cycle, which src/internal/path.c hands to NT's own
	 * resolver rather than walking itself; [EIO]/[EINTR] are may-fail.
	 * Every one of those is unreachable *before* reaching the stub,
	 * i.e. they would be unreachable even if the four functions were
	 * fully implemented -- which is what distinguishes them from the
	 * fenced clauses above. */
}

/* ============================================================
 * alarm / pause
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/alarm.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/pause.html
 * ============================================================ */
static void test_alarm(void)
{
	/* alarm.html ERRORS: "The alarm() function is always successful,
	 * and no return value is reserved to indicate an error."  RETURN
	 * VALUE: "Otherwise, alarm() shall return 0" -- "otherwise" being
	 * "no previous alarm() request with time remaining".  With no
	 * alarm ever set, 0 is the required answer and errno must be
	 * untouched. */
	errno = 0;
	CHECK(alarm(0) == 0);
	CHECK(errno == 0);

#if 0	/* UNIMPL: alarm() never schedules anything, so it can never
	 * report time remaining.
	 *
	 * alarm.html DESCRIPTION: "The alarm() function shall cause the
	 * system to generate a SIGALRM signal for the process after the
	 * number of realtime seconds specified by seconds have elapsed.
	 * ... If seconds is 0, a pending alarm request, if any, is
	 * canceled."  RETURN VALUE: "If there is a previous alarm()
	 * request with time remaining, alarm() shall return a non-zero
	 * value that is the number of seconds until the previous request
	 * would have generated a SIGALRM signal."
	 *
	 * src/unistd/sleep.c:41 is
	 *     unsigned alarm(unsigned s) { (void)s; return 0; }
	 * -- no timer, no signal, and therefore no remaining time to
	 * report.  test/POSIX-GAP-ACCOUNTING.md's degenerate-stub table
	 * already calls this "a genuine gap, and the root of the
	 * getitimer/setitimer/ualarm undefined-ok: chain: needs a
	 * per-process timer thread delivering SIGALRM".
	 *
	 * UNIMPL, not N/A: NT has the mechanism (a waitable timer, or
	 * NtSetTimer, plus the APC delivery src/signal/signal.c would need
	 * anyway), so this is unbuilt rather than unbuildable.  The
	 * assertion below is the cheapest one that catches it and needs no
	 * signal delivery at all: schedule far enough out that no
	 * plausible scheduling delay could have expired it, then cancel
	 * and read back the remaining time.  Probed on this tree:
	 * alarm(100) returns 0 and the alarm(0) after it returns 0.
	 * Re-enable when alarm() is backed by a real timer. */
	CHECK(alarm(100) == 0);		/* no previous request */
	CHECK(alarm(0) > 0);		/* ~100 seconds still to run */
	CHECK(alarm(0) == 0);		/* and now it is cancelled */
#endif

#if 0	/* N/A: pause() cannot be called from this suite at all --
	 * calling it deadlocks the run rather than failing it.
	 *
	 * pause.html DESCRIPTION: "The pause() function shall suspend the
	 * calling thread until delivery of a signal whose action is either
	 * to execute a signal-catching function or to terminate the
	 * process."  RETURN VALUE: "there is no successful completion
	 * return value.  A value of -1 shall be returned and errno set to
	 * indicate the error."  ERRORS: "[EINTR] A signal is caught by the
	 * calling process and control is returned from the signal-catching
	 * function."
	 *
	 * Every clause on the page is conditioned on a signal arriving.
	 * src/unistd/sleep.c:33-39 implements pause() as an alertable
	 * NtDelayExecution with a maximal (0x7fffffffffffffff) timeout,
	 * and this platform has no asynchronous signal delivery to end it:
	 * src/signal/signal.c raises signals only from within the raising
	 * thread, so nothing can wake a thread that is sitting in the
	 * delay.  The call returns -1/[EINTR] only if the wait is
	 * alerted, and nothing here can alert it.
	 *
	 * N/A rather than UNIMPL, at the level of *this page*: the errno
	 * and the -1 return are already written correctly for the case
	 * they describe, and what is missing is asynchronous signal
	 * delivery, which is a signal.h gap the ledger records against
	 * that header.  This is also the one name in
	 * test/POSIX-GAP-ACCOUNTING.md's original never-asserted 112 that
	 * that sweep could not give an assertion to, for this exact
	 * reason.  It must stay fenced: enabling it hangs `make check`
	 * until the job timeout. */
	{
		int r;
		errno = 0;
		r = pause();
		CHECK(r == -1 && errno == EINTR);
	}
#endif
}

/* ============================================================
 * nice
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/nice.html
 * ============================================================ */
static void test_nice(void)
{
	/* nice.html RETURN VALUE: "Upon successful completion, nice()
	 * shall return the new nice value -{NZERO}.  Otherwise, -1 shall
	 * be returned, the nice value of the process shall not be changed,
	 * and errno shall be set to indicate the error."  APPLICATION
	 * USAGE: "As -1 is a permissible return value in a successful
	 * situation, an application wishing to check for error situations
	 * should set errno to 0, then call nice(), and if it returns -1,
	 * check to see whether errno is non-zero."
	 *
	 * nice(0) adds nothing, so it must succeed and report the current
	 * nice value relative to {NZERO} -- checked here in the errno-0
	 * form the page itself prescribes. */
	errno = 0;
	CHECK(nice(0) != -1 || errno == 0);
	CHECK(errno == 0);

	/* DESCRIPTION: "A maximum nice value of 2*{NZERO}-1 and a minimum
	 * nice value of 0 shall be imposed by the system.  Requests for
	 * values above or below these limits shall result in the nice
	 * value being set to the corresponding limit."  So a successful
	 * return is always in [-{NZERO}, {NZERO}-1], for any incr. */
	errno = 0;
	CHECK(nice(0) >= -20 && nice(0) <= 19);		/* {NZERO} >= 20 */

#if 0	/* UNIMPL: nice() ignores incr entirely, and <limits.h> does not
	 * define {NZERO}, so its return value has no defined meaning.
	 *
	 * nice.html DESCRIPTION: "The nice() function shall add the value
	 * of incr to the nice value of the calling process."  RETURN
	 * VALUE: "shall return the new nice value -{NZERO}."
	 *
	 * Two separate gaps, one fence because neither is testable without
	 * the other:
	 *
	 * 1. src/unistd/ids.c:30 is `int nice(int n) { (void)n; return 0; }`.
	 *    incr is discarded, so the nice value never changes and two
	 *    calls that asked for different priorities report the same
	 *    answer.  Probed: nice(0), nice(5) and nice(-5) all return 0.
	 *
	 * 2. include/limits.h defines no NZERO.  XBD <limits.h> lists it
	 *    ("{NZERO} Default process priority.  Minimum Acceptable
	 *    Value: 20") and nice()'s return value is specified purely in
	 *    terms of it, so a caller has nothing to interpret the return
	 *    against.  That is why the unfenced range check above has to
	 *    hardcode the minimum acceptable value instead of using the
	 *    macro.
	 *
	 * UNIMPL, not N/A: NT does have process priority --
	 * NtSetInformationProcess(ProcessBasePriority) and the
	 * PROCESS_PRIORITY_CLASS values -- and src/misc/resource.c already
	 * wraps the query side for getpriority(), so a real mapping is
	 * available and was simply not written.  "I chose not to" is
	 * UNIMPL by this project's rule.  Re-enable when nice() applies
	 * incr and <limits.h> defines NZERO. */
	CHECK(nice(5) == 5 - NZERO);
	CHECK(nice(0) == 5 - NZERO);	/* the previous call stuck */
	CHECK(nice(-5) == -1 && errno == EPERM);	/* unprivileged lowering */
#endif

	/* [EPERM] ("The incr argument is negative and the calling process
	 * does not have appropriate privileges") is the page's only error,
	 * and it is covered by the fence above rather than separately:
	 * with incr ignored there is no negative-incr path to reject, so
	 * the clause and the DESCRIPTION gap are the same defect. */
}

/* ============================================================
 * gethostname
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/gethostname.html
 * ============================================================ */
static void test_gethostname(void)
{
	char buf[HOST_NAME_MAX + 1];
	char small[8];
	size_t n;

	/* DESCRIPTION: "The gethostname() function shall return the
	 * standard host name for the current machine. ... The returned
	 * name shall be null-terminated" when namelen is sufficient.
	 * RETURN VALUE: "Upon successful completion, 0 shall be returned."
	 * ERRORS: "No errors are defined." */
	memset(buf, '@', sizeof buf);
	errno = 0;
	CHECK(gethostname(buf, sizeof buf) == 0);
	CHECK(errno == 0);
	n = strlen(buf);
	CHECK(n > 0);
	CHECK(buf[n] == 0);			/* null-terminated */

	/* DESCRIPTION: "Host names are limited to {HOST_NAME_MAX} bytes." */
	CHECK(n <= HOST_NAME_MAX);

	/* A second call must answer the same name -- the "standard host
	 * name for the current machine" is not a per-call choice. */
	{
		char again[HOST_NAME_MAX + 1];
		CHECK(gethostname(again, sizeof again) == 0);
		CHECK(!strcmp(again, buf));
	}

	/* Exactly-fits is the boundary the truncation clause hinges on:
	 * namelen == strlen(name) + 1 is sufficient, so it must succeed
	 * and be null-terminated. */
	if (n + 1 <= sizeof buf) {
		char exact[HOST_NAME_MAX + 1];
		memset(exact, '@', sizeof exact);
		errno = 0;
		CHECK(gethostname(exact, n + 1) == 0);
		CHECK(errno == 0);
		CHECK(!strcmp(exact, buf));
	}

	/* The truncation *does* happen -- what the fence below is about is
	 * only what is reported afterwards.  src/unistd/gethostname.c
	 * memcpy()s namelen bytes before returning -1, so the first
	 * namelen bytes of the name really are in the buffer, which is
	 * what the clause requires; it is unspecified whether the result
	 * is null-terminated, so only the prefix is checked. */
	if (n >= sizeof small) {
		memset(small, '@', sizeof small);
		(void)gethostname(small, 4);
		CHECK(!memcmp(small, buf, 4));
		CHECK(small[4] == '@');		/* nothing written past namelen */
	}

#if 0	/* BUG: gethostname() reports a failure POSIX does not define
	 * when the name does not fit.
	 *
	 * gethostname.html DESCRIPTION: "The namelen argument shall
	 * specify the size of the array pointed to by the name argument.
	 * The returned name shall be null-terminated, except that if
	 * namelen is an insufficient length to hold the host name, then
	 * the returned name shall be truncated and it is unspecified
	 * whether the returned name is null-terminated."  ERRORS: "No
	 * errors are defined."
	 *
	 * Truncation *is* the specified behaviour for a short buffer, so
	 * it is a successful completion and RETURN VALUE's "Upon
	 * successful completion, 0 shall be returned" applies to it.
	 * src/unistd/gethostname.c:15 instead does
	 *     if (n >= len) { memcpy(name, h, len); errno = ENAMETOOLONG; return -1; }
	 * -- it performs the required truncation and then reports it as a
	 * failure, with an errno the page does not list.  A caller
	 * following the page has no reason to look at the buffer after a
	 * -1 and loses the truncated name it is entitled to.
	 *
	 * Recorded rather than fixed, and worth noting for whoever fixes
	 * it: test/unistd.c:723 currently *pins* the present behaviour
	 * (`CHECK(gethostname(buf, 1) == -1 && errno == ENAMETOOLONG)`),
	 * so that assertion has to change in the same commit.  Several
	 * other libcs return -1/ENAMETOOLONG here too; that is a
	 * historical divergence, not a licence in this page.  Probed on
	 * this tree: gethostname(buf, 4) returns -1 with errno 36
	 * (ENAMETOOLONG) and the four truncated bytes present in buf.
	 * Re-enable when a short namelen is a successful truncation. */
	if (n >= sizeof small) {
		memset(small, '@', sizeof small);
		errno = 0;
		CHECK(gethostname(small, 4) == 0);
		CHECK(errno == 0);
		CHECK(!memcmp(small, buf, 4));
	}
#endif

	/* N/A, with the mechanism: there is no clause left on this page to
	 * reach.  It defines no errors at all, and the host name itself
	 * comes from %COMPUTERNAME% (src/unistd/gethostname.c), which is
	 * NT's own answer to the same question -- there is no second
	 * source to cross-check it against on this platform. */
}

int main(void)
{
	char tmpl[] = "posixunistdids-XXXXXX";
	char *dir = mkdtemp(tmpl);
	char origcwd[4096];

	CHECK(getcwd(origcwd, sizeof origcwd) == origcwd);
	CHECK(dir == tmpl);
	if (!dir) return 1;
	CHECK(chdir(dir) == 0);

	test_getid_always_successful();
	test_getgroups();
	test_setid_family();
	test_process_group_and_session();
	test_chown_family();
	test_alarm();
	test_nice();
	test_gethostname();

	CHECK(chdir(origcwd) == 0);
	CHECK(rmdir(dir) == 0);

	if (!fails) printf("posix-unistd-ids: all tests passed\n");
	return fails != 0;
}
