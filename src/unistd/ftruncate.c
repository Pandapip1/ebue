#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

int ftruncate(int fd, off_t len)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	FILE_END_OF_FILE_INFORMATION eof;
	NTSTATUS st;
	if (!f) return -1;
	if (len < 0) { errno = EINVAL; return -1; }
	eof.EndOfFile = len;
	st = NtSetInformationFile(f->h, &io, &eof, sizeof eof, FileEndOfFileInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int truncate(const char *path, off_t len)
{
	int fd = open(path, O_WRONLY);
	int r;
	if (fd < 0) return -1;
	r = ftruncate(fd, len);
	close(fd);
	return r;
}
