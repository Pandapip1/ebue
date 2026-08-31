/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT's __handle_type()/__fd_init()/__fd_runtime_data() -- moved
 * verbatim out of src/internal/fd.c (which keeps only the genuinely
 * portable fd-table bookkeeping: __fds[], __fd_alloc/_install_at/_get,
 * __fd_close_all_cloexec) so PLATFORM=linux's own src/internal/linux/
 * plat_fd_init.c can supply its own __handle_type()/__fd_init()
 * without colliding at link time -- the same REPLACED_OBJS override
 * crt/linux/crt1.c already uses for crt/crt1.c (see Makefile's
 * PLAT_GLOBS comment).
 *
 * Nothing here changed in substance from fd.c's original -- see that
 * file's own remaining comments (the RuntimeData/_osfile format,
 * accmode_of()'s GrantedAccess measurements) for the rationale behind
 * any of this; this split is purely mechanical, verified by diff
 * against fd.c before this file existed.
 */
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

/* msvcrt's _osfile bits */
#define FOPEN      0x01
#define FEOFLAG    0x02
#define FCRLF      0x04
#define FPIPE      0x08
#define FNOINHERIT 0x10
#define FAPPEND    0x20
#define FDEV       0x40
#define FTEXT      0x80

#define VFS_RUNTIME_MAGIC 0x32534656u /* "VFS2", little-endian */
#define VFS_RUNTIME_CWD_NATIVE 0x80

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

/* pp's own dereferences below (pp->StandardInput and friends) are a
 * disclosed, deliberately unmarked residual, the same class crt/
 * crt1.c's own __libc_start_main() comment already established: pp is
 * a plain local, __peb->ProcessParameters -- a struct FIELD's own
 * value, distinct from __peb itself -- and __fd_init() takes no
 * parameters at all, so there is no signature for `nonnull` to
 * describe this on. See crt1.c's own comment for why this is verified
 * sound by hand regardless (RTL_USER_PROCESS_PARAMETERS is populated
 * by the NT loader before any user-mode instruction runs). */
void __fd_init(void)
{
	PRTL_USER_PROCESS_PARAMETERS pp = __peb->ProcessParameters;
	install_std(0, pp->StandardInput);
	install_std(1, pp->StandardOutput);
	install_std(2, pp->StandardError);

	if (pp->RuntimeData.Buffer && pp->RuntimeData.Length >= sizeof(int)) {
		const unsigned char *p = (const unsigned char *)pp->RuntimeData.Buffer;
		const unsigned char *vfs = 0;
		const unsigned char *vfs_native = 0, *vseen = 0, *vnext = 0;
		size_t base;
		int count, i;
		memcpy(&count, p, sizeof count);
		if (count > FD_MAX) count = FD_MAX;
		base = sizeof(int) + count * (1 + sizeof(HANDLE));
		if ((size_t)pp->RuntimeData.Length >= base) {
			const unsigned char *osfile = p + sizeof(int);
			const unsigned char *osfhnd = osfile + count;
			if ((size_t)pp->RuntimeData.Length >= base + 9 + 4 * (size_t)count) {
				unsigned magic, trailer_count;
				memcpy(&magic, p + base, sizeof magic);
				memcpy(&trailer_count, p + base + 4, sizeof trailer_count);
				if (magic == VFS_RUNTIME_MAGIC && trailer_count == (unsigned)count) {
					vfs = p + base + 8;
					vfs_native = vfs + count;
					vseen = vfs_native + count;
					vnext = vseen + count;
					int cwd = vnext[count];
					if (cwd & VFS_RUNTIME_CWD_NATIVE)
						cwd = __VFS_NATIVE | (cwd & ~VFS_RUNTIME_CWD_NATIVE);
					__vfs_cwd_set(cwd);
				}
			}
			for (i = 0; i < count; i++) {
				HANDLE h;
				int vk = vfs ? vfs[i] : __VFS_NONE;
				memcpy(&h, osfhnd + i * sizeof(HANDLE), sizeof h);
				if (!(osfile[i] & FOPEN) || !h || h == (HANDLE)(LONG_PTR)-1) continue;
				if (i < 3 && __fds[i].h) {
					if (vk) {
						__fds[i].vfs = (unsigned char)vk;
						__fds[i].vfs_native = vfs_native[i];
						__fds[i].vseen = vseen[i];
						__fds[i].vnext = vnext[i];
						if (vk == __VFS_ROOT || vk == __VFS_DEV) __fds[i].type = __FD_DIR;
					}
					continue;   /* the PEB's std handles win */
				}
				if (!vk && __handle_type(h) == __FD_UNKNOWN) continue;
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
				                      (vk == __VFS_ROOT || vk == __VFS_DEV ? O_RDONLY : accmode_of(h, O_RDWR)),
				                vk == __VFS_ROOT || vk == __VFS_DEV ? __FD_DIR : 0);
				__fds[i].vfs = (unsigned char)vk;
				if (vk) {
					__fds[i].vfs_native = vfs_native[i];
					__fds[i].vseen = vseen[i];
					__fds[i].vnext = vnext[i];
				}
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
	int count = 0, i, have_vfs = __vfs_cwd_get() != __VFS_NONE;
	unsigned char *blk, *osfile, *osfhnd;

	for (i = 0; i < FD_MAX; i++) {
		if (__fds[i].h && !(__fds[i].flags & O_CLOEXEC)) {
			count = i + 1;
			if (__fds[i].vfs) have_vfs = 1;
		}
	}
	if (count <= 3 && !have_vfs) { *len = 0; return 0; }

	*len = sizeof(int) + count * (1 + sizeof(HANDLE)) + (have_vfs ? 9 + 4 * count : 0);
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
	if (have_vfs) {
		unsigned magic = VFS_RUNTIME_MAGIC, trailer_count = (unsigned)count;
		int cwd = __vfs_cwd_get();
		unsigned char *trailer = osfhnd + count * sizeof(HANDLE);
		memcpy(trailer, &magic, sizeof magic);
		memcpy(trailer + 4, &trailer_count, sizeof trailer_count);
		for (i = 0; i < count; i++) trailer[8 + i] = __fds[i].vfs;
		for (i = 0; i < count; i++) trailer[8 + count + i] = __fds[i].vfs_native;
		for (i = 0; i < count; i++) trailer[8 + 2 * count + i] = __fds[i].vseen;
		for (i = 0; i < count; i++) trailer[8 + 3 * count + i] = __fds[i].vnext;
		trailer[8 + 4 * count] = (unsigned char)(__VFS_KIND(cwd) |
			((cwd & __VFS_NATIVE) ? VFS_RUNTIME_CWD_NATIVE : 0));
	}
	return blk;
}
