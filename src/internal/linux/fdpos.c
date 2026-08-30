/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux's half of the src/internal/nt/fdpos.c split. See that file's
 * own banner for why NT needs this at all: its handles are opened
 * FILE_SYNCHRONOUS_IO_NONALERT, so a positioned pread/pwrite still
 * advances CurrentByteOffset as a side effect, and callers save/restore
 * it around the transfer to keep POSIX's "pread/pwrite never move the
 * file position" contract. Linux's pread64(2)/pwrite64(2) take the
 * offset as an explicit syscall argument and never touch the fd's own
 * position at all -- there is nothing to save or restore, so these are
 * real, correct no-ops for this platform, not stubs standing in for
 * unwritten work.
 */
#include "libc.h"

int __fd_pos_save(HANDLE h, long long *pos)
{
	(void)h;
	*pos = 0;
	return 0;
}

void __fd_pos_restore(HANDLE h, long long pos)
{
	(void)h;
	(void)pos;
}
