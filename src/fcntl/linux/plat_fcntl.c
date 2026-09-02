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
 * __plat_open() is implemented here, real: plat_fcntl.h hands it a raw,
 * unresolved (dirfd, path) pair (see that header's own banner), not an
 * already-NT-resolved `struct __ntpath *np`. The NT-specific resolution
 * machinery (VFS-overlay lookup, __ntpath_at(), the $LXMOD extended-
 * attribute buffer) lives in src/fcntl/nt/plat_fcntl.c's own
 * __plat_open() body instead.
 *
 * This backend's own __plat_open() needs almost none of NT's flag
 * translation: Linux's real openat(2) already takes (dirfd, path)
 * directly, and MOST of ntlibc's own O_* flag values already match the
 * Linux kernel ABI bit-for-bit (confirmed against this host's real
 * <fcntl.h> via a throwaway oracle program, not assumed: O_CREAT=0100,
 * O_EXCL=0200, O_TRUNC=01000, O_APPEND=02000, O_NONBLOCK=04000,
 * O_DSYNC=010000, O_SYNC=04010000, O_CLOEXEC=02000000, O_PATH=010000000).
 *
 * THREE do not, discovered only by actually running open(O_DIRECTORY)
 * against a real kernel and getting EINVAL back, not by inspection:
 * ntlibc's own <fcntl.h> has O_DIRECTORY=0200000/O_NOFOLLOW=0400000/
 * O_DIRECT=040000, but the real Linux kernel ABI (this same oracle
 * program) is O_DIRECTORY=040000/O_NOFOLLOW=0100000/O_DIRECT=0200000 --
 * O_DIRECTORY and O_DIRECT are transposed, and O_NOFOLLOW is a value
 * neither of those swaps produces. This is a real, pre-existing
 * mismatch in include/fcntl.h itself, invisible until this exact
 * moment: every consumer of these three macros anywhere else in
 * ntlibc -- the NT backend's own flag-to-CreateOptions translation,
 * every front door that only ever tests `flags & O_DIRECTORY` by
 * name -- is entirely self-referential (define and consume the same
 * value, whatever it is), so nothing before this file ever needed
 * O_DIRECTORY's bit position to equal anything outside ntlibc itself.
 * A real Linux openat(2) call is the first place that assumption gets
 * tested against an external ABI. Fixed here, not in include/fcntl.h:
 * changing a public header's flag values is a much larger-blast-radius
 * edit than translating three bits in one backend function, exactly
 * the same judgment call the NT backend already makes for its own
 * ACCESS_MASK/CreateDisposition/CreateOptions three-way split below.
 * Every OTHER already-merged Linux backend was checked (grep) for any
 * other real syscall call site touching these three macros: none
 * exists, so this is the only place the bug was reachable.
 *
 * Everything else needs no translation table, and Linux already has
 * real, native `/dev/null` etc, so the VFS-overlay machinery this
 * interface's *vfsout and *vfsnativeout report is simply never
 * invoked here -- left untouched at whatever
 * the front door (src/fcntl/open.c) already initialized them to
 * (__VFS_NONE/0), matching the header's own contract for a non-NT
 * backend. `dirfd` is resolved the same way src/unistd/linux/
 * plat_unistd.c's resolve_dirfd() already does (AT_FDCWD passed
 * straight through, an ntlibc fd-table index unboxed via the fd
 * table's own fd+1 encoding), duplicated here rather than shared
 * across translation units, matching every other Linux backend file's
 * own-syscall-table discipline.
 *
 * The one piece of real work left: Linux's openat(2), like NT's
 * NtCreateFile after its own retry-as-directory dance, allows opening
 * a directory for reading without O_DIRECTORY (POSIX-legal; only later
 * reads fail EISDIR) -- so *typeout cannot be decided from the O_
 * flags alone the way it might look like it can. A real statx(2) on
 * the freshly opened fd (the same technique src/stat/linux/
 * plat_stat.c/src/ioctl/linux/plat_ioctl.c already use) answers this
 * for real.
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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "libc.h"
#include "plat_fcntl.h"

/* Linux syscall numbers -- see plat_mem.c's banner for why these are
 * hardcoded rather than pulled from a host header; x86_64/i386 numbers
 * confirmed against arch/x86/entry/syscalls/syscall_{64,32}.tbl.
 * i386's SYS_fcntl (55, not fcntl64=221) and SYS_ftruncate (93, not
 * ftruncate64=194) are the plain 32-bit-offset syscalls -- see this
 * file's own __plat_lock_probe()/__plat_lock_set()/__plat_lock_clear()
 * banner below for the disclosed consequence. */
#if defined(__aarch64__)
#define SYS_fcntl     25
#define SYS_fallocate 47
#define SYS_ftruncate 46
#define SYS_statx     291
#define SYS_openat    56
#define SYS_close     57
#elif defined(__x86_64__)
#define SYS_fcntl     72
#define SYS_fallocate 285
#define SYS_ftruncate 77
#define SYS_statx     332
#define SYS_openat    257
#define SYS_close     3
#elif defined(__i386__)
#define SYS_fcntl     55
#define SYS_fallocate 324
#define SYS_ftruncate 93
#define SYS_statx     383
#define SYS_openat    295
#define SYS_close     6
#else
#error "plat_fcntl.c: unsupported architecture"
#endif

#define AT_EMPTY_PATH_LX     0x1000
#define STATX_BASIC_STATS_LX 0x7ff

/* File-type mode-bits, standard POSIX/Linux kernel values. */
#define S_IFMT_LX   0170000
#define S_IFSOCK_LX 0140000
#define S_IFREG_LX  0100000
#define S_IFDIR_LX  0040000
#define S_IFCHR_LX  0020000
#define S_IFIFO_LX  0010000

/* ntlibc's own O_DIRECTORY/O_NOFOLLOW/O_DIRECT values do NOT match the
 * real Linux kernel ABI -- see this file's own banner for how that was
 * discovered and why it is fixed here, not in include/fcntl.h. Every
 * other O_* flag already matches and is passed straight through. */
#define LX_O_DIRECTORY 040000
#define LX_O_NOFOLLOW  0100000
#define LX_O_DIRECT    0200000

static int to_linux_open_flags(int flags)
{
	int out = flags & ~(O_DIRECTORY | O_NOFOLLOW | O_DIRECT);
	if (flags & O_DIRECTORY) out |= LX_O_DIRECTORY;
	if (flags & O_NOFOLLOW)  out |= LX_O_NOFOLLOW;
	if (flags & O_DIRECT)    out |= LX_O_DIRECT;
	return out;
}

/* fcntl(2) lock commands and lock types: confirmed against this host's
 * own <fcntl.h> -- identical across 64-bit Linux architectures. */
#define F_GETLK_LX  5
#define F_SETLK_LX  6
#define F_SETLKW_LX 7
#define F_RDLCK_LX  0
#define F_WRLCK_LX  1
#define F_UNLCK_LX  2

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all. NOT `extern long syscall(long, ...)`: that
 * symbol is satisfied by the HOST's real glibc at link time in a
 * non-freestanding build, and glibc's syscall() performs its own
 * error translation: on failure it always returns exactly -1 and
 * sets glibc's OWN errno, never the raw kernel -errno in [-4095,-1]
 * this file's is_sys_error()/`errno = (int)-ret` translation requires
 * -- and, worse than a merely-wrong errno, __plat_fallocate() below
 * reads `-ret` directly and compares it to EOPNOTSUPP/ENOSYS to
 * decide whether to degrade to ftruncate(): under the glibc-wrapped
 * syscall(), every fallocate(2) failure collapses to ret==-1, so
 * that comparison would never match and the degradation path would
 * never trigger even when it should. See src/mman/linux/plat_mem.c's
 * fix for the fuller account of this bug, confirmed independently
 * across six other Linux backends. aarch64's syscall
 * calling convention: x8 = syscall number, x0..x5 = up to 6
 * arguments, result (or -errno in [-4095,-1]) in x0. */
#if defined(__aarch64__)
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) // NOLINT(bugprone-easily-swappable-parameters) -- raw syscall ABI slots are positional and semantically distinct
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc");
	return x0;
}
#elif defined(__x86_64__)
/* See crt/linux/crt1.c's own raw_syscall() banner for the full per-arch
 * calling-convention rationale -- duplicated here per this tree's own
 * "own syscall table per file" discipline, not shared. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	register long r9  __asm__("r9")  = a6;
	__asm__ volatile("syscall"
	                 : "=a"(ret)
	                 : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
	                 : "rcx", "r11", "memory");
	return ret;
}
#elif defined(__i386__)
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long args[7];
	long ret;
	args[0] = nr; args[1] = a1; args[2] = a2; args[3] = a3;
	args[4] = a4; args[5] = a5; args[6] = a6;
	__asm__ volatile(
		"pushl %%ebp\n\t"
		"pushl %%ebx\n\t"
		"movl 4(%%eax), %%ebx\n\t"
		"movl 8(%%eax), %%ecx\n\t"
		"movl 12(%%eax), %%edx\n\t"
		"movl 16(%%eax), %%esi\n\t"
		"movl 20(%%eax), %%edi\n\t"
		"movl 24(%%eax), %%ebp\n\t"
		"movl (%%eax), %%eax\n\t"
		"int $0x80\n\t"
		"popl %%ebx\n\t"
		"popl %%ebp"
		: "=a"(ret)
		: "a"(args)
		: "ecx", "edx", "esi", "edi", "memory", "cc");
	return ret;
}
#endif

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

/* See src/unistd/linux/plat_unistd.c's own resolve_dirfd() -- identical
 * logic, duplicated per this tree's own-syscall-table-per-file
 * discipline: turns ntlibc's own AT_FDCWD sentinel or fd-table index
 * into what the raw *at() syscalls need. Returns -1 with errno already
 * set (by __fd_get()) only on a bad table index -- never a legitimate
 * result otherwise, since AT_FDCWD is -100 and every unboxed real fd is
 * >= 0. */
