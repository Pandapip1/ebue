/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The descriptor table: small integers over NT handles.
 *
 * Descriptors 0, 1 and 2 come from the PEB's process parameters, which is
 * where the process that started this one put them.  Descriptors above 2
 * are passed between ntlibc (and msvcrt) programs in the RuntimeData
 * field of the same block, in the layout the Microsoft C runtime has used
 * since the 16-bit days:
 *
 *     int count; unsigned char osfile[count]; HANDLE osfhnd[count];
 *
 * so that a child started by execve or posix_spawn sees the fds its
 * parent meant it to, including ones redirected by a shell.  The table
 * itself lives in this process's data and is therefore copied whole by
 * fork.
 */
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

struct __fd __fds[FD_MAX];
int __fd_limit = FD_MAX;

/* msvcrt's _osfile bits */
#define FOPEN      0x01
#define FEOFLAG    0x02
#define FCRLF      0x04
#define FPIPE      0x08
#define FNOINHERIT 0x10
#define FAPPEND    0x20
#define FDEV       0x40
#define FTEXT      0x80

int __handle_type(HANDLE h)
{
	IO_STATUS_BLOCK io;
	FILE_FS_DEVICE_INFORMATION dev;
	NTSTATUS st;

	if (!h || h == (HANDLE)(LONG_PTR)-1) return __FD_UNKNOWN;
	st = NtQueryVolumeInformationFile(h, &io, &dev, sizeof dev, FileFsDeviceInformation);
	if (!NT_SUCCESS(st)) return __FD_UNKNOWN;
	switch (dev.DeviceType) {
	case FILE_DEVICE_DISK:
	case FILE_DEVICE_DISK_FILE_SYSTEM:
	case FILE_DEVICE_NETWORK_FILE_SYSTEM:
	case FILE_DEVICE_CD_ROM:
	case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
	case FILE_DEVICE_VIRTUAL_DISK:
	case FILE_DEVICE_FILE_SYSTEM:
	case FILE_DEVICE_TAPE_FILE_SYSTEM: {
		FILE_STANDARD_INFORMATION si;
		if (NT_SUCCESS(NtQueryInformationFile(h, &io, &si, sizeof si, FileStandardInformation)) && si.Directory)
			return __FD_DIR;
		return __FD_FILE;
	}
	case FILE_DEVICE_CONSOLE:
	case FILE_DEVICE_SCREEN:
		return __FD_CONSOLE;
	case FILE_DEVICE_NAMED_PIPE:
	case FILE_DEVICE_MAILSLOT:
		return __FD_PIPE;
	case FILE_DEVICE_NULL:
	case FILE_DEVICE_SERIAL_PORT:
	default:
		return __FD_CHAR;
	}
}

int __fd_alloc(int lowest)
{
	int i;
	if (lowest < 0) lowest = 0;
	/* __fd_limit, not FD_MAX: setrlimit(RLIMIT_NOFILE) lowers it, and
	 * "a number one greater than the maximum value that the system may
	 * assign to a newly-created descriptor" (setrlimit.html) is exactly
	 * this bound.  It never exceeds FD_MAX, so the table stays in
	 * range whatever a caller asks for. */
	for (i = lowest; i < __fd_limit; i++)
		if (!__fds[i].h) return i;
	errno = EMFILE;
	return -1;
}

int __fd_install_at(int fd, HANDLE h, unsigned flags, int type)
{
	struct __fd *f = &__fds[fd];
	f->h = h;
	f->flags = flags;
	f->type = (unsigned char)(type ? type : __handle_type(h));
	f->eof = 0;
	f->dirflag = 0;
	f->pos = -1;
	return fd;
}

int __fd_install(HANDLE h, unsigned flags, int type)
{
	int fd = __fd_alloc(0);
	if (fd < 0) return -1;
	return __fd_install_at(fd, h, flags, type);
}

struct __fd *__fd_get(int fd)
{
	if (fd < 0 || fd >= FD_MAX || !__fds[fd].h) { errno = EBADF; return 0; }
	return &__fds[fd];
}

HANDLE __fd_handle(int fd)
{
	struct __fd *f = __fd_get(fd);
	return f ? f->h : 0;
}

