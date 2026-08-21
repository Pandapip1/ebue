/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <errno.h>
#include "libc.h"

off_t lseek(int fd, off_t off, int whence)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	FILE_POSITION_INFORMATION pi;
	FILE_STANDARD_INFORMATION si;
	NTSTATUS st;
	long long base;

	if (!f) return -1;
	if (f->type != __FD_FILE) { errno = ESPIPE; return -1; }

	switch (whence) {
	case SEEK_SET: base = 0; break;
	case SEEK_CUR:
		st = NtQueryInformationFile(f->h, &io, &pi, sizeof pi, FilePositionInformation);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		base = pi.CurrentByteOffset;
		break;
	case SEEK_END:
		st = NtQueryInformationFile(f->h, &io, &si, sizeof si, FileStandardInformation);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		base = si.EndOfFile;
		break;
	default: errno = EINVAL; return -1;
	}
	if (base + off < 0) { errno = EINVAL; return -1; }
	pi.CurrentByteOffset = base + off;
	st = NtSetInformationFile(f->h, &io, &pi, sizeof pi, FilePositionInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return pi.CurrentByteOffset;
}