static int resolve_dirfd(int dirfd)
{
	struct __fd *f;
	if (dirfd == AT_FDCWD) return AT_FDCWD;
	f = __fd_get(dirfd);
	if (!f) return -1;
	return unbox(f->h);
}

/* The kernel's raw 64-bit struct flock (fcntl(2) F_GETLK/F_SETLK/
 * F_SETLKW): confirmed field-for-field against this host's own
 * <fcntl.h> via offsetof()/sizeof() (l_type/l_whence at 0/2, l_start at
 * 8, l_len at 16, l_pid at 24, total size 32) -- identical layout on
 * every 64-bit Linux architecture, no F_GETLK64-style compat split. */
struct __lx_flock { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	short l_type;
	short l_whence;
	long l_start;
	long l_len;
	int l_pid;
};

/* The kernel's fixed, architecture-independent struct statx (linux/
 * stat.h) -- see src/stat/linux/plat_stat.c's banner for why this
 * layout, unlike the classic kernel struct stat, needs no per-
 * architecture variant, and for the fuller field-by-field confirmation
 * against this host's own header. Moved up here (rather than staying
 * next to __plat_file_extent(), its only reader before __plat_open()
 * needed one too) since __plat_open() below also needs it to decide
 * *typeout. */
struct __lx_statx_timestamp { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	long long tv_sec;
	unsigned int tv_nsec;
	int __reserved; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
};
struct __lx_statx { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	unsigned int stx_mask;
	unsigned int stx_blksize;
	unsigned long long stx_attributes;
	unsigned int stx_nlink;
	unsigned int stx_uid;
	unsigned int stx_gid;
	unsigned short stx_mode;
	unsigned short __spare0[1]; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	unsigned long long stx_ino;
	unsigned long long stx_size;
	unsigned long long stx_blocks;
	// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	unsigned long long __rest[26]; /* attributes_mask, four timestamps,
	                                * rdev/dev major/minor, mnt_id,
	                                * dio alignment, and the kernel's own
	                                * reserved tail -- 256 bytes total. */
};