/* The access mode of a handle this process did not open.
 *
 * The RuntimeData block a parent leaves for its child is msvcrt's
 * _osfile format -- the FOPEN/FAPPEND/FPIPE/FDEV bits above are that
 * format's, byte for byte, which is what lets an ntlibc program and an
 * msvcrt program inherit each other's descriptors.  That format has no
 * access-mode bit: all eight are spoken for, and msvcrt does not carry
 * the mode across inheritance either.  So the mode cannot be recovered
 * from the block, and it must not be added to the block -- a ninth bit
 * would be meaningless to every other CRT that reads this and would
 * make the payload a private extension rather than the interop format
 * it is.
 *
 * It does not need to be.  The handle itself knows: the object manager
 * records the access the handle was opened with, and
 * NtQueryObject(ObjectBasicInformation) reports it in GrantedAccess.
 * That is authoritative rather than advisory -- it is the very mask the
 * kernel will check a read or write against -- and it works whatever
 * CRT the parent was built with, including one that never heard of
 * ntlibc.
 *
 * Measured (Wine, x86_64), GrantedAccess for handles this library opens:
 *   O_RDONLY          0x00120089  READ_DATA, no WRITE_DATA
 *   O_WRONLY          0x00120196  WRITE_DATA, no READ_DATA
 *   O_RDWR            0x0012019f  both
 *   O_WRONLY|O_APPEND 0x00120194  APPEND_DATA only, no WRITE_DATA
 * -- the last because open() maps O_APPEND by trading FILE_WRITE_DATA
 * for FILE_APPEND_DATA (src/fcntl/open.c), so "writable" here has to
 * mean either bit or an appending descriptor reads back as read-only.
 *
 * `fallback` is used only if the query fails, which is not expected:
 * ObjectBasicInformation needs no access rights of its own.  It is
 * O_RDWR for an inherited descriptor, deliberately, and NOT the
 * O_RDONLY that used to be assumed: assuming read-only is precisely
 * what produced the defect this fixes, and the two errors are not
 * symmetric.  Guessing too permissive is corrected by the kernel, which
 * refuses the operation on its own; guessing too restrictive is
 * corrected by nobody, because this library refuses the operation
 * before the kernel is ever asked, on a descriptor that would have
 * worked. */
