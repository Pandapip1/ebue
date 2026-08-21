/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getdents: the GNU raw-directory-read call, taken directly off a fd
 * rather than a DIR.  Continuation across calls comes for free from
 * NtQueryDirectoryFile itself -- RestartScan = FALSE (the default state
 * of a freshly opened handle) picks up from wherever the kernel's own
 * cursor on that handle left off, so nothing needs to be remembered here
 * between calls.
 *
 * Like readdir(), "." and ".." come through as ordinary records straight
 * from NT (see dirent_internal.h) and need no special handling here.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "dirent_internal.h"

int getdents(int fd, struct dirent *out, size_t size)
{
	struct __fd *f = __fd_get(fd);
	unsigned char buf[8192];
	IO_STATUS_BLOCK io;
	NTSTATUS st;
	FILE_ID_BOTH_DIR_INFORMATION *fi;
	size_t used = 0;

	if (!f) return -1;
	if (f->type != __FD_DIR) { errno = ENOTDIR; return -1; }
	if (size < sizeof(struct dirent)) { errno = EINVAL; return -1; }

	st = NtQueryDirectoryFile(f->h, 0, 0, 0, &io, buf, sizeof buf,
	                          FileIdBothDirectoryInformation, FALSE, 0, FALSE);
	if (st == STATUS_NO_MORE_FILES) return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	fi = (FILE_ID_BOTH_DIR_INFORMATION *)buf;
	for (;;) {
		struct dirent *d;
		if (used + sizeof(struct dirent) > size) break;

		d = (struct dirent *)((char *)out + used);
		memset(d, 0, sizeof *d);
		d->d_ino = (ino_t)fi->FileId;
		d->d_type = __dirent_dtype(fi->FileAttributes);
		d->d_reclen = sizeof *d;
		__utf16_to_utf8_buf(fi->FileName, fi->FileNameLength / sizeof(WCHAR), d->d_name, sizeof d->d_name);
		used += sizeof *d;
		d->d_off = (off_t)used;

		if (!fi->NextEntryOffset) break;
		fi = (FILE_ID_BOTH_DIR_INFORMATION *)((char *)fi + fi->NextEntryOffset);
	}
	return (int)used;
}
