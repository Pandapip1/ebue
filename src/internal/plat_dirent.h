/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform-directory interface src/dirent/{readdir,getdents}.c's
 * POSIX-facing front doors call into instead of a raw
 * NtQueryDirectoryFile call.  See src/dirent/nt/plat_dirent.c for the
 * implementation this declares.
 *
 * The raw record format a successful read fills `buf` with is NOT part
 * of this interface -- it stays whatever the backend's native directory-
 * enumeration call produces (NT: FILE_ID_BOTH_DIR_INFORMATION records
 * chained by NextEntryOffset), and every caller of this function already
 * parses that format directly (see src/dirent/dirent_internal.h's own
 * banner for why: it is this library's shared, NT-specific record shape,
 * used identically by readdir()'s buffering and getdents()'s raw
 * pass-through, and re-deriving a backend-neutral record shape here
 * would be a bigger redesign than this seam is scoped to make).  What
 * this interface DOES hide is the syscall itself and its status
 * handling -- STATUS_NO_MORE_FILES becomes a plain empty read, exactly
 * like __plat_read()'s end-of-file convention. */
#ifndef _NTLIBC_PLAT_DIRENT_H
#define _NTLIBC_PLAT_DIRENT_H

#include <stddef.h>
#include <sys/types.h>
#include "plat_handle.h"

/* Fill `buf` (`bufsize` bytes) with the next batch of raw directory
 * records from `h`'s own enumeration cursor.  `restart` asks the
 * backend to rewind to the first entry (rewinddir()'s effect) instead
 * of continuing from wherever the last call left off.  Returns the
 * number of bytes filled (> 0), 0 at end-of-directory, or -1 with
 * errno set. */
ssize_t __plat_dir_read(__plat_handle_t h, void *buf, size_t bufsize, int restart);

#endif
