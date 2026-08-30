/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_fcntl.h -- see that header
 * for the contract each function makes.  Everything here was, until
 * this file existed, inline inside src/fcntl/{open,fcntl,fadvise}.c;
 * nothing changed in substance, only location and the addition of a
 * POSIX-shaped return (0/-1 with errno set, or a POSIX error number
 * directly for __plat_fallocate() -- see its own comment) in place of
 * a raw NTSTATUS.
 *
 * __plat_open() absorbed the rest of what used to be src/fcntl/open.c's
 * __open_handle() (everything past its portable /dev/std* special case:
 * VFS-overlay resolution, __ntpath_at(), the $LXMOD extended-attribute
 * buffer) on top of what was already here as __plat_create_file() --
 * this backend now owns the ENTIRE NT-specific path-to-handle journey,
 * not just the NtCreateFile call at the end of it, matching
 * plat_fcntl.h's own updated banner. Nothing in the moved logic
 * changed in substance, only location -- this is the identical
 * sequence __open_handle() used to run inline, verified line for line
 * against the pre-refactor version.
 */
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include "libc.h"
#include "plat_fcntl.h"

int __plat_open(int dirfd, const char *path, int flags, unsigned mode,
                __plat_handle_t *out, int *typeout, int *vfsout, int *vfsnativeout)
{
	struct __ntpath np;
	unsigned char mode_ea[32];
	void *ea = 0;
	unsigned ea_len = 0;
	int vfs, native;
	IO_STATUS_BLOCK io;
	ACCESS_MASK access;
	ULONG disposition, options, attrs;
	NTSTATUS st;
	HANDLE h;

	vfs = __vfs_resolve_at(dirfd, path);
	if (vfs < 0) return -1;
	native = (vfs & __VFS_NATIVE) != 0;
	if (native) {
		*vfsout = __VFS_KIND(vfs);
		*vfsnativeout = 1;
		vfs = __VFS_NONE;
	}
	if (vfs == __VFS_MISSING) {
		errno = flags & O_CREAT ? EROFS : ENOENT;
		return -1;
	}
	if (vfs == __VFS_ROOT || vfs == __VFS_DEV) {
		if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) { errno = EEXIST; return -1; }
		if ((flags & O_ACCMODE) != O_RDONLY || (flags & O_TRUNC)) { errno = EISDIR; return -1; }
		if (__vfs_open_dir(vfs, flags & O_CLOEXEC, out) < 0) return -1;
		*typeout = __FD_DIR;
		*vfsout = vfs;
		return 0;
	}
	if (vfs == __VFS_CONSOLE || vfs == __VFS_NULL || vfs == __VFS_TTY) {
		if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) { errno = EEXIST; return -1; }
		if (flags & O_DIRECTORY) { errno = ENOTDIR; return -1; }
		path = vfs == __VFS_NULL ? "NUL" : "CON";
		dirfd = AT_FDCWD;
		*vfsout = vfs;
	}

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE | (flags & O_CLOEXEC ? 0 : OBJ_INHERIT)) < 0)
		return -1;

	/* open.html DESCRIPTION: mode is ANDed with the complement of umask.
	 * The $LXMOD extended-attribute buffer is this library's own POSIX-
	 * mode-persistence strategy (see src/stat/lxmod.c). */
	if (flags & O_CREAT) {
		mode = mode & ~__umask_get() & 07777;
		ea_len = __lxmod_create_buffer(mode_ea, S_IFREG | mode);
		ea = mode_ea;
	}

	access = SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_READ_EA;
	switch (flags & O_ACCMODE) {
	case O_RDONLY: access |= FILE_GENERIC_READ; break;
	case O_WRONLY: access |= FILE_GENERIC_WRITE; break;
	case O_RDWR:   access |= FILE_GENERIC_READ | FILE_GENERIC_WRITE; break; // NOLINT(misc-redundant-expression) -- both masks include SYNCHRONIZE, harmless ORed twice
	/* The fourth access mode, 03, is O_EXEC and O_SEARCH -- equal
	 * values, as fcntl.h.html permits.  Refused, not served: each asks
	 * for a handle that can do LESS than a read handle (execute-only on
	 * a file, traverse-only on a directory), so quietly widening either
	 * to O_RDONLY would grant more access than the caller asked for and
	 * return success -- the one way a request to be restricted must not
	 * fail.  [EINVAL] "The value of the oflag argument is not valid" is
	 * also what this arm answered before those two names existed, so
	 * naming them changed no behaviour.  Serving them for real means
	 * FILE_EXECUTE / FILE_TRAVERSE access masks and the fd-table and
	 * read()/write() checks that go with a mode neither reads nor
	 * writes; that is not done here. */
	default: __ntpath_free(&np); errno = EINVAL; return -1;
	}
	if (flags & O_APPEND) access = (access & ~FILE_WRITE_DATA) | FILE_APPEND_DATA;
	if (flags & O_TRUNC) access |= FILE_WRITE_DATA;   /* overwrite needs it */
	if (flags & O_PATH) access = SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_READ_EA;

	switch (flags & (O_CREAT | O_EXCL | O_TRUNC)) {
	case 0:
	case O_EXCL:                  disposition = FILE_OPEN; break;
	case O_CREAT:                 disposition = FILE_OPEN_IF; break;
	case O_CREAT | O_EXCL:
	case O_CREAT | O_EXCL | O_TRUNC: disposition = FILE_CREATE; break;
	case O_TRUNC:
	case O_TRUNC | O_EXCL:        disposition = FILE_OVERWRITE; break;
	case O_CREAT | O_TRUNC:       disposition = FILE_OVERWRITE_IF; break;
	default: disposition = FILE_OPEN; break;
	}

	options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT;
	if (flags & O_DIRECTORY) options |= FILE_DIRECTORY_FILE;
	else if (disposition != FILE_OPEN && disposition != FILE_OPEN_IF) options |= FILE_NON_DIRECTORY_FILE;
	if (flags & O_NOFOLLOW) options |= FILE_OPEN_REPARSE_POINT;
	if (flags & (O_SYNC | O_DSYNC)) options |= FILE_WRITE_THROUGH;
	if (flags & O_DIRECT) options |= FILE_NO_INTERMEDIATE_BUFFERING;

	attrs = FILE_ATTRIBUTE_NORMAL;
	/* open.html DESCRIPTION: mode is ANDed with the complement of umask.
	 * NtCreateFile only applies its EA buffer when it creates the object,
	 * so O_CREAT on an existing file cannot overwrite that file's mode. */
	if (flags & O_CREAT) {
		if (!(mode & 0222)) attrs = FILE_ATTRIBUTE_READONLY;
	}

	st = NtCreateFile(&h, access, &np.oa, &io, 0, attrs, FILE_SHARE_VALID_FLAGS,
	                  disposition, options, ea, ea_len);

	/* A directory opened without O_DIRECTORY for reading: allowed by
	 * POSIX (reads then fail with EISDIR); NT refuses FILE_NON_DIRECTORY
	 * only when we asked for it, and refuses data access on directories
	 * with STATUS_FILE_IS_A_DIRECTORY, so retry as a directory. */
	if (st == STATUS_FILE_IS_A_DIRECTORY && (flags & O_ACCMODE) == O_RDONLY && !(flags & O_CREAT)) {
		options |= FILE_DIRECTORY_FILE;
		access = SYNCHRONIZE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES |
		         FILE_READ_EA | FILE_TRAVERSE;
		st = NtCreateFile(&h, access, &np.oa, &io, 0, attrs, FILE_SHARE_VALID_FLAGS, FILE_OPEN, options, 0, 0);
	}
	__ntpath_free(&np);
	/* Writing to a directory is EISDIR, not EACCES. */
	if (st == STATUS_FILE_IS_A_DIRECTORY) { errno = EISDIR; return -1; }
	if (!NT_SUCCESS(st)) {
		/* FILE_CREATE on an existing directory, etc. */
		if (st == STATUS_OBJECT_NAME_COLLISION) errno = EEXIST;
		else __set_errno_status(st);
		return -1;
	}

	*typeout = (options & FILE_DIRECTORY_FILE) ? __FD_DIR : 0;
	*out = h;
	return 0;
}

