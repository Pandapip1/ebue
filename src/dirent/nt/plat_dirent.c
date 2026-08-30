/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_dirent.h -- see that header
 * for the contract this makes.  Everything here was, until this file
 * existed, inline inside src/dirent/readdir.c's __dirstream_next() and
 * src/dirent/getdents.c's getdents(); nothing changed in substance,
 * only location and the addition of a POSIX-shaped return (byte count,
 * 0 at end-of-directory, or -1/errno) in place of a raw NTSTATUS.
 */
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
