/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * lockf(): a thin wrapper over fcntl(F_SETLK/F_SETLKW/F_GETLK)
 * (src/fcntl/fcntl.c), itself backed by NT byte-range locks
 * (src/fcntl/nt/plat_fcntl.c's __plat_lock_set()/__plat_lock_probe()/
 * __plat_lock_clear()) -- see include/sys/file.h's banner for how that
 * mapping behaves (mandatory, not advisory, unlike POSIX's own flock()
 * contract). Previously left undefined-ok on the theory that "the
 * separate lockf() wrapper remains outside the implemented API", even
 * though the fcntl() machinery it wraps already existed; there was
 * nothing left to design, only to write.
 *
 * lockf.html DESCRIPTION: the section acted on "starts at the current
 * offset in the file and extends forward for a positive len, or
 * backward for a negative len (until the negative len bytes before the
 * current offset)... if len is 0, the section from the current offset
 * through the largest possible offset shall be locked". That is
 * fcntl()'s own l_whence == SEEK_CUR, l_start == 0, l_len == len
 * semantics exactly -- src/fcntl/fcntl.c's record_lock_range() already
 * implements all three of positive/negative/zero len that way for
 * F_SETLK et al, so lockf() only has to build the struct flock and pick
 * which fcntl() command each of its four operations maps to. */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "libc.h"

int lockf(int fd, int cmd, off_t len)
{
	struct flock fl;

	memset(&fl, 0, sizeof fl);
	fl.l_whence = SEEK_CUR;
	fl.l_start = 0;
	fl.l_len = len;

	switch (cmd) {
	case F_ULOCK:
		fl.l_type = F_UNLCK;
		return fcntl(fd, F_SETLK, &fl);
	case F_LOCK:
		fl.l_type = F_WRLCK;
		return fcntl(fd, F_SETLKW, &fl);
	case F_TLOCK:
		/* lockf(3) has one kind of lock, not fcntl()'s
		 * shared/exclusive distinction -- F_WRLCK is the right
		 * request for both F_TLOCK and F_LOCK above. On conflict,
		 * fcntl(F_SETLK) already fails with EAGAIN/EWOULDBLOCK
		 * (src/internal/nt/errno_nt.c's STATUS_LOCK_NOT_GRANTED/
		 * STATUS_FILE_LOCK_CONFLICT mapping), one of the two errno
		 * values lockf.html's ERRORS documents for F_TLOCK. */
		fl.l_type = F_WRLCK;
		return fcntl(fd, F_SETLK, &fl);
	case F_TEST:
		/* "Test the section for locks held by other processes."
		 * fcntl(F_GETLK) already ignores this process's own locks
		 * and reports whether a conflicting lock (by another
		 * process) exists via l_type; a conflict is reported back
		 * as lockf()'s own failure, not F_GETLK's success. */
		fl.l_type = F_WRLCK;
		if (fcntl(fd, F_GETLK, &fl) < 0) return -1;
		if (fl.l_type == F_UNLCK) return 0;
		errno = EAGAIN;
		return -1;
	default:
		errno = EINVAL;
		return -1;
	}
}

// NOLINTEND(misc-include-cleaner)
