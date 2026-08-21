/* chmod can only express one thing on NTFS: whether the file is read-only. */
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

static int chmod_handle(HANDLE h, mode_t mode)
{
	IO_STATUS_BLOCK io;
	FILE_BASIC_INFORMATION bi, set;
	NTSTATUS st;
	st = NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	set.CreationTime = set.LastAccessTime = set.LastWriteTime = set.ChangeTime = 0;
	set.FileAttributes = bi.FileAttributes;
	if (mode & 0222) set.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
	else set.FileAttributes |= FILE_ATTRIBUTE_READONLY;
	if (!(set.FileAttributes & ~FILE_ATTRIBUTE_ARCHIVE)) set.FileAttributes |= FILE_ATTRIBUTE_NORMAL;
	if (set.FileAttributes == bi.FileAttributes) return 0;
	st = NtSetInformationFile(h, &io, &set, sizeof set, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int fchmod(int fd, mode_t mode)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;
	if (f->type != __FD_FILE && f->type != __FD_DIR) return 0;
	return chmod_handle(f->h, mode);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	int r;
	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT | (flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0));
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	r = chmod_handle(h, mode);
	NtClose(h);
	return r;
}

int chmod(const char *path, mode_t mode) { return fchmodat(AT_FDCWD, path, mode, 0); }
int lchmod(const char *path, mode_t mode) { return fchmodat(AT_FDCWD, path, mode, AT_SYMLINK_NOFOLLOW); }

static mode_t umask_value = 022;
mode_t umask(mode_t m) { mode_t o = umask_value; umask_value = m & 0777; return o; }

int mkfifo(const char *p, mode_t m) { (void)p; (void)m; errno = ENOSYS; return -1; }
int mkfifoat(int d, const char *p, mode_t m) { (void)d; (void)p; (void)m; errno = ENOSYS; return -1; }
int mknod(const char *p, mode_t m, dev_t dv) { (void)p; (void)m; (void)dv; errno = EPERM; return -1; }
int mknodat(int d, const char *p, mode_t m, dev_t dv) { (void)d; (void)p; (void)m; (void)dv; errno = EPERM; return -1; }
