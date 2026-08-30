/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test-harness scaffolding for the Linux filesystem-subsystem pilot --
 * NOT part of ntlibc, exactly like fuzz/linux_pilot_harness.c (the
 * mman/unistd fd-ops pilot's own harness) and fuzz/ntstubs.c are "not
 * part of ntlibc" for their respective native builds. A separate file
 * from linux_pilot_harness.c, not a reuse of it, because it backs a
 * separate binary (linux_pilot_test_fs) with its own link line -- see
 * tools/linux-build-fs.sh.
 *
 * The fd table (__fds[]/__fd_limit/__fd_alloc/__fd_install/
 * __fd_install_at/__fd_get) is reimplemented here for the identical
 * reason linux_pilot_harness.c's own banner gives for its copy: linking
 * the real src/internal/fd.c would still require satisfying NT-only
 * syscalls this pilot has no Linux backend for at all (fd.c is core
 * NT/RTL bookkeeping, never migrated behind a plat_*.h interface, and
 * explicitly out of scope for either pilot).
 *
 * Beyond the fd table, this file supplies six more stand-ins the real
 * front-door files this pilot links (src/fcntl/fcntl.c, fadvise.c;
 * src/stat/chmod.c, stat.c) reference from within functions this test
 * DOES call, even though the reachable code path here never actually
 * takes the branch that uses them (the same "the compiler cannot
 * statically prove the branch dead, so the linker still needs a real
 * symbol" situation linux_pilot_harness.c's own banner describes for
 * fd.c's __handle_type()):
 *
 *   - syscall(): every plat_*.c backend in this pilot (both the new
 *     filesystem ones and the original mman/unistd pilot's) is written
 *     against `extern long syscall(long number, ...);` and interprets
 *     its result via the RAW Linux kernel ABI convention -- a
 *     successful call's own return value, or -errno (unsigned,
 *     [-4095,-1]) on failure -- exactly as plat_mem.c's own
 *     is_sys_error()/`errno = (int)-ret` banner describes, and exactly
 *     what a real production ntlibc would get from its OWN raw syscall
 *     trampoline once one exists (the banner's own words: "both are
 *     declared/defined locally below" -- implying a real one, not a
 *     borrowed one). This pilot, like the original one, currently
 *     borrows glibc's OWN exported `syscall()` symbol as a stand-in
 *     for that trampoline rather than writing inline assembly -- and
 *     that substitution is NOT behaviourally transparent: glibc's
 *     syscall() is documented (man 2 syscall) to translate the raw
 *     kernel convention into the ordinary C library one itself --
 *     returning -1 and setting the global `errno` on failure, not the
 *     raw -errno value the wrapped syscall actually returned. Confirmed
 *     directly against this host (a tiny C program calling
 *     `syscall(SYS_openat, ..., "/nonexistent")` prints `raw return =
 *     -1, errno = 2`, not `raw return = -2`).
 *
 *     Every plat_*.c file's `errno = (int)-ret` on the error path is
 *     therefore silently wrong when built against glibc's syscall():
 *     glibc has ALREADY set the correct errno and returned bare -1, and
 *     `(int)-(-1)` is always 1 (EPERM) -- clobbering whatever the real
 *     error was with a constant, wrong one, on every single failure,
 *     regardless of which syscall or which real error occurred. This
 *     is invisible to a test that only checks a success path (which is
 *     everything the original mman/unistd pilot's own fuzz/
 *     linux_pilot_test.c checks -- every CHECK() there is a success
 *     condition), and it stayed invisible in early runs of this
 *     filesystem pilot too, for the same reason -- until a fork()-based
 *     cross-process fcntl()/flock() conflict test (this file's
 *     fuzz/linux_pilot_test_fs.c) needed a SPECIFIC errno (EAGAIN) on a
 *     deliberately-triggered failure to prove the lock was real, and
 *     every conflict came back reported as EPERM instead. strace -f
 *     confirmed the kernel itself was already returning the correct
 *     `-1 EAGAIN` at the syscall boundary in both cases -- the bug is
 *     entirely in this substitution layer, not in the plat_*.c
 *     implementations' logic, which is correct for the raw-ABI
 *     trampoline they are actually written against.
 *
 *     Fixed HERE, in the harness, not in any plat_*.c file: this
 *     defines a REAL `syscall()` (aarch64 raw `svc #0`, arguments in
 *     x0-x5, number in x8, unmodified raw return in x0) that gives
 *     every already-correct plat_*.c file the exact ABI it was written
 *     for, and statically shadows glibc's translating symbol of the
 *     same name the same way this file's own fcntl()/flock() calls
 *     already resolve to THIS link's ntlibc definitions rather than
 *     glibc's (a directly-linked object's symbol takes priority over a
 *     shared library's same-named export). This is aarch64-only, like
 *     every syscall number in this pilot; a future architecture needs
 *     its own asm here, not just its own SYS_* numbers.
 */
#include <stdarg.h>

long syscall(long number, ...)
{
	va_list ap;
	long a1, a2, a3, a4, a5, a6;
	register long x0 __asm__("x0");
	register long x1 __asm__("x1");
	register long x2 __asm__("x2");
	register long x3 __asm__("x3");
	register long x4 __asm__("x4");
	register long x5 __asm__("x5");
	register long x8 __asm__("x8");

	va_start(ap, number);
	a1 = va_arg(ap, long);
	a2 = va_arg(ap, long);
	a3 = va_arg(ap, long);
	a4 = va_arg(ap, long);
	a5 = va_arg(ap, long);
	a6 = va_arg(ap, long);
	va_end(ap);

	x0 = a1; x1 = a2; x2 = a3; x3 = a4; x4 = a5; x5 = a6; x8 = number;
	__asm__ volatile("svc #0"
	                : "+r"(x0)
	                : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
	                : "memory", "cc");
	return x0;
}

/* The remaining stand-ins, same reasoning as above ("the compiler
 * cannot statically prove the branch dead, so the linker still needs a
 * real symbol"):
 *
 *   - getpid(): src/unistd/getpid.c's real body is
 *     `(pid_t)(ULONG_PTR)__teb()->ClientId.UniqueProcess` -- NT TEB
 *     access, meaningless outside an NT process and not something this
 *     filesystem-subsystem pilot has any business reimplementing (that
 *     is src/unistd's own territory, not migrated here). fcntl.c's
 *     record_lock() calls getpid() unconditionally at the top of every
 *     F_GETLK/F_SETLK/F_SETLKW request, so a real body is needed to
 *     link fcntl() at all. This one IS answered for real, via a raw
 *     Linux getpid(2) syscall -- not a stub -- because record_lock()'s
 *     own per-fd lock-ownership bookkeeping (src/fcntl/fcntl.c's
 *     record_locks[]) depends on the *real* owning pid to behave
 *     correctly across the fork() this test performs for its lock-
 *     conflict check below, and a fake constant would silently break
 *     that check instead of just being cosmetically wrong.
 *
 *   - __mq_fd_replaced(): src/thread/mqueue.c's POSIX-message-queue
 *     fd-remap bookkeeping, called from fcntl(F_SETFD)'s handle-remake
 *     path. This pilot exercises no mqueue descriptors, so there is
 *     nothing for it to remap -- same shape of no-op as
 *     linux_pilot_harness.c's own __mq_fd_closed().
 *
 *   - __fsize_allow(): src/misc/resource.c's RLIMIT_FSIZE gate, called
 *     unconditionally by posix_fallocate() (src/fcntl/fadvise.c).
 *     Reporting "no limit" here means the other four RLIMIT_FSIZE
 *     entry points (__fsize_limited/_clamp/_room_at/_exceeded, already
 *     stubbed the same way below) stay unreachable at runtime, exactly
 *     as linux_pilot_harness.c's own comment on those four says.
 *
 *   - __vfs_stat(): src/stat/stat.c's fstat() calls it whenever
 *     `f->vfs` is set on the descriptor (the fixed POSIX namespace
 *     layered over NT paths -- /dev, /proc-shaped synthetic entries --
 *     src/internal/libc.h's own banner on struct __fd). This pilot's
 *     fd-table entries never set `vfs`, so the call is always skipped
 *     at runtime, but fstat()'s own compiled body still references the
 *     symbol.
 *
 *   - __fd_pos_save()/__fd_pos_restore(), __vfs_resolve_at(), __ntpath():
 *     pulled in by src/unistd/{read,write}.c's pread()/pwrite() and by
 *     src/stat/statvfs.c's/stat.c's path-taking entry points
 *     (statvfs()/fstatat()/stat()/lstat()), none of which this test
 *     calls -- but GNU ld resolves every undefined reference in an
 *     object file handed to it directly (as opposed to one pulled from
 *     a .a archive by need) regardless of --gc-sections, which only
 *     trims the resulting unreferenced sections from the final image
 *     rather than exempting them from symbol resolution. Same shape of
 *     stub as linux_pilot_harness.c's own __fd_pos_save()/
 *     __fd_pos_restore() (there, for the identical reason: an NT-only
 *     quirk workaround, meaningless on Linux, that was never brought
 *     into the platform-abstraction interface at all).
 *
 *   - __handle_path(), __ntpath_at(), __ntpath_free(), free(): all four
 *     are pulled in by src/stat/chmod.c's fchmod(), which -- on an
 *     EACCES from __plat_chmod() -- falls back to reopening the file by
 *     name via fchmodat(), which resolves that name through NT-only
 *     __ntpath_at() (src/internal/path.c, explicitly out of scope for
 *     this whole migration; see src/internal/plat_fcntl.h's and
 *     plat_stat.h's own banners). This pilot's fchmod() call never hits
 *     EACCES (it chmods a file this same process just created), so the
 *     fallback is dead at runtime, but fchmod()'s compiled body still
 *     references __handle_path() directly, and the fchmodat() it would
 *     call transitively references __ntpath_at()/__ntpath_free() (and,
 *     via the retrieved path buffer, free()). __handle_path() returns
 *     NULL here (matching its own real contract's "no reopenable name"
 *     answer for an unlinked/edge-case file, which is also just true:
 *     this stub tracks no NT handle-to-path mapping at all), so
 *     fchmod()'s own `if (!path) { errno = e; return -1; }` guard is
 *     what actually executes if this path is ever hit -- __ntpath_at()/
 *     __ntpath_free()/free() are therefore never actually called, only
 *     linked; their bodies exist purely to satisfy the symbol table. As
 *     with __plat_create_file()'s stub (src/fcntl/linux/plat_fcntl.c),
 *     this is a real, out-of-scope gap being named, not a shortcut this
 *     pilot is quietly relying on for something that matters.
 */
#include <string.h>
#include <unistd.h>
#include "libc.h"

struct __fd __fds[FD_MAX];
int __fd_limit = FD_MAX;

int __fd_alloc(int lowest)
{
	int i;
	if (lowest < 0) lowest = 0;
	for (i = lowest; i < __fd_limit; i++)
		if (!__fds[i].h) return i;
	errno = EMFILE;
	return -1;
}

int __fd_install_at(int fd, HANDLE h, unsigned flags, int type)
{
	struct __fd *f = &__fds[fd];
	memset(f, 0, sizeof *f);
	f->h = h;
	f->flags = flags;
	f->type = (unsigned char)type; /* always nonzero in this pilot */
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

void __mq_fd_closed(int fd)
{
	(void)fd;
}

void __mq_fd_replaced(int fd, __plat_handle_t h)
{
	(void)fd; (void)h;
}

/* src/misc/resource.c's RLIMIT_FSIZE machinery -- see this file's own
 * banner. Reporting "no limit" leaves __fsize_clamp/_room_at/_exceeded
 * unreachable at runtime for this pilot, but their symbols are still
 * needed if anything else pulls write.c in transitively; not linked
 * here, so only the four below (plus __fsize_allow) are provided. */
int __fsize_limited(void) { return 0; }
long long __fsize_clamp(__plat_handle_t h, int append, size_t count)
{
	(void)h; (void)append;
	return (long long)count;
}
long long __fsize_room_at(long long off) { (void)off; return 0x7fffffffffffffffLL; }
int __fsize_exceeded(void) { return -1; }
int __fsize_allow(long long size) { (void)size; return 0; }

int __vfs_stat(int kind, struct stat *st)
{
	(void)kind; (void)st;
	errno = ENOSYS;
	return -1;
}

int __fd_pos_save(HANDLE h, long long *pos)
{
	(void)h;
	*pos = 0;
	return 0;
}

void __fd_pos_restore(HANDLE h, long long pos)
{
	(void)h; (void)pos;
}

int __vfs_resolve_at(int dirfd, const char *path)
{
	(void)dirfd; (void)path;
	errno = ENOSYS;
	return -1;
}

int __ntpath(const char *path, struct __ntpath *out, ULONG attributes)
{
	(void)path; (void)out; (void)attributes;
	errno = ENOSYS;
	return -1;
}

char *__handle_path(HANDLE h)
{
	(void)h;
	return 0;
}

int __ntpath_at(int dirfd, const char *path, struct __ntpath *np, ULONG attributes)
{
	(void)dirfd; (void)path; (void)np; (void)attributes;
	errno = ENOSYS;
	return -1;
}

void __ntpath_free(struct __ntpath *np)
{
	(void)np;
}

void free(void *p)
{
	(void)p;
}

/* getpid(): a REAL raw Linux syscall (via this file's own syscall()
 * trampoline above), not a stub -- see this file's own banner for why
 * record_lock()'s fork-based lock-conflict test needs the true owning
 * pid, not a fake constant. */
#define SYS_getpid 172
pid_t getpid(void)
{
	return (pid_t)syscall(SYS_getpid);
}
