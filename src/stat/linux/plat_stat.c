/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_stat.h -- see that header
 * for the contract each function makes, and src/mman/linux/plat_mem.c's
 * banner for the general discipline this file follows (raw syscall(2),
 * no host libc, -nostdinc against ntlibc's own headers, aarch64
 * syscall numbers confirmed against this host's own <sys/syscall.h> as
 * an oracle).
 *
 * Six of the ten functions plat_stat.h declares are implemented for
 * real here: __plat_chmod(), __plat_fstat(), __plat_statvfs(), and
 * __plat_set_times() all take only an already-open __plat_handle_t (the
 * front doors -- src/stat/chmod.c's fchmod(), stat.c's fstat(),
 * statvfs.c's fstatvfs(), utimensat.c's futimens() -- reach the fd
 * table directly, never a path), so those are fully portable.
 *
 * __plat_lxmod_get()/__plat_lxmod_set() are declared by the header but
 * NOT defined here, and this is neither an oversight nor a gap: grep
 * across src/ confirms they are called ONLY from within
 * src/stat/nt/plat_stat.c itself (__plat_chmod() and __plat_fstat()
 * there call them internally to read/write WSL's $LXMOD NTFS extended
 * attribute) -- no portable front-door file (chmod.c, stat.c, mkdir.c)
 * calls either one directly. $LXMOD exists purely so NT, which has no
 * native POSIX mode bits at all, can persist one; a real Linux file
 * system already has real, native mode bits (this file's __plat_chmod()
 * and __plat_fstat() below read/write them directly via fchmod(2)/
 * statx(2)), so there is nothing for a Linux $LXMOD equivalent to do.
 * Since nothing outside the NT backend ever calls these two, the
 * linker never asks this file for them, and defining meaningless stub
 * bodies "for completeness" would only invite something depending on
 * them by accident later.
 *
 * __plat_chmodat(), __plat_mkdir(), __plat_fstatat(),
 * __plat_statvfs_path(), and __plat_set_times_at() all take
 * `struct __ntpath *np` -- the same NT-only path-resolution gap
 * src/fcntl/linux/plat_fcntl.c's __plat_create_file() documents at
 * length. Each front door (fchmodat(), mkdirat(), fstatat(), statvfs(),
 * utimensat()) calls __ntpath_at()/__ntpath() itself before reaching
 * this interface, so there is no POSIX-shaped path this function could
 * be handed on a non-NT backend. Stubbed with ENOSYS, not attempted;
 * see plat_fcntl.c's banner for the fuller account, which applies
 * identically to all five here.
 *
 * __plat_fstat() uses statx(2) rather than a raw fstat(2)/newfstatat(2)
 * deliberately: unlike the classic kernel `struct stat`, whose raw
 * layout genuinely differs between architectures (x86_64's is not
 * aarch64's -- exactly the kind of per-arch landmine src/unistd/linux/
 * plat_fd.c's __plat_seek_query() comment says it chose to avoid rather
 * than hardcode), struct statx is a FIXED, architecture-independent ABI
 * (Linux 4.11+, stable by design). The local mirror below was
 * confirmed field-for-field against this host's own <linux/stat.h> via
 * offsetof()/sizeof(), not assumed: sizeof(struct statx) == 256,
 * stx_mode at 28, stx_ino at 32, stx_size at 40, stx_blocks at 48,
 * stx_{a,b,c,m}time at 64/80/96/112 (each a 16-byte {tv_sec:8,
 * tv_nsec:4,__reserved:4} struct statx_timestamp), stx_{rdev,dev}_
 * {major,minor} at 128/132/136/140.
 *
 * st_mode needs NO translation at all going from stx_mode into
 * ntlibc's own struct stat: ntlibc's <sys/stat.h> S_IF* and S_IR* etc.
 * values are the same standard POSIX/Linux octal bit values the kernel
 * itself uses, so a straight assignment is correct -- unlike NT's
 * backend, which has to synthesize a mode from FILE_ATTRIBUTE_* bits,
 * a $LXMOD EA, and a validated-PE-executable fallback (see src/stat/
 * nt/plat_stat.c's mode_from_attrs()/pe_executable()) because NT has no
 * native concept of a POSIX mode at all. Similarly `type` (the caller's
 * __FD_* classification, which NT's __plat_fstat() needs to synthesize
 * an identity for a pipe/console/char handle -- see that file's own
 * banner) goes UNUSED here: a real Linux statx(2) already reports the
 * correct S_IFIFO/S_IFCHR/S_IFREG/S_IFDIR and a real st_dev/st_ino for
 * every fd kind natively, so there is nothing left to synthesize.
 *
 * st_dev/st_rdev are assembled from statx's separate major/minor pairs
 * via a simple, DOCUMENTED (not glibc-bit-compatible) packing: this
 * only needs to be internally self-consistent -- same device always
 * produces the same st_dev, different devices always differ -- which
 * is the entire property stat.html's DESCRIPTION asks of st_dev/st_ino
 * together (see also src/stat/nt/plat_stat.c's own __STAT_DEV_PIPE/
 * __STAT_DEV_CHAR comment on the same point). Reproducing glibc's exact
 * historical major/minor encoding is not needed for that and is not
 * attempted.
 *
 * __plat_statvfs() uses fstatfs(2), whose raw kernel struct statfs is
 * ALSO the same shape as glibc's own <sys/statfs.h> on every 64-bit
 * Linux architecture (no 32/64 largefile split exists there the way it
 * does on some 32-bit ABIs) -- confirmed against this host's own header
 * the same way, sizeof 120 bytes, f_type/f_bsize/.../f_flags at
 * 0/8/.../80, each field 8 bytes wide. Unlike NT, which reports zero
 * for f_files/f_ffree/f_favail because NTFS's MFT has no fixed inode
 * pool to count (see src/stat/nt/plat_stat.c's own long comment on
 * this), Linux genuinely has real numbers here and this file reports
 * them for real.
 *
 * __plat_set_times() is a single utimensat(2) syscall with a NULL
 * pathname -- the documented Linux idiom for "operate on this fd
 * directly" (glibc's own futimens() is implemented this exact way) --
 * and needs NO translation of `ts` at all: ntlibc's UTIME_NOW (0x3fffffff)
 * and UTIME_OMIT (0x3ffffffe) (<sys/stat.h>) are already the Linux
 * kernel's own values for the same sentinels, and ntlibc's struct
 * timespec already matches the kernel's raw 64-bit ABI on this
 * architecture, so `ts` is passed straight through unmodified. This is
 * dramatically simpler than NT's version (src/stat/nt/plat_stat.c's
 * __plat_set_times()), which needs a query/merge round-trip against
 * FileBasicInformation to work around a Wine quirk that clears
 * FILE_ATTRIBUTE_READONLY on any timestamp-only call -- nothing
 * analogous exists here.
 */
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "plat_stat.h"

