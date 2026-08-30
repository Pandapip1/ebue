/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "libc.h"

/* pread/pwrite pass an explicit ByteOffset to NtReadFile/NtWriteFile, but
 * our file handles are opened FILE_SYNCHRONOUS_IO_NONALERT, and for such
 * handles the file system sets FileObject->CurrentByteOffset to the end
 * of the transfer (see ReactOS drivers/filesystems/vfatfs/rw.c, the
 * FO_SYNCHRONOUS_IO check after the read completes; Wine does the same
 * with lseek in dlls/ntdll/unix/file.c NtReadFile).  POSIX wants the
 * position left alone, so callers save it with __fd_pos_save before the
 * transfer and put it back with __fd_pos_restore afterwards.
 *
 * This is not atomic with respect to other threads using the same fd,
 * but this libc has no threads, so that is fine. */

int __fd_pos_save(HANDLE h, long long *pos)
{
	IO_STATUS_BLOCK io;
	FILE_POSITION_INFORMATION pi;
	NTSTATUS st;

	st = NtQueryInformationFile(h, &io, &pi, sizeof pi, FilePositionInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*pos = pi.CurrentByteOffset;
	return 0;
}

void __fd_pos_restore(HANDLE h, long long pos)
{
	IO_STATUS_BLOCK io;
	FILE_POSITION_INFORMATION pi;

	pi.CurrentByteOffset = pos;
	NtSetInformationFile(h, &io, &pi, sizeof pi, FilePositionInformation);
}