static unsigned accmode_of(HANDLE h, unsigned fallback)
{
	OBJECT_BASIC_INFORMATION obi;
	ULONG len = 0;
	int r, w;

	if (!NT_SUCCESS(NtQueryObject(h, ObjectBasicInformation, &obi, sizeof obi, &len)))
		return fallback;
	r = (obi.GrantedAccess & FILE_READ_DATA) != 0;
	w = (obi.GrantedAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
	if (r && w) return O_RDWR;
	if (w) return O_WRONLY;
	if (r) return O_RDONLY;
	return fallback;
}

/* Descriptors 0-2, from the process parameters the creator left behind.
 *
 * Both guards below are load-bearing, and the second one is doing more
 * work than it looks like.  A parent cannot reliably say "this one is
 * closed" by writing 0 or -1 here: on real Windows both were measured to
 * arrive as a live, open handle instead (see the long accounting in
 * src/process/spawn.c, and test/spawn-stdhandle-attr.c, which prints the
 * raw arriving value).  Which actor rewrites the field is not known --
 * it is not kernel32 or kernelbase, neither of which is loaded in these
 * ntdll-only processes -- but it is value-blind, so the only thing the
 * receiving side can do is check what actually turned up.  That is what
 * __handle_type() is for here: whatever cannot be identified as a file,
 * console or pipe is not installed, which covers a dead-but-inheritable
 * handle, a duplicated pseudohandle, and the deliberate placeholder
 * __spawn writes for a closed standard descriptor. */
static void install_std(int fd, HANDLE h)
{
	if (!h || h == (HANDLE)(LONG_PTR)-1) return;
	if (__handle_type(h) == __FD_UNKNOWN) return;
	__fd_install_at(fd, h, fd == 0 ? O_RDONLY : O_WRONLY, 0);
}

void __fd_init(void)
{
	PRTL_USER_PROCESS_PARAMETERS pp = __peb->ProcessParameters;
	install_std(0, pp->StandardInput);
	install_std(1, pp->StandardOutput);
	install_std(2, pp->StandardError);

	if (pp->RuntimeData.Buffer && pp->RuntimeData.Length >= sizeof(int)) {
		const unsigned char *p = (const unsigned char *)pp->RuntimeData.Buffer;
		int count, i;
		memcpy(&count, p, sizeof count);
		if (count > FD_MAX) count = FD_MAX;
		if ((size_t)pp->RuntimeData.Length >= sizeof(int) + count * (1 + sizeof(HANDLE))) {
			const unsigned char *osfile = p + sizeof(int);
			const unsigned char *osfhnd = osfile + count;
			for (i = 0; i < count; i++) {
				HANDLE h;
				memcpy(&h, osfhnd + i * sizeof(HANDLE), sizeof h);
				if (!(osfile[i] & FOPEN) || !h || h == (HANDLE)(LONG_PTR)-1) continue;
				if (i < 3 && __fds[i].h) continue;   /* the PEB's std handles win */
				if (__handle_type(h) == __FD_UNKNOWN) continue;
				/* The access mode is NOT in osfile[i] -- see
				 * accmode_of().  Without it every inherited
				 * descriptor read back as O_RDONLY (because
				 * O_RDONLY is 0), and src/unistd/write.c's
				 * write()/pwrite() refuse an O_RDONLY descriptor
				 * with EBADF -- so an inherited writable
				 * descriptor could not be written to at all,
				 * while ftruncate() and posix_fallocate() on the
				 * very same descriptor succeeded, because they
				 * ask the kernel instead of this field. */
				__fd_install_at(i, h, (osfile[i] & FAPPEND ? O_APPEND : 0) |
				                      accmode_of(h, O_RDWR), 0);
			}
		}
	}
}

/* Build the RuntimeData block describing the descriptors a child should
 * inherit: everything open and not close-on-exec.  Handles are made
 * inheritable as a side effect (NtCreateUserProcess copies only those).
 * Returns a malloc'd block and its length, or NULL and 0 with nothing to
 * pass. */
void *__fd_runtime_data(size_t *len)
{
	int count = 0, i;
	unsigned char *blk, *osfile, *osfhnd;

	for (i = 0; i < FD_MAX; i++)
		if (__fds[i].h && !(__fds[i].flags & O_CLOEXEC)) count = i + 1;
	if (count <= 3) { *len = 0; return 0; }

	*len = sizeof(int) + count * (1 + sizeof(HANDLE));
	blk = __malloc(*len);
	if (!blk) { *len = 0; return 0; }
	memcpy(blk, &count, sizeof count);
	osfile = blk + sizeof(int);
	osfhnd = osfile + count;
	for (i = 0; i < count; i++) {
		HANDLE h = 0;
		unsigned char fl = 0;
		if (__fds[i].h && !(__fds[i].flags & O_CLOEXEC)) {
			HANDLE dup;
			fl = FOPEN;
			if (__fds[i].flags & O_APPEND) fl |= FAPPEND;
			if (__fds[i].type == __FD_PIPE) fl |= FPIPE;
			if (__fds[i].type == __FD_CONSOLE || __fds[i].type == __FD_CHAR) fl |= FDEV;
			/* Make the handle itself inheritable, in place. */
			if (NT_SUCCESS(NtDuplicateObject(NtCurrentProcess(), __fds[i].h, NtCurrentProcess(), &dup,
			                                 0, OBJ_INHERIT, DUPLICATE_SAME_ACCESS | DUPLICATE_SAME_ATTRIBUTES))) {
				NtClose(__fds[i].h);
				__fds[i].h = dup;
			}
			h = __fds[i].h;
		}
		osfile[i] = fl;
		memcpy(osfhnd + i * sizeof(HANDLE), &h, sizeof h);
	}
	return blk;
}

int __fd_close_all_cloexec(void)
{
	int i;
	for (i = 0; i < FD_MAX; i++)
		if (__fds[i].h && (__fds[i].flags & O_CLOEXEC)) close(i);
	return 0;
}
