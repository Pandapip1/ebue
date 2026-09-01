/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_FCNTL_H
#define	_FCNTL_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_off_t
#define __NEED_pid_t
#define __NEED_mode_t

#ifdef _GNU_SOURCE
#define __NEED_size_t
#define __NEED_ssize_t
#endif

#include <bits/alltypes.h>

/* fcntl.h.html DESCRIPTION: "The <fcntl.h> header shall define the
 * values used for l_whence, SEEK_SET, SEEK_CUR, and SEEK_END as
 * described in <stdio.h>."
 *
 * Unconditional -- the sentence carries no option-group margin marker.
 * It exists so a translation unit doing record locking, which needs
 * this header for struct flock and F_SETLK, can fill in l_whence
 * without also including <stdio.h>; on glibc and musl such a unit
 * compiles, and here it did not.
 *
 * Same values as <stdio.h> and <unistd.h>, and deliberately spelled the
 * same way rather than guarded: C99 6.10.3p2 makes an identical
 * redefinition benign, which is what lets all three headers be included
 * together, and is how <unistd.h> and <stdio.h> already coexist. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_RDONLY  00
#define O_WRONLY  01
#define O_RDWR    02
#define O_ACCMODE 03

/* fcntl.h.html DESCRIPTION: "The <fcntl.h> header shall define the
 * following symbolic constants for use as the file access modes for
 * open(), openat(), and fcntl(). The values shall be unique, except
 * that O_EXEC and O_SEARCH may have equal values."
 *
 * 03 is the only bit pattern O_ACCMODE can still hold that is not
 * already an access mode, and spending it on both -- which is the whole
 * point of the standard's exception -- is what keeps O_ACCMODE itself
 * at 03.  The alternative is musl's: spell both as O_PATH and widen the
 * mask to (03|O_PATH).  Rejected here because it is not additive.  It
 * would reclassify every existing open(..., O_PATH) in a program as an
 * execute-only open, and change the value fcntl(F_GETFL) reports for
 * the descriptors those calls made, purely as a side effect of two new
 * names appearing.  03, by contrast, was already the unreachable arm of
 * the access-mode switch in src/fcntl/open.c, so nothing that compiles
 * today means anything different tomorrow.
 *
 * open() refuses both, with [EINVAL] -- the same answer 03 got before
 * it had names, and read the comment on that switch for why refusing is
 * the honest answer rather than a stub.  These are the header constants
 * only; giving O_SEARCH a traverse-only directory handle and O_EXEC an
 * execute-only one is separate work this does not claim. */
#define O_EXEC   03
#define O_SEARCH 03

/* "O_TTY_INIT Set the termios structure terminal parameters to a state
 * that provides conforming behavior... The O_TTY_INIT flag can have the
 * value zero and in this case it need not be bitwise-distinct from the
 * other flags."
 *
 * Zero, and by the clause's own escape hatch rather than as a stub.
 * The only terminal on this platform is the NT console, and the state
 * that gives conforming behavior there is the mode a console comes up
 * in -- processed input, line input, echo, which src/termios/termios.c
 * maps to ISIG|ICANON|ECHO -- so a freshly opened console already
 * satisfies the flag and there is nothing for open() to set.  What is
 * NOT claimed: a console whose mode an earlier process altered is not
 * put back, because nothing in NT distinguishes "as it came up" from
 * "as someone left it". */
#define O_TTY_INIT 0

#define O_CREAT        0100
#define O_EXCL         0200
#define O_NOCTTY       0400
#define O_TRUNC       01000
#define O_APPEND      02000
#define O_NONBLOCK    04000
#define O_DSYNC      010000
#define O_SYNC     04010000
#define O_RSYNC    04010000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW  0400000
#define O_CLOEXEC  02000000

#define O_ASYNC      020000
#define O_DIRECT     040000
#define O_LARGEFILE 0100000
#define O_NOATIME  01000000
#define O_PATH    010000000
#define O_TMPFILE 020200000
#define O_NDELAY O_NONBLOCK

/* Windows-specific: open in text mode (CRLF translation). Ignored: all
 * files are binary here, the way they are everywhere that is not DOS. */
#define O_BINARY 0
#define O_TEXT 0

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4

#define F_SETOWN 8
#define F_GETOWN 9
#define F_SETSIG 10
#define F_GETSIG 11

#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7

#define F_DUPFD_CLOEXEC 1030

#define FD_CLOEXEC 1

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_EACCESS 0x200

struct flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
};

int creat(const char *, mode_t);
int fcntl(int, int, ...);
int open(const char *, int, ...);
int openat(int, const char *, int, ...);

#define POSIX_FADV_NORMAL     0
#define POSIX_FADV_RANDOM     1
#define POSIX_FADV_SEQUENTIAL 2
#define POSIX_FADV_WILLNEED   3
#define POSIX_FADV_DONTNEED   4
#define POSIX_FADV_NOREUSE    5
int posix_fadvise(int, off_t, off_t, int);
int posix_fallocate(int, off_t, off_t);

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXG 0070
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXO 0007
#endif

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
