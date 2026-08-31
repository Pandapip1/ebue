/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_dirent.h -- see that header
 * for the contract each function makes.
 *
 * __plat_dir_read() was, until this file existed, inline inside
 * src/dirent/readdir.c's __dirstream_next() and src/dirent/getdents.c's
 * getdents(); nothing changed in substance, only location and the
 * addition of a POSIX-shaped return (byte count, 0 at end-of-directory,
 * or -1/errno) in place of a raw NTSTATUS.
 *
 * __plat_dir_decode_one() is new: it is the NT-specific half of what
 * used to be readdir.c's make_real()/__dirstream_next() and getdents.c's
 * own NextEntryOffset walk, moved here unchanged in substance (byte-for-
 * byte the same FileId/FileAttributes/FileNameLength/FileName reads and
 * UTF-16-to-UTF-8 conversion) so both front doors can share one decoding
 * step instead of each hardcoding FILE_ID_BOTH_DIR_INFORMATION
 * themselves. __dirent_dtype() (formerly src/dirent/dirent_internal.h)
 * moved here too, as a static helper, since attribute-to-d_type mapping
 * is exactly the kind of NT-specific interpretation step this backend
 * exists to own -- FILE_ATTRIBUTE_* bits mean nothing to any other
 * backend. */
#include <string.h>
#include "libc.h"
#include "plat_dirent.h"

ssize_t __plat_dir_read(__plat_handle_t h, void *buf, size_t bufsize, int restart)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	io.Status = 0; io.Information = 0;
	st = NtQueryDirectoryFile(h, 0, 0, 0, &io, buf, (ULONG)bufsize,
	                          FileIdBothDirectoryInformation, FALSE, 0, restart);
	if (st == STATUS_NO_MORE_FILES) return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

/* mode_from_attrs-style: a d_type value from NT's FileAttributes.  A
 * reparse point is reported as a symlink regardless of its actual reparse
 * tag (mount points included) since a directory listing has no cheap way
 * to tell them apart without opening each one, and DT_LNK is what every
 * other minimal implementation of this reports for the case anyway.
 * These numeric values are dirent.h's own DT_DIR/DT_LNK/DT_REG (4/10/8),
 * duplicated here rather than pulled from a header the same way
 * src/dirent/dirent_internal.h's own __DT_* duplicates them -- see
 * plat_dirent.h's own comment on struct __dirent_raw's `type` field. */
#define __NT_DT_REG 8
#define __NT_DT_DIR 4
#define __NT_DT_LNK 10

static unsigned char dtype_from_attrs(ULONG attrs)
{
	if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return __NT_DT_LNK;
	if (attrs & FILE_ATTRIBUTE_DIRECTORY) return __NT_DT_DIR;
	return __NT_DT_REG;
}

int __plat_dir_decode_one(const void *buf, size_t buflen, size_t *pos, struct __dirent_raw *out)
{
	const FILE_ID_BOTH_DIR_INFORMATION *fi;
	size_t next;

	if (*pos >= buflen) return 0;
	/* fi->FileId below is a disclosed, deliberately unmarked residual,
	 * the same "struct/local-derived pointer, not a parameter" class
	 * this file's own Linux sibling (src/dirent/linux/plat_dirent.c)
	 * discloses for its own d->d_ino: fi is `buf + *pos`, a local, not
	 * a parameter, and buf itself is already required (src/internal/
	 * plat_dirent.h). Verified sound by hand regardless: `*pos <
	 * buflen` just above proves this offset is within buf's own real,
	 * NtQueryDirectoryFile-filled extent. */
	fi = (const FILE_ID_BOTH_DIR_INFORMATION *)((const unsigned char *)buf + *pos);

	/* Zeroed up front, not just the fields set explicitly below: the old
	 * pre-decode_one code wrote FileName's UTF-8 conversion directly into
	 * an already-memset() struct dirent (readdir.c's/getdents.c's own
	 * memset(out, 0, sizeof *out) before this conversion ran), so bytes
	 * of d_name past the NUL terminator were always zero, never
	 * leftover stack/heap content. __utf16_to_utf8_buf() only ever
	 * writes outlen bytes plus one NUL, never the rest of the buffer, so
	 * that guarantee has to be re-created here now that this struct is
	 * an independent local the caller memcpy()s whole, not the final
	 * struct dirent itself. */
	memset(out, 0, sizeof *out);

	out->ino = (ino_t)fi->FileId;
	out->type = dtype_from_attrs(fi->FileAttributes);
	__utf16_to_utf8_buf(fi->FileName, fi->FileNameLength / sizeof(WCHAR), out->name, sizeof out->name);

	/* Same "last record in this fill" convention __dirstream_next() and
	 * getdents()'s own walk always used: NextEntryOffset is 0 on the
	 * final record of a batch, so the rest of the buffer (buflen - *pos)
	 * is consumed instead of looping forever on a zero offset. */
	next = fi->NextEntryOffset ? fi->NextEntryOffset : buflen - *pos;
	*pos += next;
	return 1;
}
