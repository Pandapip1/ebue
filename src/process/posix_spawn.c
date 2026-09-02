/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_spawn() and posix_spawnp() -- see
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawn.html
 *
 * Almost all of this is already done by __spawn() (src/process/spawn.c),
 * which is what execve(), fork() and system() are built on.  Two things
 * are added here: the POSIX interface shape, and the replay of a
 * posix_spawn_file_actions_t.
 *
 *
 * Replaying file actions without a child to replay them in
 * -------------------------------------------------------
 *
 * POSIX describes posix_spawn() as a fork() whose child performs the
 * recorded actions and then execs -- "The file actions specified by the
 * spawn file actions object shall be performed in the order in which
 * they were added" (DESCRIPTION, step 3 of four).  There is no child to
 * perform them in here: NT starts a process from an image file, so
 * __spawn() hands the future child its descriptor table up front, as a
 * table of inheritable handles packed into RTL_USER_PROCESS_PARAMETERS'
 * RuntimeData (src/internal/fd.c __fd_runtime_data).
 *
 * So the actions are performed in the *parent*, on the parent's own
 * descriptor table, immediately before __spawn() reads it, and undone
 * immediately after.  The child sees exactly the table the actions
 * produced, which is what the spec's four steps are specified to
 * produce, and the parent gets its own table back.
 *
 * Three things make that safe here rather than merely expedient:
 *
 *   - ntlibc has no threads, so nothing else can observe or race the
 *     parent's table during the window.  (This is the design
 *     test/posix-dl.c's own file-actions fence proposed, for this
 *     reason.)
 *
 *   - The save is of the *table slot*, not of a descriptor: struct __fd
 *     is copied out verbatim and the slot zeroed, so the saved HANDLE
 *     is no longer reachable from __fds[] at all.  That matters because
 *     __fd_runtime_data() rewrites the handle of every inheritable slot
 *     in place (duplicating it OBJ_INHERIT and closing the original);
 *     a handle it can still see is not one that can be restored
 *     afterwards.  Removing the slot from the table first is what keeps
 *     the saved handle valid, and every handle in the table is unique
 *     -- dup()/dup2() here always make a *new* NT handle
 *     (src/unistd/dup.c) -- so blanking one slot can never orphan
 *     another.
 *
 *   - Restoring closes whatever the actions left in the slot before
 *     writing the original back, so a descriptor created for the child
 *     does not leak, and an inheritable handle created for the child
 *     does not linger to be picked up by the *next* spawn (the same
 *     hazard src/process/spawn.c's closed_placeholder() cleanup and
 *     src/process/fork.c's comment both guard against).
 *
 * A failing action therefore fails posix_spawn() itself, with the errno
 * of the underlying close()/dup2()/open() as its return value -- the
 * first of the two dispositions ERRORS allows ("an error value shall be
 * returned as described by close(), dup2(), and open(), respectively
 * (or, if the error occurs after the calling process successfully
 * returns, the child process shall exit with exit status 127)").  No
 * process is created in that case, and no exit status 127 is ever
 * manufactured here.
 *
 *
 * Which spawn attributes can be acted on
 * --------------------------------------
 *
 * A flag this platform cannot honour makes posix_spawn() fail.  It is
 * not ignored: a spawn that quietly drops POSIX_SPAWN_SETSCHEDULER and
 * returns 0 has told its caller the child runs at a priority it does
 * not run at.  Each rejection below returns the error POSIX's own
 * ERRORS section routes that flag to, so the failure is the one a
 * portable caller is already written to expect:
 *
 *   POSIX_SPAWN_SETSIGDEF -- honoured, and satisfied by construction.
 *     "the signals ... shall be set to their default actions in the
 *     child".  An NT process created by RtlCreateUserProcess runs its
 *     own crt1 before main(), and src/signal/signal.c's disposition
 *     table (`handlers[]`) is a static: every signal in every fresh
 *     child is already SIG_DFL, whatever subset the caller names.
 *     Nothing to do, and nothing being faked -- the postcondition holds.
 *
 *   POSIX_SPAWN_SETSIGMASK -- honoured, on NT.  An empty mask is true
 *     by construction (`blocked` in signal.c is a static, so a fresh
 *     child's mask already starts empty).  A non-empty mask rides an
 *     ntlibc-specific trailer on the same RuntimeData blob that already
 *     carries the inherited-descriptor table (src/internal/nt/
 *     plat_fd_init.c's SIG_RUNTIME_MAGIC), set by
 *     __spawn_set_pending_sigmask() (libc.h) immediately before
 *     __spawn() and read back by __fd_init() before the child's main()
 *     -- in fact before anything of the child's own runs at all -- so
 *     the mask is in place before the very first instruction that could
 *     observe it. This is the case GNU make, the consumer this header
 *     was written for, hits least often: it calls sigemptyset() and
 *     then posix_spawnattr_setsigmask() with that empty set (src/job.c
 *     child_execute_job), precisely to unblock everything in the child
 *     -- the already-true-by-construction case above -- but a caller
 *     that wants the opposite, a specific signal held blocked across
 *     exec, is now delivered it for real rather than told EINVAL.
 *
 *     Not equivalent to POSIX's promise in one respect: on POSIX the
 *     kernel carries the mask across exec, so it applies to *any*
 *     image; this trailer reaches an ntlibc-built child only, and does
 *     nothing for cmd.exe or any other program exec'd this way (there
 *     is no other program this library's own __fd_init() runs inside
 *     of to read it). Refused with EINVAL on Linux, unchanged: the
 *     mechanism above is NT-specific and nothing here has built or
 *     verified an equivalent there. See test/posix-spawn.c's fence.
 *
 *   POSIX_SPAWN_RESETIDS -- honoured, and inapplicable.  "reset the
 *     effective user ID ... to the real user ID".  An NT access token
 *     has no real/effective/saved triple to differ (test/posix-dl.c
 *     records this as N/A with that mechanism), so the postcondition
 *     is unconditionally true and there is nothing to reset.
 *
 *   POSIX_SPAWN_SETPGROUP -- honoured only for the process group the
 *     caller is already in.  src/unistd/ids.c keeps that group as
 *     per-process bookkeeping (a spawned child starts in the group
 *     every process is born into rather than inheriting the caller's,
 *     which that file's banner records), so the group named by the
 *     attribute is one this library can neither create nor join for a
 *     child.  Note that NT is not short of a process-group concept:
 *     console process groups are created by CreateProcess's
 *     CREATE_NEW_PROCESS_GROUP and are the target of
 *     GenerateConsoleCtrlEvent's dwProcessGroupId, which is
 *     job-control signal delivery to a group.  What NT does not
 *     have is a way to *join* one: a console process group's members
 *     are exactly the descendants of its root process, its id is
 *     always that root's pid, and no call places a process into a
 *     pre-existing group it does not descend from.  So a spawn-pgroup
 *     naming the caller's own group is accepted -- for a caller that
 *     has not moved itself out of the group every process is born
 *     into, which is the only case the child can be observed in at
 *     all, it is already true of the child -- and anything else,
 *     including 0, "put the child in a new group of its own", is
 *     refused with EINVAL, which is what ERRORS routes here ("an error
 *     value shall be returned as described by setpgid()", whose
 *     "[EINVAL] The value of the pgid argument ... is not a value
 *     supported by the implementation" is exactly this).
 *
 *   POSIX_SPAWN_SETSCHEDPARAM / POSIX_SPAWN_SETSCHEDULER -- honoured,
 *     on NT, using the same suspended-process window
 *     __spawn_set_pending_priority()'s own comment (libc.h) and
 *     src/process/nt/plat_process.c describe: __plat_priority_set() is
 *     called on the child's own process handle before its first
 *     instruction ever runs, the identical call src/misc/resource.c's
 *     own setpriority() makes for a non-self target.
 *
 *     The POSIX shape does not survive the translation, and that has
 *     not changed: NT has priorities but no SCHED_FIFO/SCHED_RR/
 *     SCHED_OTHER policy distinction, so sched_setscheduler()'s policy
 *     argument has no valid value here and <sched.h> deliberately does
 *     not claim the _POSIX_PRIORITY_SCHEDULING option group at all
 *     (Issue 6 removed [ENOSYS] from sched_setscheduler() on the
 *     grounds that stubs need not be provided at all -- not that this
 *     matters here, since this platform accepts rather than stubs).
 *     `policy` is therefore accepted unconditionally, whatever value it
 *     holds, and only `sched_priority` is applied, run through
 *     nice_from_sched_priority()'s own comment (below) for the mapping
 *     used and why it is an admitted invention rather than a specified
 *     one. Best-effort past that point, like every other caller of
 *     __plat_priority_set(): a target this process has no privilege to
 *     raise simply keeps its default priority, silently, which is not
 *     this flag's failure to report -- posix_spawn() itself still
 *     succeeds, the same way setpriority() already tolerates the NT
 *     call it wraps failing without failing itself.
 *
 *     Refused with EINVAL on Linux, unchanged: real POSIX scheduling
 *     policies exist there, so nothing here has invented a substitute
 *     for lacking one the way NT genuinely lacks one, and nothing here
 *     has built or verified applying `sched_priority` for real through
 *     that backend's own __plat_process_spawn().  spawnattr.c's own
 *     accessors still store and return the value unconditionally,
 *     whether or not this function goes on to act on it.
 *
 *   POSIX_SPAWN_USEVFORK -- not POSIX; accepted, and satisfied by
 *     construction.  __spawn() never copies the parent's address space,
 *     which is the entire content of the flag.
 *
 * Any other bit is EINVAL, again by ERRORS' "the value specified by ...
 * attrp is invalid".
 *
 *
 * errno
 * -----
 *
 * "Upon successful completion, posix_spawn() and posix_spawnp() shall
 * return the process ID of the child process to the parent process, in
 * the variable pointed to by a non-NULL pid argument, and shall return
 * zero as the function return value.  Otherwise, no child process shall
 * be created, the value stored into the variable pointed to by a
 * non-NULL pid is unspecified, and an error number shall be returned as
 * the function value to indicate the error."  The error goes in the
 * return value and *not* in errno, which is the single most commonly
 * botched thing about these two functions -- so errno is captured on
 * entry and put back on every path out, including the successful one.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include "libc.h"
#include "spawn_internal.h"
#include "plat_fd.h"

/* signal.c always provides this helper; signal.h exposes its public
 * declaration only for BSD/GNU feature profiles.  The implementation uses
 * it internally regardless of the caller-facing feature selection. */
int sigisemptyset(const sigset_t *);

/* One saved descriptor-table slot.
 *
 * `slot` is the struct __fd as it stood before the actions ran; the
 * table entry it came from was zeroed at the same moment, so nothing in
 * __fds[] refers to slot.h any more and __fd_runtime_data() cannot
 * rewrite it out from under the save. */
struct saved_slot {
	int fd;
	struct __fd slot;
};

/* Vacate fd so an action can put something there, remembering the
 * parent's contents the *first* time -- an fd named by two actions is
 * saved once, by what was in it before any of them ran, and the second
 * action closes what the first left rather than overwriting it.
 * (Overwriting is not a slow leak: it strands an inheritable handle to
 * something like a pipe write end in the parent forever, so the parent's
 * own read of that pipe never sees EOF.  test_order_two_targets() in
 * test/posix-spawn.c is the case that shows it.)
 *
 * Returns 0, or -1 if the save array is full, which cannot happen -- it
 * is sized at one entry per action and each action vacates at most one
 * slot -- but is checked rather than assumed. */
/* nsv is required: dereferenced unconditionally at entry
 * (`for (i = 0; i < *nsv; i++)`), and every real call site passes
 * &nsv, a local, never NULL. sv is left unmarked -- it is genuinely
 * only reached when cap >= 1 in every real call (do_action() is only
 * ever invoked with the cap spawn_common() sized to fa->__len, always
 * >= 1 there), but nothing in take_slot()'s own signature or callers
 * documents that as a hard invariant the way nsv's unconditional
 * access does. */
static int take_slot(struct saved_slot *sv, int *nsv, int cap, int fd)
    __attribute__((nonnull(2)));
static int take_slot(struct saved_slot *sv, int *nsv, int cap, int fd) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int i;
	for (i = 0; i < *nsv; i++) {
		if (sv[i].fd != fd) continue;
		if (__fds[fd].h) __plat_close(__fds[fd].h);
		memset(&__fds[fd], 0, sizeof __fds[fd]);
		return 0;
	}
	if (*nsv >= cap) return -1;
	sv[*nsv].fd = fd;
	sv[*nsv].slot = __fds[fd];
	(*nsv)++;
	memset(&__fds[fd], 0, sizeof __fds[fd]);
	return 0;
}

/* Put every saved slot back, closing whatever the actions left behind
 * first.  Called on the success path and on every failure path, so the
 * parent's table is identical afterwards either way. */
static void restore_slots(struct saved_slot *sv, int nsv)
{
	int i;
	for (i = nsv - 1; i >= 0; i--) {
		int fd = sv[i].fd;
		if (__fds[fd].h) __plat_close(__fds[fd].h);
		__fds[fd] = sv[i].slot;
	}
}

/* Perform one action.  Returns 0, or the error number to hand back. */
/* a is required (`switch (a->kind)` dereferences it unconditionally at
 * entry); sv/nsv are left unmarked -- both are only ever forwarded
 * into take_slot(), never dereferenced by do_action() itself. */
static int do_action(const struct __spawn_action *a, struct saved_slot *sv, int *nsv, int cap)
    __attribute__((nonnull(1)));
static int do_action(const struct __spawn_action *a, struct saved_slot *sv, int *nsv, int cap)
{
	switch (a->kind) {
	case __SPAWN_CLOSE:
		/* "as if close(fildes) had been called".  A close of a
		 * descriptor that is already closed is left as a success
		 * rather than reported as EBADF: the action's whole purpose
		 * is the child's *post*condition ("fildes is not open"), that
		 * postcondition already holds, and a caller that lists every
		 * descriptor it wants shut has no way to know which of them
		 * happen to be open.  glibc's posix_spawn does the same. */
		if (take_slot(sv, nsv, cap, a->fd) < 0) return ENOMEM;
		return 0;

	case __SPAWN_DUP2: {
		/* The duplicate is made before the target slot is vacated,
		 * not after, because adddup2(fd, fd) names one slot as both
		 * source and target -- vacating first would close the very
		 * handle about to be duplicated.
		 *
		 * OBJ_INHERIT, and O_CLOEXEC dropped: a descriptor a file
		 * action names is one the caller wants the child to have.
		 * That is what dup2() does here too (src/unistd/dup.c dup_to
		 * with cloexec=0), and it is what makes adddup2(fd, fd) mean
		 * anything -- POSIX's step 4 closes every FD_CLOEXEC
		 * descriptor in the child, so naming a descriptor as its own
		 * dup2 target is how a caller says "keep this one". */
		__plat_handle_t h;
		unsigned flags;
		int type;
		struct __fd *f = __fd_get(a->fd);
		if (!f) return EBADF;
		flags = f->flags & ~(unsigned)O_CLOEXEC;
		type = f->type;
		if (__plat_dup(f->h, 1, &h) < 0) return errno;
		if (take_slot(sv, nsv, cap, a->newfd) < 0) { __plat_close(h); return ENOMEM; }
		__fd_install_at(a->newfd, h, flags, type);
		return 0;
	}

	case __SPAWN_OPEN: {
		/* "as if open() had been called ... and the returned file
		 * descriptor, if not fildes, had been changed to fildes"
		 * (posix_spawn_file_actions_addopen.html DESCRIPTION).  open()
		 * hands back the lowest free slot, which may or may not be
		 * fildes; the slot has just been vacated, so it often is. */
		int t;
		if (take_slot(sv, nsv, cap, a->fd) < 0) return ENOMEM;
		t = open(a->path, a->oflag, a->mode);
		if (t < 0) return errno;
		if (t != a->fd) {
			if (dup2(t, a->fd) < 0) { int e = errno; (void)close(t); return e; }
			(void)close(t);
		}
		return 0;
	}
	default:
		return EINVAL;
	}
}

/* Everything posix_spawn() must decide *before* it starts editing the
 * descriptor table, so that a rejected attribute costs no undo.
 * Returns 0 or the error number.
 *
 * POSIX_SPAWN_SETSIGMASK (non-empty mask) and POSIX_SPAWN_SETSCHEDPARAM/
 * POSIX_SPAWN_SETSCHEDULER are accepted here on NT, not refused: both
 * ride the suspended-process window before the child's first
 * instruction, and (for the mask) the same parent-to-child channel (see
 * __spawn_set_pending_sigmask()/__spawn_set_pending_priority(), libc.h,
 * and src/process/nt/plat_process.c/src/internal/nt/plat_fd_init.c for
 * where each is actually consumed).  Left refused on Linux: this file is
 * portable, but the mechanism each flag rides is NT-specific, and
 * nothing here has built or verified a Linux equivalent -- only the NT
 * implementation of __plat_process_spawn() does anything with either
 * flag. */
static int check_attr(const posix_spawnattr_t *at)
{
	short f;
	if (!at) return 0;
	f = at->__flags;
	if (f & ~(short)(POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF
	                 | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSCHEDPARAM
	                 | POSIX_SPAWN_SETSCHEDULER | POSIX_SPAWN_USEVFORK))
		return EINVAL;
#if defined(__linux__)
	if (f & (POSIX_SPAWN_SETSCHEDPARAM | POSIX_SPAWN_SETSCHEDULER)) return EINVAL;
	if ((f & POSIX_SPAWN_SETSIGMASK) && !sigisemptyset(&at->__sigmask)) return EINVAL;
#endif
	if ((f & POSIX_SPAWN_SETPGROUP) && at->__pgroup != getpgrp()) return EINVAL;
	return 0;
}

/* nice_from_sched_priority(): sched_priority has no POSIX scheduling
 * policy behind it on this platform to interpret it against (this
 * file's own banner, POSIX_SPAWN_SETSCHEDPARAM/SETSCHEDULER) -- so
 * there is no specified mapping onto NT's own priority classes, only
 * an invented one.  The one used here is the least invention
 * available: treat the number as if it already were a nice value and
 * clamp it exactly the way setpriority() clamps a caller-supplied one
 * for a target this process has no elevated privilege over
 * (src/misc/resource.c: never below 0), so applying it can only ever
 * lower or hold the child's priority, never silently fail to raise one
 * the caller thought it had. */
static int nice_from_sched_priority(int sched_priority)
{
	if (sched_priority < 0) return 0;
	if (sched_priority > NZERO - 1) return NZERO - 1;
	return sched_priority;
}

static int spawn_common(pid_t *pid, const char *path,
                        const posix_spawn_file_actions_t *fa,
                        const posix_spawnattr_t *at,
                        char *const argv[], char *const envp[], int use_path)
{
	struct saved_slot *sv = 0;
	int nsv = 0, cap = 0;
	int i, rc, child = -1, saved_errno = errno;
	char *full = 0;

	rc = check_attr(at);
	if (rc) goto out;

	/* posix_spawnp() "shall do a path search" for a file argument with
	 * no slash; posix_spawn() never does one.  __find_program() takes
	 * that as its second argument and applies the same has-a-directory
	 * test execvp() uses, plus the ".exe" suffix an NT image wants. */
	errno = 0;
	full = __find_program(path, use_path);
	/* __find_program() sets ENOENT when a PATH search came up empty and
	 * leaves errno alone when its malloc() failed, so a cleared errno is
	 * how the second case is told from the first. */
	if (!full) { rc = errno ? errno : ENOMEM; goto out; }

	/* fa->__actions[i] just below is not expressible via nonnull on
	 * spawn_common()'s own fa parameter: fa is already real-NULL-
	 * tolerant here (POSIX allows file_actions to be a null pointer, and
	 * this `if (fa && ...)` is that, not decoration), and the actual
	 * fact making the subscript safe once inside the branch is a field
	 * invariant of posix_spawn_file_actions_t itself, established in
	 * src/process/spawn_file_actions.c's own fa_push(): fa->__cap only
	 * ever grows via a checked realloc that assigns fa->__actions before
	 * fa->__len can exceed the old capacity, so fa->__len > 0 implies
	 * fa->__actions != NULL by construction -- a fact about a struct
	 * FIELD's value, not about fa itself. */
	if (fa && fa->__len) {
		cap = fa->__len;
		sv = malloc((size_t)cap * sizeof *sv);
		if (!sv) { rc = ENOMEM; goto out; }
		for (i = 0; i < fa->__len; i++) {
			rc = do_action(&fa->__actions[i], sv, &nsv, cap);
			if (rc) goto out;
		}
	}

	/* Set immediately before __spawn() and cleared immediately after,
	 * success or failure either way, so neither ever leaks onto a
	 * later, unrelated spawn -- see __spawn_set_pending_sigmask()'s own
	 * comment (libc.h) for why this is the only channel either
	 * attribute has left to ride. */
	if (at && (at->__flags & POSIX_SPAWN_SETSIGMASK) && !sigisemptyset(&at->__sigmask))
		__spawn_set_pending_sigmask(&at->__sigmask);
	if (at && (at->__flags & (POSIX_SPAWN_SETSCHEDPARAM | POSIX_SPAWN_SETSCHEDULER)))
		__spawn_set_pending_priority(nice_from_sched_priority(at->__param.sched_priority));

	child = __spawn(full, argv, envp);
	if (child < 0) rc = errno;

	__spawn_clear_pending_sigmask();
	__spawn_clear_pending_priority();

out:
	restore_slots(sv, nsv);
	free(sv);
	free(full);
	if (!rc && pid) *pid = child;
	errno = saved_errno;
	return rc;
}

int posix_spawn(pid_t *__restrict pid, const char *__restrict path,
                const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *__restrict at,
                char *const *__restrict argv, char *const *__restrict envp)
{
	return spawn_common(pid, path, fa, at, argv, envp, 0);
}

int posix_spawnp(pid_t *__restrict pid, const char *__restrict file,
                 const posix_spawn_file_actions_t *fa,
                 const posix_spawnattr_t *__restrict at,
                 char *const *__restrict argv, char *const *__restrict envp)
{
	return spawn_common(pid, file, fa, at, argv, envp, 1);
}

// NOLINTEND(misc-include-cleaner)
