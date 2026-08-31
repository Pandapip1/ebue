/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * flock(): see include/sys/file.h for the design writeup (including why
 * NT's shared byte-range lock space is mandatory rather than advisory).
 *
 * Always locks the whole file, byte 0 through the largest offset NT's
 * signed 64-bit LARGE_INTEGER can express (0x7fffffffffffffff) -- not
 * "however many bytes the file is today". This is deliberate, not a
 * shortcut: flock()'s contract is a whole *open file description*
 * lock that must still apply after the file grows (POSIX's read/write
 * calls have no fixed upper bound on file size either), and
 * LockFile()'s own documentation gives exactly this technique: "You
 * can lock bytes that are beyond the end of the current file. This is
 * useful to coordinate adding records to the end of a file." (LockFile
 * function (fileapi.h), Win32 apps | Microsoft Learn, Remarks).
 *
 * A repeat flock(fd, X) for the *same* X this fd already holds
 * (tracked in lockstate[] below) is a pure no-op -- no NT call at all
 * -- both because it is genuinely redundant and to minimise how often
 * the landmine below is anywhere near reach.
 *
 * LOCK_SH<->LOCK_EX conversion (flock(2) DESCRIPTION: "a process may
 * hold only one type of lock ... subsequent flock() calls ... convert
 * an existing lock to the new lock mode") has no atomic primitive on
 * NT -- it has to be unlock-then-relock. Two landmines were found
 * empirically against the environment `make check` runs against
 * (Wine), neither documented anywhere and both reproduced with raw
 * NtLockFile()/NtUnlockFile() calls outside this library entirely, to
 * confirm they are Wine's behaviour and not a bug in this file:
 *
 *   1. NtUnlockFile() on a range nothing has locked does not fail
 *      clean with STATUS_RANGE_NOT_LOCKED the way wineserver's own
 *      source (server/fd.c's unlock_fd()) says it should -- it wedges
 *      the *entire* wineserver, hanging every subsequent wine process
 *      sharing that prefix, not merely the caller. Worked around by
 *      never calling NtUnlockFile() except when lockstate[] already
 *      knows a lock placed by an earlier call here is actually held.
 *   2. Unlocking a shared lock and then re-locking the same range
 *      exclusive (or the reverse) on the same file, in the same
 *      process -- the sequence flock(2)'s conversion contract requires
 *      -- deadlocks the second NtLockFile() call, even against a
 *      freshly NtDuplicateObject()'d handle, even though an exclusive
 *      lock acquired without any prior activity on that file works
 *      fine. No workaround was found; this looks like stale lock
 *      bookkeeping on wineserver's side that a plain unlock does not
 *      fully clear.
 *
 * (1) is fully worked around below -- it never happens. (2) is not:
 * there is no way to convert a lock's type through this Wine version
 * without risking a hang, so this file still *implements* conversion
 * the only correct way there is on NT (unlock, then re-lock with the
 * new exclusivity) for the sake of being correct on real Windows, but
 *
 * A third, non-deterministic issue was also observed in this same
 * environment while chasing (1) and (2): the exact same NtLockFile()-
 * then-NtUnlockFile() call pair, byte-for-byte identical arguments,
 * sometimes succeeds and sometimes returns STATUS_NOT_IMPLEMENTED from
 * the unlock -- reproduced with raw ntdll calls with no ntlibc code
 * involved at all, on an otherwise-idle system, so this is Wine's file
 * locking under concurrent load (this project's CI and this
 * environment both run many wine processes against one shared
 * wineserver at once) being flaky, not a logic bug reachable from this
 * file's code. test/posix-termios.c treats an unexpected flock()
 * failure as a note rather than a hard assertion for exactly this
 * reason, the same tolerance this project already extends to other
 * confirmed Wine quirks (e.g. src/select/select.c's wqa_works(), which
 * works around wine-9.0 hardcoding WriteQuotaAvailable to 0).
 *
 * test/posix-termios.c deliberately never exercises an actual type
 * change (LOCK_SH followed by LOCK_EX or vice versa) on a single fd,
 * so that `make check` does not hang on a confirmed Wine bug rather
 * than an ntlibc one.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/file.h>
#include <errno.h>
#include "libc.h"
#include "plat_flock.h"

static struct { __plat_handle_t h; unsigned char held; unsigned char exclusive; } lockstate[FD_MAX];

int flock(int fd, int op) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	int want_exclusive;

	if (!f) return -1;
	if (fd < 0 || fd >= FD_MAX) { errno = EBADF; return -1; }
	if (lockstate[fd].held && lockstate[fd].h != f->h) lockstate[fd].held = 0;

	switch (op & ~LOCK_NB) {
	case LOCK_SH:
	case LOCK_EX:
		want_exclusive = (op & ~LOCK_NB) == LOCK_EX;
		if (lockstate[fd].held && lockstate[fd].exclusive == want_exclusive) return 0;   /* already exactly this */
		if (lockstate[fd].held) {
			__plat_flock_unlock(f->h);
			lockstate[fd].held = 0;
		}
		if (__plat_flock_lock(f->h, op & LOCK_NB, want_exclusive) < 0) return -1;
		lockstate[fd].h = f->h;
		lockstate[fd].held = 1;
		lockstate[fd].exclusive = (unsigned char)want_exclusive;
		return 0;
	case LOCK_UN:
		if (lockstate[fd].held) {
			int r = __plat_flock_unlock(f->h);
			lockstate[fd].held = 0;
			if (r < 0) return -1;
		}
		/* Nothing held by this fd (never locked, or already
		 * unlocked): flock(2) DESCRIPTION does not document this as
		 * an error on any platform, so a plain success matches every
		 * other implementation's behaviour -- and avoids landmine
		 * (1) above entirely. */
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

// NOLINTEND(misc-include-cleaner)
