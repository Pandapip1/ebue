/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface for the rest of src/unistd/ -- everything in
 * that directory's POSIX-facing front doors that is not already covered
 * by src/internal/plat_fd.h (close/read/write/lseek/dup).  See
 * src/unistd/nt/plat_unistd.c for the implementation these declare.
 *
 * Same contract as plat_fd.h/plat_mem.h: every function here takes
 * POSIX-shaped arguments and returns a POSIX-shaped result -- errno
 * already set on failure, never a raw NTSTATUS/HANDLE/PVOID for the
 * front door to interpret.  Every NT-specific interpretation step (which
 * status means what, how a reparse point or a token SID is laid out, how
 * an alertable APC is armed) lives entirely inside the backend function's
 * body.  Front-door bookkeeping that is this library's own POSIX-
 * satisfying strategy -- src/unistd/ids.c's pgid/sid statics,
 * src/unistd/sleep.c's alarm_due/alarm_seq generation counter -- is NOT
 * part of this interface and stays in the front door, unchanged.
 */
#ifndef _NTLIBC_PLAT_UNISTD_H
#define _NTLIBC_PLAT_UNISTD_H

#include <stddef.h>
#include <sys/types.h>
#include "plat_handle.h"

/* ---- src/unistd/sleep.c ------------------------------------------------ */

/* The current time on the same clock alarm() deadlines and
 * __alertable_delay()'s elapsed-time accounting are kept on -- realtime
 * seconds, 100ns ticks since the platform epoch.  Never fails. */
long long __plat_time_now(void);

/* Delivered when the process-wide alarm timer __plat_alarm_arm() armed
 * expires, on the thread that armed it, only while that thread is in an
 * alertable wait -- see sleep.c's banner for exactly what that does and
 * does not reach.  `seq` is whatever __plat_alarm_arm() was called with;
 * the front door uses it to tell a punctual expiry from one already
 * superseded by a later alarm()/alarm(0) (see sleep.c's alarm_apc
 * comment for why that identity check, not the clock, is the only
 * correct test). */
typedef void (*__plat_alarm_fn)(unsigned long seq);

/* Arm the process-wide alarm timer -- created lazily on first use and
 * kept for the life of the process, non-inheritable so fork()'s child
 * can simply forget it (see __plat_alarm_reset_after_fork) -- to call
 * `deliver(seq)` at absolute time `due`.  0 on success, -1 if the timer
 * could not be created or armed (alarm() has nowhere to report that;
 * see its own comment on why the caller treats this the same as "no
 * alarm could be honoured"). */
int __plat_alarm_arm(long long due, unsigned long seq, __plat_alarm_fn deliver);

/* Withdraw a pending request armed by __plat_alarm_arm(), if any.  Never
 * fails: alarm.html gives alarm() no error to report one with, and a
 * timer that was never created has nothing to withdraw. */
void __plat_alarm_cancel(void);

/* fork()'s child side: forget the timer handle without closing it (it
 * was created non-inheritable, so the value was never duplicated into
 * the child and may already name something else there -- see sleep.c's
 * __alarm_reset_after_fork). */
void __plat_alarm_reset_after_fork(void);

/* ---- src/unistd/getpid.c ------------------------------------------------ */

/* getppid(): the process ID this process was created from, or 1 (init's
 * conventional pid) if that cannot be determined -- getppid.html reserves
 * no error return, so a query that fails still has to answer with some
 * pid. */
pid_t __plat_getppid(void);

/* ---- src/unistd/ftruncate.c --------------------------------------------- */

/* ftruncate(): set the object `h` refers to to exactly `len` bytes,
 * already validated (len >= 0, fildes open for writing, within
 * RLIMIT_FSIZE) by the front door. */
int __plat_ftruncate(__plat_handle_t h, off_t len);

/* ---- src/unistd/fsync.c -------------------------------------------------- */

/* fsync()/fdatasync(): force `h`'s buffered writes out. */
int __plat_fsync(__plat_handle_t h);

/* ---- src/unistd/pipe.c --------------------------------------------------- */

/* pipe()/pipe2(): create the two ends of a fresh anonymous pipe.
 * `inheritable` requests that both handles be inheritable by a child
 * process (i.e. O_CLOEXEC was not requested). */
