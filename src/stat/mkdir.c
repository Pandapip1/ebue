#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	(void)mode;

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtCreateFile(&h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &np.oa, &io, 0, FILE_ATTRIBUTE_NORMAL,
	                  FILE_SHARE_VALID_FLAGS, FILE_CREATE,
	                  FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT, 0, 0);
	__ntpath_free(&np);
	if (st == STATUS_OBJECT_NAME_COLLISION) { errno = EEXIST; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	NtClose(h);
	return 0;
}

int mkdir(const char *path, mode_t mode) { return mkdirat(AT_FDCWD, path, mode); }