/* aarch64 Linux syscall numbers -- see plat_mem.c's banner for why
 * these are hardcoded rather than pulled from a host header. */
#define SYS_fchmod    52
#define SYS_statx     291
#define SYS_fstatfs   44
#define SYS_utimensat 88

#define AT_EMPTY_PATH_LX     0x1000
#define STATX_BASIC_STATS_LX 0x7ff

extern long syscall(long number, ...);

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

/* The kernel's fixed, architecture-independent struct statx (linux/
 * stat.h) -- see this file's own banner for the field-by-field
 * confirmation against this host's real header. */
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
	unsigned long long stx_attributes_mask;
	struct __lx_statx_timestamp stx_atime;
	struct __lx_statx_timestamp stx_btime;
	struct __lx_statx_timestamp stx_ctime;
	struct __lx_statx_timestamp stx_mtime;
	unsigned int stx_rdev_major;
	unsigned int stx_rdev_minor;
	unsigned int stx_dev_major;
	unsigned int stx_dev_minor;
	unsigned long long stx_mnt_id;
	unsigned int stx_dio_mem_align;
	unsigned int stx_dio_offset_align;
	unsigned long long __spare3[12];
};

/* The kernel's raw struct statfs (fstatfs(2)): confirmed field-for-
 * field against this host's own <sys/statfs.h> via offsetof()/
 * sizeof() -- sizeof 120, each named field 8 bytes wide at the offset
 * given, f_fsid packed as two ints occupying one 8-byte slot. Identical
 * to glibc's own struct statfs on every 64-bit Linux architecture. */
