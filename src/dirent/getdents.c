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

static int append_missing(struct __fd *f, struct dirent *out, size_t size)
{
	size_t used = 0;
	int total = __vfs_mandatory_count(f->vfs);
	while (f->vnext < total && used + sizeof(struct dirent) <= size) {
		int i = f->vnext++;
		struct dirent *d;
		if (f->vseen & (1u << i)) continue;
		d = (struct dirent *)((char *)out + used);
		memset(d, 0, sizeof *d);
		d->d_ino = (ino_t)__vfs_mandatory_kind(f->vfs, i);
		d->d_type = f->vfs == __VFS_ROOT ? __DT_DIR : __DT_CHR;
		d->d_reclen = sizeof *d;
		strcpy(d->d_name, __vfs_mandatory_name(f->vfs, i));
		used += sizeof *d;
		d->d_off = (off_t)f->vnext;
	}
	return (int)used;
}

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
	if (!f->vfs_native && (f->vfs == __VFS_ROOT || f->vfs == __VFS_DEV)) {
		static const char *const root_names[] = { ".", "..", "dev" };
		static const unsigned char root_types[] = { __DT_DIR, __DT_DIR, __DT_DIR };
		static const char *const dev_names[] = { ".", "..", "console", "null", "tty" };
		static const unsigned char dev_types[] = { __DT_DIR, __DT_DIR, __DT_CHR, __DT_CHR, __DT_CHR };
		const char *const *names = f->vfs == __VFS_ROOT ? root_names : dev_names;
		const unsigned char *types = f->vfs == __VFS_ROOT ? root_types : dev_types;
		size_t total = f->vfs == __VFS_ROOT ? 3 : 5;
		size_t index = f->pos < 0 ? 0 : (size_t)f->pos;
		while (index < total && used + sizeof(struct dirent) <= size) {
			struct dirent *d = (struct dirent *)((char *)out + used);
			memset(d, 0, sizeof *d);
			d->d_ino = (ino_t)(index < 2 ? (f->vfs == __VFS_ROOT ? __VFS_ROOT :
			             (index == 0 ? __VFS_DEV : __VFS_ROOT)) :
			             (f->vfs == __VFS_ROOT ? __VFS_DEV : __VFS_CONSOLE + index - 2));
			d->d_type = types[index];
			d->d_reclen = sizeof *d;
			strcpy(d->d_name, names[index]);
			index++;
			used += sizeof *d;
			d->d_off = (off_t)index;
		}
		f->pos = (long long)index;
		return (int)used;
	}

	st = NtQueryDirectoryFile(f->h, 0, 0, 0, &io, buf, sizeof buf,
	                          FileIdBothDirectoryInformation, FALSE, 0, FALSE);
	if (st == STATUS_NO_MORE_FILES)
		return f->vfs_native ? append_missing(f, out, size) : 0;
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
		if (f->vfs_native) f->vseen |= __vfs_mandatory_seen(f->vfs, d->d_name);
		used += sizeof *d;
		d->d_off = (off_t)used;

		if (!fi->NextEntryOffset) break;
		fi = (FILE_ID_BOTH_DIR_INFORMATION *)((char *)fi + fi->NextEntryOffset);
	}
	return (int)used;
}