int __plat_pipe(__plat_handle_t *rp, __plat_handle_t *wp, int inheritable);

/* ---- src/unistd/sysconf.c ------------------------------------------------ */

/* _SC_NPROCESSORS_CONF/_SC_NPROCESSORS_ONLN: 1 if the real count cannot
 * be determined -- sysconf() has no error return for a name it does
 * support, so a query that fails still has to answer with a plausible
 * value. */
long __plat_nprocessors(void);

/* _SC_PHYS_PAGES, already expressed in 4096-byte pages regardless of the
 * platform's native page size: -1 if the real count cannot be
 * determined, sysconf()'s "no limit" answer. */
long __plat_phys_pages(void);

/* ---- src/unistd/unlink.c -------------------------------------------------- */

/* unlink()/rmdir()/unlinkat(): remove the directory entry `path` (already
 * validated not to be "."/".." when `isdir`) names, relative to `dirfd`
 * (AT_FDCWD for the current directory). */
int __plat_unlink(int dirfd, const char *path, int isdir);

/* ---- src/unistd/chdir.c ---------------------------------------------------- */

/* chdir(): set the process's current directory to `path` (UTF-8, POSIX
 * form -- already resolved through the vfs and length-checked by the
 * front door). */
int __plat_chdir(const char *path);

/* ---- src/unistd/link.c ------------------------------------------------------ */

/* linkat(): create a new directory entry `newpath` (relative to
 * `newdirfd`) for the file `oldpath` (relative to `olddirfd`) already
 * names.  `followsym` is AT_SYMLINK_FOLLOW: link the symlink's target
 * rather than the symlink itself. */
int __plat_link(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int followsym);

/* readlinkat(): read the target of the symbolic link `path` (relative to
 * `dirfd`) into `buf`/`bufsz`, POSIX truncate-silently semantics.
 * Returns the number of bytes placed, or -1(errno).  The front door has
 * already ruled out a virtual-fs path; every reparse-point interpretation
 * step (NTFS symlink, junction, WSL symlink) lives here. */
ssize_t __plat_readlink(int dirfd, const char *path, char *buf, size_t bufsz);

/* symlinkat(): create a new symbolic link `linkpath` (relative to
 * `newdirfd`) whose contents are `target`, verbatim (not resolved). */
int __plat_symlink(const char *target, int newdirfd, const char *linkpath);

/* ---- src/unistd/ids.c ------------------------------------------------------- */

/* getuid(): this process's uid, derived from its primary token's user
 * SID (see ids.c's banner for the whole Cygwin-compatible mapping this
 * implements).  Never fails -- a token that cannot be opened or queried
 * still has to produce some uid, and getuid.html reserves no error
 * return. */
uid_t __plat_detect_uid(void);

/* Publish `self` (this process's own, already-known pid) as its own
 * process-group leader: a named, idempotent, cross-process boolean keyed
 * by pid, since NT has no real process-group object to set one on (see
 * ids.c's banner).  Safe to call repeatedly for the same pid; best-effort
 * and silent on failure the same way setpgrp.html's "no errors are
 * defined" already requires of its caller. */
void __plat_pgrp_publish_self(pid_t self);

/* Has `pid` published itself as a process-group leader via
 * __plat_pgrp_publish_self()? */
int __plat_pgrp_is_leader(pid_t pid);

/* Does a process with this pid exist, as far as this process can tell
 * (STATUS_ACCESS_DENIED counts as existing -- the process is there and
 * merely not ours to open)?  Callers handle pid 0/self/known-child
 * themselves first; this is only the "ask the object manager" fallback
 * for everything else. */
int __plat_process_exists(pid_t pid);

/* chown()/lchown()/fchownat(): there is no ownership to change, but
 * "there is no ownership to change" is not "there is no path to
 * resolve" -- this resolves `path` (relative to `dirfd`, honoring
 * AT_SYMLINK_NOFOLLOW in `flags`) and reports whether it exists, which
 * is all chown.html's shall-fail clauses ask for. */
int __plat_chown_probe(int dirfd, const char *path, int flags);

#endif
