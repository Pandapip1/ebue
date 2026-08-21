/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * opendir/fdopendir: get a directory HANDLE (through the same __ntpath ->
 * NtCreateFile path open() uses, with FILE_DIRECTORY_FILE so a plain file
 * given by mistake fails with ENOTDIR rather than being read as one) and
 * wrap it in a DIR.
 *
 * The handle goes through the fd table like any other, which is what
 * makes dirfd() trivial and fdopendir() nearly free: fdopendir() does not
 * duplicate its argument, it just starts using the fd that is already
 * there, exactly as glibc's does, so the caller must not touch that fd
 * itself afterward and closedir() closes it.
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "dirent_internal.h"

static DIR *alloc_dir(int fd)
{
	DIR *dp = __malloc(sizeof *dp);
	if (!dp) { errno = ENOMEM; return 0; }
	memset(dp, 0, sizeof *dp);
	dp->buf = __malloc(__DIRBUF_SIZE);
	if (!dp->buf) { __free(dp); errno = ENOMEM; return 0; }
	dp->fd = fd;
	dp->restart = 1;
	return dp;
}

DIR *fdopendir(int fd)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return 0;
	if (f->type != __FD_DIR) { errno = ENOTDIR; return 0; }
	return alloc_dir(fd);
}

DIR *opendir(const char *path)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	int fd;
	DIR *dp;

	if (__ntpath(path, &np, OBJ_CASE_INSENSITIVE) < 0) return 0;
	st = NtCreateFile(&h, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, 0,
	                  FILE_ATTRIBUTE_NORMAL, FILE_SHARE_VALID_FLAGS, FILE_OPEN,
	                  FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT, 0, 0);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return 0; }

	fd = __fd_install(h, O_CLOEXEC, __FD_DIR);
	if (fd < 0) { NtClose(h); return 0; }

	dp = alloc_dir(fd);
	if (!dp) { close(fd); return 0; }
	return dp;
}