int __plat_lock_probe(__plat_handle_t h, long long off, long long len, int exclusive, int *conflicting)
{
	IO_STATUS_BLOCK io;
	LARGE_INTEGER o = off, l = len;
	NTSTATUS st;

	*conflicting = 0;
	st = NtLockFile(h, 0, 0, 0, 0, &o, &l, 0, 1, exclusive);
	if (NT_SUCCESS(st)) {
		st = NtUnlockFile(h, &io, &o, &l, 0);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		return 0;
	}
	/* NT does not expose the owning process for a byte-range lock. */
	if (st == STATUS_FILE_LOCK_CONFLICT || st == STATUS_LOCK_NOT_GRANTED) {
		*conflicting = 1;
		return 0;
	}
	return __set_errno_status(st);
}

int __plat_lock_set(__plat_handle_t h, long long off, long long len, int exclusive, int wait)
{
	LARGE_INTEGER o = off, l = len;
	NTSTATUS st = NtLockFile(h, 0, 0, 0, 0, &o, &l, 0, wait ? 0 : 1, exclusive);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_lock_clear(__plat_handle_t h, long long off, long long len)
{
	IO_STATUS_BLOCK io;
	LARGE_INTEGER o = off, l = len;
	NTSTATUS st = NtUnlockFile(h, &io, &o, &l, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* See fadvise.c's posix_fallocate() for the full derivation and the two
 * Microsoft Learn pages that disagree about it -- this is Microsoft's
 * own rule (cluster_size * (2^32-1)), not a guess, and reproduces the
 * "NTFS overview" support-for-large-volumes table exactly. */
long long __plat_volume_max_file_size(__plat_handle_t h)
{
	IO_STATUS_BLOCK io;
	FILE_FS_SIZE_INFORMATION fsi;
	unsigned long long cluster, lim;

	if (!NT_SUCCESS(NtQueryVolumeInformationFile(h, &io, &fsi, sizeof fsi, FileFsSizeInformation)))
		return LLONG_MAX;
	cluster = (unsigned long long)fsi.SectorsPerAllocationUnit * fsi.BytesPerSector;
	if (!cluster) return LLONG_MAX;
	lim = cluster * 4294967295ULL;
	return lim > (unsigned long long)LLONG_MAX ? LLONG_MAX : (long long)lim;
}

int __plat_file_extent(__plat_handle_t h, long long *alloc_size, long long *eof)
{
	IO_STATUS_BLOCK io;
	FILE_STANDARD_INFORMATION si;
	NTSTATUS st = NtQueryInformationFile(h, &io, &si, sizeof si, FileStandardInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*alloc_size = si.AllocationSize;
	*eof = si.EndOfFile;
	return 0;
}

/* `grow_alloc` decides whether the AllocationSize step below runs at
 * all -- the front door (fadvise.c's posix_fallocate()) computes it as
 * `want > alloc_size && want >= eof`, and here is why both conjuncts
 * are load-bearing, moved here verbatim from where this logic used to
 * live inline in posix_fallocate() itself:
 *
 * ZwSetInformationFile(FileAllocationInformation) is documented (ntifs.h
 * FILE_ALLOCATION_INFORMATION, "Remarks") as: "If the allocation size is
 * set to a value that is less than the end-of-file position, the
 * end-of-file position is automatically adjusted to match the
 * allocation size."  The requested size is also rounded up to the
 * filesystem's cluster size first, so the value that is compared
 * against EndOfFile is not the one passed in.
 *
 * `want > alloc_size` alone is safe only while alloc_size >= eof, which
 * is the ordinary NTFS case but is exactly what a sparse or compressed
 * file breaks: such a file's allocation is deliberately smaller than
 * its size.  On one -- EndOfFile 16384, AllocationSize 0 --
 * posix_fallocate(fd, 0, 100) would have passed the guard, requested an
 * allocation of 100, had it rounded to one cluster, and had the file
 * truncated to that cluster.  POSIX (posix_fallocate) never shrinks a
 * file: "If the offset+len is beyond the current file size, then
 * posix_fallocate() shall adjust the file size"; below that it changes
 * no size at all.  So requesting an allocation smaller than the current
 * EndOfFile is never something this function may do.
 *
 * Skipping the call is preferred to clamping the request up to eof.
 * Clamping would satisfy the allocation guarantee for the requested
 * range, but it would also de-sparsify the entire file:
 * posix_fallocate(fd, 0, 1) on a terabyte-sized sparse file would ask
 * for a terabyte of clusters and most likely return ENOSPC.  Turning a
 * hundred-byte request into a whole-file materialisation -- or into a
 * hard failure -- is a worse outcome than under-delivering an
 * allocation guarantee on a file shape whose whole purpose is to not
 * have that allocation.  Nothing is destroyed either way.
 *
 * DO NOT DELETE THE SECOND CONJUNCT AS REDUNDANT.  It reads that way
 * from here -- on a file whose alloc_size >= eof, want > alloc_size
 * already implies want > eof -- and that is true of real NTFS, where a
 * file extended with SetEndOfFile gets real clusters.  It is not true
 * under Wine, which implements extension with ftruncate(), producing a
 * hole: st_blocks is 0, so AllocationSize reads 0 for an ORDINARY file
 * created the normal way, not merely for one deliberately marked
 * sparse.  Under Wine the first test is therefore trivially true in the
 * common case and this conjunct is the only thing preventing the
 * truncation.  Measured on Windows 11 22621 by the Wine-divergence
 * session: a non-sparse file of EndOfFile 16384 reports AllocationSize
 * 16384 on NTFS, and 0 under Wine.  (A genuinely sparse file reports 0
 * on both -- that part Wine gets right.)
 *
 * Not reproduced from inside this tree: ntlibc has no FSCTL_SET_SPARSE
 * and Wine's FSCTL_SET_ZERO_DATA returns STATUS_NOT_SUPPORTED, so a
 * deliberately sparse file cannot be built here.  That negative result
 * is what the interlock was written without -- it rests on the
 * documented FileAllocationInformation rule above.  The Wine finding
 * arrived afterwards and says the guard is exercised in practice
 * anyway, by ordinary files, without anyone creating a sparse one. */
int __plat_fallocate(__plat_handle_t h, long long want, long long eof, int grow_alloc)
{
	IO_STATUS_BLOCK io;
	FILE_ALLOCATION_INFORMATION ai;
	FILE_END_OF_FILE_INFORMATION eofi;
	NTSTATUS st;

	if (grow_alloc) {
		ai.AllocationSize = want;
		st = NtSetInformationFile(h, &io, &ai, sizeof ai, FileAllocationInformation);
		/* Real Windows honours this; Wine's ntdll does not implement
		 * FileAllocationInformation at all (it appears only in the
		 * set-info size table in dlls/ntdll/unix/file.c and falls
		 * through to the default arm) and every other failure short of
		 * that is a real error worth reporting (e.g. ENOSPC). Falling
		 * through on "no such information class here" still leaves the
		 * EndOfFile extension below to grow the file -- a strict
		 * reading of posix_fallocate() loses the "no later write can
		 * ENOSPC" guarantee on such a system, but the alternative is
		 * failing a real Windows-capable call every time it merely runs
		 * under Wine, which is worse than the degraded guarantee.
		 *
		 * Branch on the *status*, not on __errno_from_status().  The
		 * errno mapping is a lossy projection: it folds many distinct
		 * statuses onto one value, so a test against it silently
		 * widens.  Concretely, Wine reports the same missing set-info
		 * case as STATUS_NOT_IMPLEMENTED natively but as
		 * STATUS_INVALID_INFO_CLASS under WOW64; the latter maps to
		 * EINVAL, so an ENOSYS test tolerated the gap on x86_64 and
		 * rejected it on i386.  Widening the test to EINVAL would be
		 * worse still -- EINVAL also carries STATUS_INVALID_PARAMETER,
		 * STATUS_INFO_LENGTH_MISMATCH and STATUS_DATATYPE_MISALIGNMENT,
		 * turning this fallback into a bug-hider.  Whenever the status
		 * is in hand, decide from it. */
		if (!NT_SUCCESS(st)
		    && st != STATUS_NOT_IMPLEMENTED
		    && st != STATUS_NOT_SUPPORTED
		    && st != STATUS_INVALID_DEVICE_REQUEST
		    && st != STATUS_INVALID_INFO_CLASS)
			return __errno_from_status(st);
	}
	if (want > eof) {
		eofi.EndOfFile = want;
		st = NtSetInformationFile(h, &io, &eofi, sizeof eofi, FileEndOfFileInformation);
		if (!NT_SUCCESS(st)) return __errno_from_status(st);
	}
	return 0;
}
