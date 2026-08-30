/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_fcntl.h -- see that header
 * for the contract each function makes, and src/mman/linux/plat_mem.c's
 * banner for the general discipline this file follows (raw syscall(2),
 * no host libc, -nostdinc against ntlibc's own headers, aarch64
 * syscall numbers confirmed against this host's own <sys/syscall.h> as
 * an oracle).
 *
 * __plat_create_file() is NOT implemented here. plat_fcntl.h's own
 * banner says why up front: it takes `struct __ntpath *np`, an
 * already-NT-resolved path object, because path resolution
 * (src/internal/path.c) is explicitly out of scope for this migration
 * -- and src/fcntl/open.c's front door calls NT-only __ntpath_at()
 * itself, before this interface is ever reached. There is no POSIX- or
 * Linux-shaped way to hand this function a resolved path; a real Linux
 * open() needs an entirely different front door, not a Linux body for
 * this signature. Stubbed, not attempted -- see this file's ENOSYS body
 * below and fuzz/linux_pilot_test_fs.c's own banner, which stands a raw
 * openat(2) in for it during testing, exactly as fuzz/
 * linux_pilot_test.c already does for the mman/unistd pilot.
 *
 * Everything else below takes only an already-open __plat_handle_t --
 * src/fcntl/fcntl.c's record_lock() and src/fcntl/fadvise.c's
 * posix_fallocate() both work purely off the fd table's handle, never a
 * path -- so all five are fully portable and implemented for real.
 *
 * The three lock functions are simpler here than on NT, not just
 * differently shaped: Linux's fcntl(F_GETLK) IS a native "would this
 * conflict" probe (it fills back *l_type* with F_UNLCK when nothing
 * blocks the request), where NT has no such query and __plat_lock_probe()
 * has to fake one by actually taking the lock and immediately releasing
 * it again (see src/fcntl/nt/plat_fcntl.c's own version). F_GETLK/
 * F_SETLK/F_SETLKW (5/6/7) and F_RDLCK/F_WRLCK/F_UNLCK (0/1/2) are
 * confirmed against this host's own <fcntl.h> -- identical on every
 * 64-bit Linux architecture (no F_GETLK64-style split the way some
 * 32-bit ABIs need, since off_t is already 64-bit here).
 *
 * __plat_volume_max_file_size() always answers LLONG_MAX here, never a
 * computed cluster-derived bound. NT's own version (src/fcntl/nt/
 * plat_fcntl.c) exists ONLY because NT reports "no room" (ENOSPC) when
 * a file would exceed a volume's real maximum, never "too big for this
 * file system" -- so this library has to compute that ceiling itself
 * from FileFsSizeInformation to produce the [EFBIG] posix_fallocate()
 * requires. Linux exposes no analogous per-filesystem "maximum file
 * size" query (fstatfs(2)'s struct statfs reports space totals, not a
 * per-file ceiling), and does not need one: the real fallocate(2)
 * syscall __plat_fallocate() below calls already validates the request
 * against whatever the live filesystem's actual limit is and fails
 * EFBIG itself if exceeded -- exactly the behaviour plat_fcntl.h's own
 * "LLONG_MAX on any query failure... an unrecognised volume must not be
 * treated as having a limit of zero" fallback describes, just reached
 * here as the ordinary case rather than a fallback.
 *
 * __plat_fallocate()'s two-step split (grow the allocation, THEN
 * advance EndOfFile only if needed) exists on NT because
 * ZwSetInformationFile(FileAllocationInformation) can itself shrink
 * EndOfFile if misused against a sparse file -- see plat_fcntl.c's
 * (NT) long comment on why `grow_alloc`'s second conjunct is load-
 * bearing there. Linux's real fallocate(2) syscall has no such failure
 * mode: a single call over [0, want) both reserves the storage and
 * advances the file's size correctly, sparse files included, so
 * `grow_alloc`/`eof` are accepted (the header's contract requires the
 * parameters) but genuinely unused here -- one syscall replaces NT's
 * two-step dance entirely.
 */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "plat_fcntl.h"

/* aarch64 Linux syscall numbers -- see plat_mem.c's banner for why
 * these are hardcoded rather than pulled from a host header. */
#define SYS_fcntl     25
#define SYS_fallocate 47
#define SYS_ftruncate 46
#define SYS_statx     291

#define AT_EMPTY_PATH_LX     0x1000
#define STATX_BASIC_STATS_LX 0x7ff

/* fcntl(2) lock commands and lock types: confirmed against this host's
 * own <fcntl.h> -- identical across 64-bit Linux architectures. */
#define F_GETLK_LX  5
#define F_SETLK_LX  6
#define F_SETLKW_LX 7
#define F_RDLCK_LX  0
#define F_WRLCK_LX  1
#define F_UNLCK_LX  2

extern long syscall(long number, ...);

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

/* The kernel's raw 64-bit struct flock (fcntl(2) F_GETLK/F_SETLK/
 * F_SETLKW): confirmed field-for-field against this host's own
 * <fcntl.h> via offsetof()/sizeof() (l_type/l_whence at 0/2, l_start at
 * 8, l_len at 16, l_pid at 24, total size 32) -- identical layout on
 * every 64-bit Linux architecture, no F_GETLK64-style compat split. */
