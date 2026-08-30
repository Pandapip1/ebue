/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_dirent.h -- see that
 * header, and src/mman/linux/plat_mem.c's/src/unistd/linux/plat_fd.c's
 * own banners for the general discipline this file would otherwise
 * follow (raw syscall(2), no host libc, -nostdinc against ntlibc's own
 * headers, aarch64 syscall numbers confirmed against this host).
 *
 * __plat_dir_read() is NOT implemented here, and this is a different,
 * more subtle gap than __plat_create_file()'s (src/fcntl/linux/
 * plat_fcntl.c) -- worth spelling out precisely, because the header's
 * signature alone (an already-open __plat_handle_t, no struct __ntpath
 * in sight) looks portable at a glance.
 *
 * plat_dirent.h's own banner says the raw record format `buf` is filled
 * with "is NOT part of this interface -- it stays whatever the
 * backend's native directory-enumeration call produces ... and every
 * caller of this function already parses that format directly."  That
 * is not a hypothetical: src/dirent/dirent_internal.h, src/dirent/
 * readdir.c's __dirstream_next(), and src/dirent/getdents.c's
 * getdents() all hardcode FILE_ID_BOTH_DIR_INFORMATION -- NT's own
 * directory-enumeration record shape (FileId, FileAttributes,
 * FileNameLength, FileName, chained by NextEntryOffset) -- directly in
 * the front door.  getdents.c literally does
 * `fi = (FILE_ID_BOTH_DIR_INFORMATION *)buf;` on the exact buffer
 * __plat_dir_read() fills, then walks it by NextEntryOffset.
 *
 * A native Linux directory read is getdents64(2), whose record shape
 * (linux_dirent64: d_ino, d_off, d_reclen, d_type, d_name[], chained by
 * d_reclen rather than a separate NextEntryOffset field, and no
 * FILE_ID_BOTH_DIR_INFORMATION-shaped attribute/name-length pair at
 * all) is a completely different layout. Handing getdents64's raw
 * output to readdir.c's/getdents.c's existing NT-shaped parsing would
 * not fail to compile or fail cleanly at runtime -- it would silently
 * misinterpret the first eight bytes of a real d_ino as an NT FileId
 * field, walk off into whatever garbage the byte offsets happen to
 * land on, and read or crash on nonsense. That is a worse outcome than
 * a link error, so nothing here is implemented well enough to invite
 * linking it against the real front door.
 *
 * Fixing this for real needs the same shape of larger effort
 * __plat_create_file()'s gap does: either a backend-neutral record
 * format this interface hands back (a bigger redesign than this
 * migration pass is scoped for -- plat_dirent.h's own banner says so
 * explicitly) or an entirely separate Linux-native front door for
 * opendir()/readdir()/getdents() that parses linux_dirent64 itself,
 * the same way a Linux open() front door would need to stop calling
 * __ntpath_at(). Neither is attempted here. This stub exists so the
 * interface has a body to point at and a comment explaining why it
 * does nothing real, exactly the pattern __plat_create_file() uses.
 */
#include <errno.h>
#include "plat_dirent.h"

ssize_t __plat_dir_read(__plat_handle_t h, void *buf, size_t bufsize, int restart)
{
	(void)h; (void)buf; (void)bufsize; (void)restart;
	errno = ENOSYS;
	return -1;
}
