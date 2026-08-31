/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_unistd.h -- see src/mman/
 * linux/plat_mem.c's own banner for the general discipline this file
 * follows too (raw syscall(2), no host libc, -nostdinc against ntlibc's
 * own headers, aarch64 syscall numbers confirmed against this host's own
 * <sys/syscall.h> rather than assumed).
 *
 * UNLIKE plat_fcntl.h's __plat_create_file(), which takes a `struct
 * __ntpath *` the front door (src/fcntl/open.c) has already resolved
 * through NT-only machinery -- an interface shape plat_fcntl.h's own
 * banner says "a non-NT backend will need an entirely different front
 * door" for -- every path-taking function plat_unistd.h declares
 * (__plat_unlink, __plat_chdir, __plat_link, __plat_readlink,
 * __plat_symlink, __plat_chown_probe) takes a plain `const char *path`
 * and, where relevant, a plain `int dirfd`.  Checked against every one
 * of their front doors (src/unistd/{unlink,chdir,link,ids}.c): none of
 * them calls __ntpath_at()/__ntpath() themselves -- that resolution
 * happens only inside the NT backend's own __plat_unlink()/__plat_link()/
 * etc. bodies (src/unistd/nt/plat_unistd.c).
 *
 * __plat_chdir() and (as of the same fix open()'s own front door got,
 * src/fcntl/open.c) __plat_readlink() are the two exceptions to "none of
 * them calls anything NT-only": their front doors (chdir.c, link.c's
 * readlinkat()) used to call __vfs_resolve_at() (src/internal/vfs.c)
 * directly -- not __ntpath_at(), but still the same fixed-POSIX-
 * namespace overlay machinery NT needs because it has no native concept
 * of `/`, `/dev`, `/dev/null` etc, and a future UEFI backend most likely
 * will too. That call moved into the NT backend's own __plat_chdir()/
 * __plat_readlink() bodies for the identical reason __plat_open() itself
 * absorbed __vfs_resolve_at() -- and Linux, unlike NT or a hypothetical
 * UEFI backend, has real native devices and a real native root, so it
 * needs no overlay and no equivalent call at all: __plat_chdir() below
 * always reports __VFS_NONE via its *vfsout parameter (the plat_unistd.h
 * contract for a backend with nothing to report there), and
 * __plat_readlink() needs no vfs pre-check whatsoever -- Linux's own
 * readlinkat(2) already answers ENOENT/EINVAL correctly on its own.
 *
 * So this whole family ports directly onto Linux's own *at() syscalls,
 * which take the identical (dirfd, path) shape POSIX already gives
 * them -- no second front door needed, unlike open().
 *
 * `dirfd` here may be ntlibc's own AT_FDCWD sentinel or an ntlibc fd-
 * table index (an int the POSIX front door received directly, e.g.
 * unlinkat()'s caller-supplied dirfd) -- never a raw Linux fd on its own.
 * resolve_dirfd() below turns either into what the raw *at() syscalls
 * need: AT_FDCWD passed straight through (ntlibc's own <fcntl.h> already
 * defines it as -100, confirmed against this host's own <fcntl.h> to be
 * numerically identical to Linux's, so no translation is needed for that
 * case), or the fd table's boxed handle (src/unistd/linux/plat_fd.c's
 * fd+1 encoding) unboxed back into the real fd it names.
 *
 * ntlibc's own <fcntl.h> AT_FDCWD/AT_SYMLINK_NOFOLLOW/AT_REMOVEDIR/
 * AT_SYMLINK_FOLLOW/O_CLOEXEC values were checked against this host's
 * real <fcntl.h> (-100, 0x100, 0x200, 0x400, 0x80000 respectively) and
 * found numerically identical to the kernel ABI's own, unlike NT's flags
 * (which the NT backend translates from scratch): this header's own
 * POSIX flag namespace was already chosen to match Linux, so they are
 * used directly below rather than reintroduced under an _LX suffix the
 * way src/unistd/linux/plat_fd.c's fcntl(2)-command constants (F_SETFD,
 * FD_CLOEXEC -- values <fcntl.h> does not define at all) had to be.
 *
 * SCOPED OUT, deliberately: __plat_alarm_arm()'s SIGALRM/timer
 * machinery.  alarm()'s real semantics need a raw signal handler wired
 * through this process's signal-delivery machinery
 * (src/signal/sigdelivery.c's __raise_internal(), which a parallel,
 * separately-owned migration session is porting) -- reimplementing that
 * here risks exactly the duplicate-__plat_* collision the project's own
 * NT-migration history already hit twice.  __plat_alarm_arm() below
 * always returns -1 ("could not arm"), which is the exact degraded mode
 * sleep.c's own alarm() already tolerates ("There is nothing to report a
 * failed arm with, so a request the system silently could not honour
 * just leaves alarm_due at 0, same as the cancelled case") -- alarm()
 * itself, and every other function in this file, is otherwise fully
 * implemented, including __plat_time_now(), the same realtime clock
 * alarm()'s deadline math runs on.
 *
 * getpid()/gettid() are ALSO real here now, via __plat_getpid()/
 * __plat_gettid() below (plat_unistd.h gained both): they used to read
 * NT's TEB directly in src/unistd/getpid.c's own front door, never
 * going through plat_unistd.h at all -- a real gap noted when this file
 * was first written, closed once pthread_mutex.c's own port needed a
 * working getpid() to be reachable on this backend at all (see the
 * pthread front-door work's own commit for the fuller account).
 */
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>   /* syscall()'s own public prototype (-Wmissing-prototypes) */
#include "libc.h"
#include "plat_unistd.h"

/* aarch64 Linux syscall numbers (arch/arm64/include/uapi/asm/unistd.h,
 * via the generic modern ABI's asm-generic/unistd.h) -- confirmed
 * against this host's own <sys/syscall.h>, the same oracle src/mman/
 * linux/plat_mem.c's banner describes, rather than assumed. */
#define SYS_chdir              49
#define SYS_unlinkat           35
#define SYS_linkat             37
#define SYS_readlinkat         78
#define SYS_symlinkat          36
#define SYS_newfstatat         79
#define SYS_ftruncate          46
#define SYS_fsync              82
#define SYS_pipe2              59
#define SYS_getppid           173
#define SYS_getpid            172
#define SYS_gettid            178
#define SYS_getuid            174
#define SYS_clock_gettime     113
#define SYS_sched_getaffinity 123
#define SYS_sysinfo           179
#define SYS_kill              129
#define SYS_setpgid           154
#define SYS_getpgid           155

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all. NOT `extern long syscall(long, ...)`: that
 * symbol is satisfied by the HOST's real glibc at link time in a
 * non-freestanding build (this file's own -nostdinc only avoids the
 * host's headers, not its final link step), and glibc's syscall()
 * performs its own error translation: on failure it always returns
 * exactly -1 and sets glibc's OWN errno (a different memory location
 * than ntlibc's own errno global, src/internal/errno.c), never the
 * raw kernel -errno in [-4095,-1] this file's is_sys_error()/
 * `errno = (int)-ret` translation requires -- and, worse than a
 * merely-wrong errno, __plat_process_exists() below reads `-ret`
 * directly and compares it to EPERM to distinguish "exists, not
 * mine to signal" from "does not exist": under the glibc-wrapped
 * syscall(), every kill(2) failure collapses to ret==-1, so
 * `-ret==EPERM` would be true unconditionally and this function
 * would report every nonexistent pid as existing. Confirmed both by
 * inspecting a linked pilot binary (nm -D shows an undefined
 * `syscall@GLIBC_*`) and independently by five sibling Linux backends
 * (src/mman/linux/plat_mem.c, src/unistd/linux/plat_fd.c,
 * src/socket/linux/plat_socket.c, src/time/linux/plat_time.c,
 * src/process/linux/plat_process.c) and src/thread/linux/plat_thread.c,
 * each of which independently hit and fixed the identical bug; this
 * is the same fix applied here. aarch64's syscall calling convention:
 * x8 = syscall number, x0..x5 = up to 6 arguments, result (or -errno
 * in [-4095,-1]) in x0. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
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

/* A raw Linux syscall returns the result on success, or -errno (an
 * unsigned value in [-4095, -1]) on failure -- see plat_mem.c's banner
 * for the full statement of this convention. */
static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

/* syscall(): include/unistd.h's own declaration is marked "undefined-ok:
 * NT has no stable, numbered raw-syscall ABI exposed to user mode" --
 * true of NT, not of Linux, which has had exactly that ABI (a fixed
 * per-architecture syscall number in x8, up to six arguments in x0..x5,
 * `svc #0`, the result or -errno in x0) as public, stable interface
 * since long before this pilot existed. So unlike the rest of this file,
 * which exists to satisfy plat_unistd.h's own __plat_*() seam, this is
 * the plain POSIX front door itself: every argument this function's own
 * caller supplied is unknown in count (that is what "..." means), so all
 * six slots raw_syscall() always takes are read regardless of how many
 * the caller actually passed -- exactly the same unconditional six-slot
 * va_arg() extraction src/signal/linux/plat_signal.c's own file-local
 * `syscall()` trampoline already does for the identical reason (that
 * one is static and unrelated to this one beyond sharing a name and a
 * technique -- two independent files independently discovering they
 * both need a raw `svc #0` wrapper is the whole reason every Linux
 * backend in this tree defines its own rather than sharing one, per
 * src/mman/linux/plat_mem.c's own banner). A kernel syscall that takes
 * fewer than six arguments simply ignores the extra ones, so handing it
 * uninitialized register/stack content past what the caller actually
 * supplied is harmless in practice on this ABI, if formally
 * unspecified-but-not-undefined C (reading a va_arg the caller never
 * provided) -- exactly the same tradeoff already accepted at the sibling
 * call site.
 *
 * syscall(2)'s own contract for the return value: the raw kernel result
 * on success, or errno set plus -1 on failure -- is_sys_error()/
 * `errno = (int)-ret` above is that exact translation, already proven
 * correct by every other function in this file. */
long syscall(long number, ...)
{
	va_list ap;
	long a1, a2, a3, a4, a5, a6, ret;

	va_start(ap, number);
	a1 = va_arg(ap, long);
	a2 = va_arg(ap, long);
	a3 = va_arg(ap, long);
	a4 = va_arg(ap, long);
	a5 = va_arg(ap, long);
	a6 = va_arg(ap, long);
	va_end(ap);

	ret = raw_syscall(number, a1, a2, a3, a4, a5, a6);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return ret;
}

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

/* See this file's own banner: turns ntlibc's own AT_FDCWD sentinel or
 * fd-table index into what the raw *at() syscalls need.  Returns -1 with
 * errno already set (by __fd_get()) only on a bad table index -- never a
 * legitimate result otherwise, since AT_FDCWD is -100 and every unboxed
 * real fd is >= 0 (plat_fd.c's fd+1 encoding never boxes a value <= 0). */
static int resolve_dirfd(int dirfd)
{
	struct __fd *f;
	if (dirfd == AT_FDCWD) return AT_FDCWD;
	f = __fd_get(dirfd);
	if (!f) return -1;
	return unbox(f->h);
}

/* ======================================================================
 * sleep.c: the realtime clock alarm()/__alertable_delay() share, and the
 * (deliberately unimplemented -- see this file's banner) alarm timer.
 * ====================================================================== */

long long __plat_time_now(void)
{
	/* Raw kernel struct timespec: two `long`s on every 64-bit Linux
	 * ABI (tv_sec, tv_nsec), unlike struct stat/sysinfo there is no
	 * historical 32-bit padding tail to worry about. */
	struct { long tv_sec; long tv_nsec; } ts = {0, 0};
	long long nt;
	long ret = raw_syscall(SYS_clock_gettime, 0L /* CLOCK_REALTIME */, (long)&ts, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) return __TICKS_1601_TO_1970; /* the epoch: never actually fails on Linux */
	/* __unix_to_nt() (src/internal/libc.h) is the same NT-epoch/100ns-
	 * tick conversion src/time/clock_gettime.c and every other clock
	 * reader in this library already shares -- reused rather than
	 * reinvented so this backend's answer cannot drift from theirs. */
	if (!__unix_to_nt(ts.tv_sec, ts.tv_nsec, &nt)) return __TICKS_1601_TO_1970;
	return nt;
}

int __plat_alarm_arm(long long due, unsigned long seq, __plat_alarm_fn deliver)
{
	(void)due; (void)seq; (void)deliver;
	return -1; /* see this file's banner: SIGALRM/timer machinery, deliberately out of scope */
}

void __plat_alarm_cancel(void)
{
	/* Nothing was ever armed (see __plat_alarm_arm() above); alarm.html
	 * gives this no error to report one with regardless. */
}

void __plat_alarm_reset_after_fork(void)
{
	/* Nothing to reset: __plat_alarm_arm() never creates a timer. */
}

/* ======================================================================
 * getpid.c: getpid()/gettid() are real here now (see plat_unistd.h's
 * own updated banner) -- neither can fail on Linux any more than
 * getppid(2) below can, so neither checks is_sys_error() at all.
 * ====================================================================== */

pid_t __plat_getpid(void)
{
	return (pid_t)raw_syscall(SYS_getpid, 0L, 0L, 0L, 0L, 0L, 0L);
}

pid_t __plat_gettid(void)
{
	return (pid_t)raw_syscall(SYS_gettid, 0L, 0L, 0L, 0L, 0L, 0L);
}

pid_t __plat_getppid(void)
{
	long ret = raw_syscall(SYS_getppid, 0L, 0L, 0L, 0L, 0L, 0L);
	/* getppid(2) cannot fail on Linux (getppid.html reserves no error
	 * return either); init's conventional pid is the only sane
	 * fallback if it somehow did. */
	return is_sys_error(ret) ? (pid_t)1 : (pid_t)ret;
}

/* ======================================================================
 * ftruncate.c
 * ====================================================================== */

int __plat_ftruncate(__plat_handle_t h, off_t len)
{
	long ret = raw_syscall(SYS_ftruncate, (long)unbox(h), (long)len, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* ======================================================================
 * fsync.c
 * ====================================================================== */

int __plat_fsync(__plat_handle_t h)
{
	long ret = raw_syscall(SYS_fsync, (long)unbox(h), 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* ======================================================================
 * pipe.c
 * ====================================================================== */

int __plat_pipe(__plat_handle_t *rp, __plat_handle_t *wp, int inheritable)
{
	int fds[2] = {-1, -1};
	long ret = raw_syscall(SYS_pipe2, (long)fds, (long)(inheritable ? 0 : O_CLOEXEC), 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*rp = (__plat_handle_t)(long)(fds[0] + 1);
	*wp = (__plat_handle_t)(long)(fds[1] + 1);
	return 0;
}

/* ======================================================================
 * sysconf.c
 * ====================================================================== */

long __plat_nprocessors(void)
{
	/* sched_getaffinity(2)'s return is the number of bytes of mask it
	 * actually wrote (the kernel's own cpumask_t size), which may be
	 * less than the buffer offered; counting set bits over exactly
	 * that many bytes, rather than the whole buffer, is what makes
	 * this correct for any core count the buffer is large enough to
	 * hold. */
	unsigned char mask[128];
	long ret, i, count = 0;
	for (i = 0; i < (long)sizeof mask; i++) mask[i] = 0;
	ret = raw_syscall(SYS_sched_getaffinity, 0L, (long)sizeof mask, (long)mask, 0L, 0L, 0L);
	if (is_sys_error(ret) || ret <= 0) return 1;
	for (i = 0; i < ret; i++) {
		unsigned char b = mask[i];
		while (b) { count += (b & 1); b = (unsigned char)(b >> 1); }
	}
	return count ? count : 1;
}

long __plat_phys_pages(void)
{
	/* Raw uapi struct sysinfo layout on a 64-bit Linux kernel,
	 * confirmed against this host's own <sys/sysinfo.h>: sizeof 112,
	 * offsetof(totalram)==32 (8 bytes), offsetof(mem_unit)==104 (4
	 * bytes).  Read by fixed byte offset out of a plain buffer, little-
	 * endian (true of every architecture this library currently
	 * targets), rather than through a locally-declared struct: this
	 * file's -nostdinc build has no host header to check a hand-written
	 * struct's compiler-computed layout against, and struct sysinfo's
	 * historical `char _f[]` size-padding trailer (zero bytes wide on
	 * every 64-bit kernel, nonzero on 32-bit ones) is exactly the kind
	 * of detail that would silently drift between what this file
	 * assumes and what the kernel actually writes. */
	unsigned char raw[128];
	unsigned long long totalram;
	unsigned int mem_unit;
	long ret, i;
	for (i = 0; i < (long)sizeof raw; i++) raw[i] = 0;
	ret = raw_syscall(SYS_sysinfo, (long)raw, 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) return -1;
	totalram = 0;
	for (i = 7; i >= 0; i--) totalram = (totalram << 8) | raw[32 + i];
	mem_unit = 0;
	for (i = 3; i >= 0; i--) mem_unit = (mem_unit << 8) | raw[104 + i];
	if (!mem_unit) mem_unit = 1;
	return (long)((totalram * mem_unit) / 4096);
}

/* ======================================================================
 * unlink.c
 * ====================================================================== */

int __plat_unlink(int dirfd, const char *path, int isdir)
{
	int rd = resolve_dirfd(dirfd);
	long ret;
	if (rd == -1 && dirfd != AT_FDCWD) return -1; /* errno already set */
	ret = raw_syscall(SYS_unlinkat, (long)rd, (long)path, (long)(isdir ? AT_REMOVEDIR : 0), 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* ======================================================================
 * chdir.c
 * ====================================================================== */

int __plat_chdir(const char *path, int *vfsout)
{
	long ret = raw_syscall(SYS_chdir, (long)path, 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	/* No overlay on this backend at all -- see this file's own banner --
	 * so nothing to report beyond the plat_unistd.h contract's own
	 * "__VFS_NONE, always" for a backend with no overlay concept. */
	*vfsout = __VFS_NONE;
	return 0;
}

/* ======================================================================
 * link.c
 * ====================================================================== */

int __plat_link(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int followsym)
{
	int rold = resolve_dirfd(olddirfd);
	int rnew;
	long ret;
	if (rold == -1 && olddirfd != AT_FDCWD) return -1;
	rnew = resolve_dirfd(newdirfd);
	if (rnew == -1 && newdirfd != AT_FDCWD) return -1;
	ret = raw_syscall(SYS_linkat, (long)rold, (long)oldpath, (long)rnew, (long)newpath,
	                 (long)(followsym ? AT_SYMLINK_FOLLOW : 0), 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

ssize_t __plat_readlink(int dirfd, const char *path, char *buf, size_t bufsz)
{
	int rd = resolve_dirfd(dirfd);
	long ret;
	if (rd == -1 && dirfd != AT_FDCWD) return -1;
	/* readlinkat(2)'s own truncate-silently behaviour (fill up to
	 * bufsz, return the byte count, no NUL) already IS readlink.html's
	 * contract verbatim -- unlike the NT backend, which has to build
	 * that behaviour out of a reparse-point buffer by hand, nothing
	 * here needs to special-case it. */
	ret = raw_syscall(SYS_readlinkat, (long)rd, (long)path, (long)buf, (long)bufsz, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

int __plat_symlink(const char *target, int newdirfd, const char *linkpath)
{
	int rd = resolve_dirfd(newdirfd);
	long ret;
	if (rd == -1 && newdirfd != AT_FDCWD) return -1;
	ret = raw_syscall(SYS_symlinkat, (long)target, (long)rd, (long)linkpath, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* ======================================================================
 * ids.c: Linux has real process groups and a real uid, unlike NT, so
 * this whole section is a much shorter story than src/unistd/nt/
 * plat_unistd.c's Cygwin-style SID-to-uid mapping and named-event pgrp
 * trick -- it just asks the kernel.
 * ====================================================================== */

uid_t __plat_detect_uid(void)
{
	/* getuid(2) cannot fail on Linux, and getuid.html reserves no
	 * error return either -- the raw return is always the real uid,
	 * never in is_sys_error()'s [-4095,-1] failure window. */
	return (uid_t)raw_syscall(SYS_getuid, 0L, 0L, 0L, 0L, 0L, 0L);
}

void __plat_pgrp_publish_self(pid_t self)
{
	/* The NT backend publishes a named event because NT has nothing
	 * else to hang "this process is a group leader" on; Linux actually
	 * has the process group setpgid(0,0) asks for, and that real
	 * operation supersedes the event trick entirely rather than
	 * needing to be reimplemented alongside it.  Best-effort and
	 * silent on failure, matching setpgrp.html's "no errors are
	 * defined" the front door (src/unistd/ids.c) already relies on. */
	(void)self;
	raw_syscall(SYS_setpgid, 0L, 0L, 0L, 0L, 0L, 0L);
}

int __plat_pgrp_is_leader(pid_t pid)
{
	long ret = raw_syscall(SYS_getpgid, (long)pid, 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) return 0;
	return ret == (long)pid;
}

int __plat_process_exists(pid_t pid)
{
	/* kill(pid, 0) is the standard POSIX existence probe: 0 means the
	 * process exists and is signalable, EPERM means it exists and
	 * merely is not ours to signal (the direct Linux analogue of the
	 * NT backend's STATUS_ACCESS_DENIED-counts-as-existing rule), and
	 * anything else (ESRCH first among them) means it does not. */
	long ret = raw_syscall(SYS_kill, (long)pid, 0L, 0L, 0L, 0L, 0L);
	if (!is_sys_error(ret)) return 1;
	return (int)-ret == EPERM;
}

/* There is real ownership to probe on Linux (unlike NT), but
 * __plat_chown_probe()'s contract is deliberately narrower than a real
 * chown: it only has to resolve `path` and report whether it exists,
 * honoring AT_SYMLINK_NOFOLLOW -- src/unistd/ids.c's fchownat() front
 * door reports success unconditionally otherwise, matching the NT
 * backend's "there is no ownership to change" stance (see that file's
 * banner) rather than actually chown()ing, which is a larger,
 * deliberately out-of-scope change to the front door itself. */
int __plat_chown_probe(int dirfd, const char *path, int flags)
{
	int rd = resolve_dirfd(dirfd);
	/* Oversized past the real 128-byte aarch64 struct stat (confirmed
	 * against this host's own <sys/stat.h>): the contents are never
	 * read, only whether the syscall itself succeeds, so the margin
	 * costs nothing and guards against a wider layout on some other
	 * future architecture. */
	unsigned char stbuf[256];
	long ret;
	if (rd == -1 && dirfd != AT_FDCWD) return -1;
	ret = raw_syscall(SYS_newfstatat, (long)rd, (long)path, (long)stbuf,
	                 (long)(flags & AT_SYMLINK_NOFOLLOW), 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}