int __plat_open(int dirfd, const char *path, int flags, unsigned mode,
                __plat_handle_t *out, int *typeout, int *vfsout, int *vfsnativeout) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; output slots have distinct roles
{
	int rd, fd;
	long ret;
	struct __lx_statx stx;

	/* Never touched: no VFS overlay exists on this backend at all (see
	 * this file's own banner) -- *vfsout and *vfsnativeout stay whatever
	 * the front door (src/fcntl/open.c) already initialized them to. */
	(void)vfsout; (void)vfsnativeout;

	rd = resolve_dirfd(dirfd);
	if (rd == -1 && dirfd != AT_FDCWD) return -1; /* errno already set */

	ret = raw_syscall(SYS_openat, (long)rd, (long)path, (long)to_linux_open_flags(flags), (long)mode, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	fd = (int)ret;

	/* *typeout cannot be decided from the O_ flags alone: Linux, like
	 * NT after its own retry-as-directory dance, allows opening a
	 * directory for reading without O_DIRECTORY (POSIX-legal; only
	 * later reads fail EISDIR) -- see this file's own banner. A real
	 * statx(2) on the freshly opened fd answers this for real.
	 *
	 * Unlike the NT backend, __FD_FILE (or whichever __FD_* fits) must
	 * be reported explicitly here, not left at 0: on NT, src/internal/
	 * fd.c's __fd_install_at() auto-classifies a zero typeout itself
	 * via __handle_type() (NtQueryVolumeInformationFile/
	 * NtQueryInformationFile), so __plat_create_file()'s original NT
	 * implementation never needed to name anything but __FD_DIR. This
	 * backend has no such auto-classification step behind it (every
	 * Linux pilot test harness so far reimplements a minimal fd table
	 * specifically because fd.c's __handle_type() is NT-only and
	 * unported), so leaving *typeout at 0 here would silently hand
	 * back a value nothing else resolves into __FD_FILE -- confirmed
	 * the hard way: lseek()'s own `f->type != __FD_FILE` check reads
	 * type 0 as "not seekable" and reports ESPIPE on an ordinary
	 * regular file. */
	memset(&stx, 0, sizeof stx);
	ret = raw_syscall(SYS_statx, (long)fd, (long)"", (long)AT_EMPTY_PATH_LX,
	                  (long)STATX_BASIC_STATS_LX, (long)&stx, 0L);
	if (is_sys_error(ret)) {
		int e = (int)-ret;
		raw_syscall(SYS_close, (long)fd, 0L, 0L, 0L, 0L, 0L);
		errno = e;
		return -1;
	}
	switch (stx.stx_mode & S_IFMT_LX) {
	case S_IFDIR_LX:  *typeout = __FD_DIR; break;
	case S_IFIFO_LX:  *typeout = __FD_PIPE; break;
	case S_IFCHR_LX:  *typeout = __FD_CHAR; break;
	case S_IFSOCK_LX: *typeout = __FD_SOCKET; break;
	default:          *typeout = __FD_FILE; break; /* S_IFREG, S_IFBLK, or anything else */
	}

	*out = (__plat_handle_t)(long)(fd + 1);
	return 0;
}

int __plat_lock_probe(__plat_handle_t h, long long off, long long len, int exclusive, int *conflicting) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; offset, length, and mode have distinct roles
{
	struct __lx_flock fl;
	long ret;

	fl.l_type = (short)(exclusive ? F_WRLCK_LX : F_RDLCK_LX);
	fl.l_whence = 0; /* SEEK_SET: off/len already arrive absolute -- see
	                  * src/fcntl/fcntl.c's record_lock_range(). */
	fl.l_start = (long)off;
	fl.l_len = (long)len;
	fl.l_pid = 0;

	ret = raw_syscall(SYS_fcntl, (long)unbox(h), (long)F_GETLK_LX, (long)&fl, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*conflicting = fl.l_type != F_UNLCK_LX;
	return 0;
}

int __plat_lock_set(__plat_handle_t h, long long off, long long len, int exclusive, int wait) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; offset, length, and modes have distinct roles
{
	struct __lx_flock fl;
	long ret;

	fl.l_type = (short)(exclusive ? F_WRLCK_LX : F_RDLCK_LX);
	fl.l_whence = 0;
	fl.l_start = (long)off;
	fl.l_len = (long)len;
	fl.l_pid = 0;

	ret = raw_syscall(SYS_fcntl, (long)unbox(h), (long)(wait ? F_SETLKW_LX : F_SETLK_LX), (long)&fl, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_lock_clear(__plat_handle_t h, long long off, long long len) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; offset and length have distinct roles
{
	struct __lx_flock fl;
	long ret;

	fl.l_type = F_UNLCK_LX;
	fl.l_whence = 0;
	fl.l_start = (long)off;
	fl.l_len = (long)len;
	fl.l_pid = 0;

	ret = raw_syscall(SYS_fcntl, (long)unbox(h), (long)F_SETLK_LX, (long)&fl, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

long long __plat_volume_max_file_size(__plat_handle_t h)
{
	(void)h;
	return LLONG_MAX;
}

/* struct __lx_statx/__lx_statx_timestamp: defined once, above, next to
 * __plat_open() (its first reader in this file). Only stx_size/
 * stx_blocks are read below; everything past them collapses into the
 * trailing __spare padding since nothing else is needed here. */
int __plat_file_extent(__plat_handle_t h, long long *alloc_size, long long *eof) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; allocation and EOF outputs have distinct roles
{
	struct __lx_statx stx;
	long ret;

	memset(&stx, 0, sizeof stx);
	ret = raw_syscall(SYS_statx, (long)unbox(h), (long)"", (long)AT_EMPTY_PATH_LX,
	                 (long)STATX_BASIC_STATS_LX, (long)&stx, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*alloc_size = (long long)stx.stx_blocks * 512; /* stx_blocks is
	                             * always counted in 512-byte units, the
	                             * same convention struct stat's
	                             * st_blocks uses. */
	*eof = (long long)stx.stx_size;
	return 0;
}

int __plat_fallocate(__plat_handle_t h, long long want, long long eof, int grow_alloc) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; requested extent and EOF have distinct roles
{
	int fd = unbox(h);
	long ret;
	(void)grow_alloc; (void)eof;

	ret = raw_syscall(SYS_fallocate, (long)fd, 0L, 0L, (long)want, 0L, 0L);
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
			ret = raw_syscall(SYS_ftruncate, (long)fd, (long)want, 0L, 0L, 0L, 0L);
			if (!is_sys_error(ret)) return 0;
		} else {
			return 0; /* nothing to grow; the weaker guarantee is moot */
		}
	}
	return (int)-ret;
}

// NOLINTEND(misc-include-cleaner)