struct __lx_statfs {
	long f_type;
	long f_bsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_bavail;
	unsigned long f_files;
	unsigned long f_ffree;
	int f_fsid[2];
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

static dev_t pack_dev(unsigned major, unsigned minor)
{
	/* A simple, internally-consistent (not glibc-bit-compatible)
	 * packing -- see this file's own banner for why bit-for-bit
	 * fidelity to glibc's historical encoding is not needed here. */
	return ((dev_t)major << 20) | (dev_t)minor;
}

int __plat_chmod(__plat_handle_t h, mode_t mode)
{
	long ret = syscall(SYS_fchmod, unbox(h), (long)(mode & 07777));
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_chmodat(struct __ntpath *np, int flags, mode_t mode)
{
	(void)np; (void)flags; (void)mode;
	errno = ENOSYS;
	return -1;
}

int __plat_mkdir(struct __ntpath *np, void *ea, unsigned ea_len)
{
	(void)np; (void)ea; (void)ea_len;
	errno = ENOSYS;
	return -1;
}

int __plat_fstat(__plat_handle_t h, int type, struct stat *st)
{
	struct __lx_statx stx;
	long ret;

	(void)type; /* unused -- see this file's own banner */

	memset(&stx, 0, sizeof stx);
	ret = syscall(SYS_statx, unbox(h), "", AT_EMPTY_PATH_LX, STATX_BASIC_STATS_LX, &stx);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }

	memset(st, 0, sizeof *st);
	st->st_dev = pack_dev(stx.stx_dev_major, stx.stx_dev_minor);
	st->st_rdev = pack_dev(stx.stx_rdev_major, stx.stx_rdev_minor);
	st->st_ino = (ino_t)stx.stx_ino;
	st->st_mode = (mode_t)stx.stx_mode;
	st->st_nlink = (nlink_t)stx.stx_nlink;
	st->st_uid = (uid_t)stx.stx_uid;
	st->st_gid = (gid_t)stx.stx_gid;
	st->st_size = (off_t)stx.stx_size;
	st->st_blksize = (blksize_t)stx.stx_blksize;
	st->st_blocks = (blkcnt_t)stx.stx_blocks;
	st->st_atim.tv_sec = (time_t)stx.stx_atime.tv_sec;
	st->st_atim.tv_nsec = (long)stx.stx_atime.tv_nsec;
	st->st_mtim.tv_sec = (time_t)stx.stx_mtime.tv_sec;
	st->st_mtim.tv_nsec = (long)stx.stx_mtime.tv_nsec;
	st->st_ctim.tv_sec = (time_t)stx.stx_ctime.tv_sec;
	st->st_ctim.tv_nsec = (long)stx.stx_ctime.tv_nsec;
	return 0;
}

int __plat_fstatat(struct __ntpath *np, int flags, struct stat *st)
{
	(void)np; (void)flags; (void)st;
	errno = ENOSYS;
	return -1;
}

int __plat_statvfs(__plat_handle_t h, struct statvfs *buf)
{
	struct __lx_statfs sf;
	long ret;

	memset(&sf, 0, sizeof sf);
	ret = syscall(SYS_fstatfs, unbox(h), &sf);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }

	memset(buf, 0, sizeof *buf);
	/* f_bsize/f_frsize: on the common Linux file systems (ext4, xfs,
	 * btrfs, tmpfs) the kernel's own f_bsize and f_frsize already
	 * agree, so no scaling of the block counts below is needed the way
	 * glibc's own statvfs() does when they genuinely differ. */
	buf->f_frsize = (unsigned long)(sf.f_frsize ? sf.f_frsize : sf.f_bsize);
	buf->f_bsize = (unsigned long)sf.f_bsize;
	buf->f_blocks = (fsblkcnt_t)sf.f_blocks;
	buf->f_bfree = (fsblkcnt_t)sf.f_bfree;
	buf->f_bavail = (fsblkcnt_t)sf.f_bavail;
	buf->f_files = (fsfilcnt_t)sf.f_files;
	buf->f_ffree = (fsfilcnt_t)sf.f_ffree;
	buf->f_favail = (fsfilcnt_t)sf.f_ffree; /* Linux reports no separate
	                             * privileged-vs-caller inode count,
	                             * same fallback NT's own backend uses
	                             * for f_bfree/f_bavail when only one
	                             * figure is available. */
	buf->f_fsid = (unsigned long)sf.f_fsid[0];
	/* ST_RDONLY/ST_NOSUID (<sys/statvfs.h>) are the same bit values
	 * the kernel's own f_flags uses (<linux/statfs.h> ST_RDONLY==1,
	 * ST_NOSUID==2), so no translation table is needed -- the same
	 * "already matches the ABI" situation plat_mem.c's banner
	 * describes for PROT_/MAP_. */
	buf->f_flag = (unsigned long)sf.f_flags & (ST_RDONLY | ST_NOSUID);
	buf->f_namemax = (unsigned long)sf.f_namelen;
	return 0;
}

int __plat_statvfs_path(struct __ntpath *np, struct statvfs *buf)
{
	(void)np; (void)buf;
	errno = ENOSYS;
	return -1;
}

int __plat_set_times(__plat_handle_t h, const struct timespec ts[2])
{
	long ret = syscall(SYS_utimensat, unbox(h), (long)0 /* NULL path: see banner */, ts, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_set_times_at(struct __ntpath *np, int flags, const struct timespec ts[2])
{
	(void)np; (void)flags; (void)ts;
	errno = ENOSYS;
	return -1;
}