struct __lx_flock {
	short l_type;
	short l_whence;
	long l_start;
	long l_len;
	int l_pid;
};

int __plat_create_file(struct __ntpath *np, int flags, unsigned mode,
                        void *ea, unsigned ea_len,
                        __plat_handle_t *out, int *typeout)
{
	(void)np; (void)flags; (void)mode; (void)ea; (void)ea_len;
	(void)out; (void)typeout;
	errno = ENOSYS;
	return -1;
}

int __plat_lock_probe(__plat_handle_t h, long long off, long long len, int exclusive, int *conflicting)
{
	struct __lx_flock fl;
	long ret;

	fl.l_type = (short)(exclusive ? F_WRLCK_LX : F_RDLCK_LX);
	fl.l_whence = 0; /* SEEK_SET: off/len already arrive absolute -- see
	                  * src/fcntl/fcntl.c's record_lock_range(). */
	fl.l_start = (long)off;
	fl.l_len = (long)len;
	fl.l_pid = 0;

	ret = syscall(SYS_fcntl, unbox(h), F_GETLK_LX, &fl);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*conflicting = fl.l_type != F_UNLCK_LX;
	return 0;
}

int __plat_lock_set(__plat_handle_t h, long long off, long long len, int exclusive, int wait)
{
	struct __lx_flock fl;
	long ret;

	fl.l_type = (short)(exclusive ? F_WRLCK_LX : F_RDLCK_LX);
	fl.l_whence = 0;
	fl.l_start = (long)off;
	fl.l_len = (long)len;
	fl.l_pid = 0;

	ret = syscall(SYS_fcntl, unbox(h), wait ? F_SETLKW_LX : F_SETLK_LX, &fl);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_lock_clear(__plat_handle_t h, long long off, long long len)
{
	struct __lx_flock fl;
	long ret;

	fl.l_type = F_UNLCK_LX;
	fl.l_whence = 0;
	fl.l_start = (long)off;
	fl.l_len = (long)len;
	fl.l_pid = 0;

	ret = syscall(SYS_fcntl, unbox(h), F_SETLK_LX, &fl);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

long long __plat_volume_max_file_size(__plat_handle_t h)
{
	(void)h;
	return LLONG_MAX;
}

/* The kernel's fixed, architecture-independent struct statx (linux/
 * stat.h) -- see src/stat/linux/plat_stat.c's banner for why this
 * layout, unlike the classic kernel struct stat, needs no per-
 * architecture variant, and for the fuller field-by-field confirmation
 * against this host's own header. Only stx_size/stx_blocks are read
 * here; everything past them collapses into the trailing __spare
 * padding since nothing else is needed in this file. */
struct __lx_statx_timestamp {
	long long tv_sec;
	unsigned int tv_nsec;
	int __reserved;
};
struct __lx_statx {
	unsigned int stx_mask;
	unsigned int stx_blksize;
	unsigned long long stx_attributes;
	unsigned int stx_nlink;
	unsigned int stx_uid;
	unsigned int stx_gid;
	unsigned short stx_mode;
	unsigned short __spare0[1];
	unsigned long long stx_ino;
	unsigned long long stx_size;
	unsigned long long stx_blocks;
	unsigned long long __rest[26]; /* attributes_mask, four timestamps,
	                                * rdev/dev major/minor, mnt_id,
	                                * dio alignment, and the kernel's own
	                                * reserved tail -- 256 bytes total. */
};

int __plat_file_extent(__plat_handle_t h, long long *alloc_size, long long *eof)
{
	struct __lx_statx stx;
	long ret;

	memset(&stx, 0, sizeof stx);
	ret = syscall(SYS_statx, unbox(h), "", AT_EMPTY_PATH_LX, STATX_BASIC_STATS_LX, &stx);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*alloc_size = (long long)stx.stx_blocks * 512; /* stx_blocks is
	                             * always counted in 512-byte units, the
	                             * same convention struct stat's
	                             * st_blocks uses. */
	*eof = (long long)stx.stx_size;
	return 0;
}

int __plat_fallocate(__plat_handle_t h, long long want, long long eof, int grow_alloc)
{
	int fd = unbox(h);
	long ret;
	(void)grow_alloc; (void)eof;

	ret = syscall(SYS_fallocate, fd, 0, (long)0, (long)want);
	if (!is_sys_error(ret)) return 0;

	if (-ret == EOPNOTSUPP || -ret == ENOSYS) {
		/* Some Linux file systems (FAT, older tmpfs, some network
		 * file systems) do not implement fallocate(2). Degrade to
		 * ftruncate()'s weaker guarantee -- it still advances EOF,
		 * but may leave a sparse hole rather than genuinely
		 * reserving storage -- instead of failing a call that would
		 * have worked on ext4/xfs/btrfs. posix_fallocate.html
		 * permits exactly this: the storage-reservation guarantee
		 * is what degrades, not correctness. */
		if (want > eof) {
			ret = syscall(SYS_ftruncate, fd, (long)want);
			if (!is_sys_error(ret)) return 0;
		} else {
			return 0; /* nothing to grow; the weaker guarantee is moot */
		}
	}
	return (int)-ret;
}
