/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real shape of DIR, private to this directory.  dirent.h only
 * forward-declares "struct __dirstream" so that programs carry it around
 * as an opaque pointer; here it is given a body.
 *
 * NT has no getdents()-shaped syscall: NtQueryDirectoryFile fills a
 * caller buffer with one FILE_ID_BOTH_DIR_INFORMATION record after
 * another (chained by NextEntryOffset) each time it is called, and keeps
 * its own notion of "where it got to" on the file object -- calling it
 * again with RestartScan = FALSE continues; RestartScan = TRUE starts
 * over.  So a DIR is: the directory HANDLE (via the fd table, which is
 * what makes dirfd() and fdopendir() free), a bufferful of the kernel's
 * most recent answer, and a cursor into it.
 *
 * Unlike a raw Linux getdents(), NTFS *does* hand back "." and ".." as
 * the first two records of a regular directory's listing (confirmed
 * against Wine/NT rather than assumed -- an earlier version of this file
 * synthesized them itself and ended up with each doubled).  So they just
 * flow through like any other record; nothing here treats them
 * specially except that a fresh handle naturally starts there.
 *
 * telldir()/seekdir() have no kernel-backed byte offset to report; NT's
 * cursor is opaque and internal to the handle.  What is used instead is
 * simply a count of entries returned so far ("tell" below, and each
 * dirent's d_off once it has been returned).  seekdir() to a location
 * behind where the stream currently is rewinds and replays; a location
 * ahead is reached by discarding entries.  This is observably correct
 * (seekdir(dp, telldir(dp)) is a no-op, and seeking to a value obtained
 * from an earlier telldir() on the same DIR reproduces that position)
 * but is O(n) rather than O(1), and is not meaningful across different
 * DIR streams over the same directory the way a Linux fd offset can be.
 */
#ifndef _NTLIBC_DIRENT_INTERNAL_H
#define _NTLIBC_DIRENT_INTERNAL_H

#include <dirent.h>
#include "libc.h"

/* One NtQueryDirectoryFile call's worth of FILE_ID_BOTH_DIR_INFORMATION
 * records.  Large enough that most directories are read in one call. */
#define __DIRBUF_SIZE 32768

struct __dirstream {
	int fd;                 /* the __FD_DIR slot backing this stream */
	unsigned char *buf;     /* __DIRBUF_SIZE bytes of raw NT records */
	size_t bufpos;          /* byte offset in buf of the next unread record */
	size_t buflen;          /* bytes of buf holding real records; 0 = empty */
	int restart;            /* pass RestartScan = TRUE on the next fill */
	int done;                /* the kernel has said STATUS_NO_MORE_FILES */
	long tell;                 /* entries returned so far; telldir()'s value */
	struct dirent ent;          /* storage readdir() returns a pointer into */
};

/* dirent.h's DT_* are only visible under _GNU_SOURCE/_BSD_SOURCE, but the
 * type has to be computed unconditionally inside the library; these are
 * the same numeric values, duplicated so no feature-test macro is needed
 * just to implement readdir() itself. */
#define __DT_DIR 4
#define __DT_LNK 10
#define __DT_REG 8

/* The next raw record from dp's NT buffer, refilling from the kernel as
 * needed.  Returns NULL at end-of-directory (dp->done is then true) or
 * NULL with errno set on error. */
FILE_ID_BOTH_DIR_INFORMATION *__dirstream_next(DIR *dp);

/* mode_from_attrs-style: a d_type value from NT's FileAttributes.  A
 * reparse point is reported as a symlink regardless of its actual reparse
 * tag (mount points included) since a directory listing has no cheap way
 * to tell them apart without opening each one, and DT_LNK is what every
 * other minimal implementation of this reports for the case anyway. */
static inline unsigned char __dirent_dtype(ULONG attrs)
{
	if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return __DT_LNK;
	if (attrs & FILE_ATTRIBUTE_DIRECTORY) return __DT_DIR;
	return __DT_REG;
}

#endif
