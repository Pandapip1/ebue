/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * readdir/readdir_r, and __dirstream_next which every other file in this
 * directory that walks a DIR's entries (scandir, seekdir) goes through.
 *
 * d_reclen is always sizeof(struct dirent): unlike Linux's real getdents,
 * nothing here ever packs entries tighter than that, so there is no
 * shorter length to report.  d_off is dp->tell after the entry is
 * counted -- see dirent_internal.h for why that, and not a kernel byte
 * offset, is what telldir()/seekdir() work with.  "." and ".." arrive as
 * ordinary records straight from NT (see dirent_internal.h); nothing
 * here treats them specially.
 */
#include <string.h>
#include <errno.h>
#include "dirent_internal.h"

FILE_ID_BOTH_DIR_INFORMATION *__dirstream_next(DIR *dp)
{
	FILE_ID_BOTH_DIR_INFORMATION *fi;
	IO_STATUS_BLOCK io;
	NTSTATUS st;
	HANDLE h;

	if (dp->bufpos < dp->buflen) {
		fi = (FILE_ID_BOTH_DIR_INFORMATION *)(dp->buf + dp->bufpos);
		dp->bufpos += fi->NextEntryOffset ? fi->NextEntryOffset : dp->buflen - dp->bufpos;
		return fi;
	}
	if (dp->done) return 0;

	h = __fd_handle(dp->fd);
	if (!h) return 0;      /* __fd_handle already set errno = EBADF */

	st = NtQueryDirectoryFile(h, 0, 0, 0, &io, dp->buf, __DIRBUF_SIZE,
	                          FileIdBothDirectoryInformation, FALSE, 0, dp->restart);
	dp->restart = 0;
	if (st == STATUS_NO_MORE_FILES) { dp->done = 1; return 0; }
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return 0; }

	dp->buflen = (size_t)io.Information;
	dp->bufpos = 0;
	fi = (FILE_ID_BOTH_DIR_INFORMATION *)dp->buf;
	dp->bufpos += fi->NextEntryOffset ? fi->NextEntryOffset : dp->buflen;
	return fi;
}

static void make_real(DIR *dp, FILE_ID_BOTH_DIR_INFORMATION *fi, struct dirent *out)
{
	memset(out, 0, sizeof *out);
	out->d_ino = (ino_t)fi->FileId;
	out->d_type = __dirent_dtype(fi->FileAttributes);
	out->d_reclen = sizeof *out;
	__utf16_to_utf8_buf(fi->FileName, fi->FileNameLength / sizeof(WCHAR), out->d_name, sizeof out->d_name);
	dp->tell++;
	out->d_off = dp->tell;
}

/* 0 = filled *out; 1 = end of directory; -1 = error, errno set. */
static int fill(DIR *dp, struct dirent *out)
{
	struct __fd *f = __fd_get(dp->fd);
	if (!f) return -1;
	if (!f->vfs_native && (f->vfs == __VFS_ROOT || f->vfs == __VFS_DEV)) {
		static const char *const root_names[] = { ".", "..", "dev" };
		static const unsigned char root_types[] = { __DT_DIR, __DT_DIR, __DT_DIR };
		static const char *const dev_names[] = { ".", "..", "console", "null", "tty" };
		static const unsigned char dev_types[] = { __DT_DIR, __DT_DIR, __DT_CHR, __DT_CHR, __DT_CHR };
		const char *const *names = f->vfs == __VFS_ROOT ? root_names : dev_names;
		const unsigned char *types = f->vfs == __VFS_ROOT ? root_types : dev_types;
		size_t count = f->vfs == __VFS_ROOT ? 3 : 5;
		if ((size_t)dp->tell >= count) { dp->done = 1; return 1; }
		memset(out, 0, sizeof *out);
		out->d_ino = (ino_t)(dp->tell < 2 ? (f->vfs == __VFS_ROOT ? __VFS_ROOT :
		                    (dp->tell == 0 ? __VFS_DEV : __VFS_ROOT)) :
		                    (f->vfs == __VFS_ROOT ? __VFS_DEV : __VFS_CONSOLE + dp->tell - 2));
		out->d_type = types[dp->tell];
		out->d_reclen = sizeof *out;
		strcpy(out->d_name, names[dp->tell]);
		dp->tell++;
		out->d_off = dp->tell;
		return 0;
	}
	{
		FILE_ID_BOTH_DIR_INFORMATION *fi = __dirstream_next(dp);
		if (fi) {
			make_real(dp, fi, out);
			if (f->vfs_native) dp->vseen |= __vfs_mandatory_seen(f->vfs, out->d_name);
			return 0;
		}
		if (!dp->done) return -1;
	}
	if (f->vfs_native) {
		int total = __vfs_mandatory_count(f->vfs);
		while (dp->vnext < total) {
			int i = dp->vnext++;
			if (dp->vseen & (1u << i)) continue;
			memset(out, 0, sizeof *out);
			out->d_ino = (ino_t)__vfs_mandatory_kind(f->vfs, i);
			out->d_type = f->vfs == __VFS_ROOT ? __DT_DIR : __DT_CHR;
			out->d_reclen = sizeof *out;
			strcpy(out->d_name, __vfs_mandatory_name(f->vfs, i));
			dp->tell++;
			out->d_off = dp->tell;
			return 0;
		}
	}
	return 1;
}

int readdir_r(DIR *dp, struct dirent *entry, struct dirent **result)
{
	int r = fill(dp, entry);
	if (r < 0) { *result = 0; return errno; }
	*result = r ? 0 : entry;
	return 0;
}

struct dirent *readdir(DIR *dp)
{
	int r = fill(dp, &dp->ent);
	return r ? 0 : &dp->ent;
}
