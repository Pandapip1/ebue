/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux's __fd_init(), replacing src/internal/fd.c's own (PLATFORM's
 * REPLACED_OBJS override, same mechanism crt/linux/crt1.c uses for
 * crt/crt1.c -- see Makefile's PLAT_GLOBS comment).
 *
 * NT's __fd_init() (fd.c) reads __peb->ProcessParameters for the three
 * standard handles and an optional RuntimeData blob describing every
 * OTHER descriptor a parent chose to hand down across CreateProcess --
 * both concepts specific to how NT starts a process; there is no PEB,
 * no ProcessParameters, and no such inheritance blob on Linux. A real
 * Linux process instead simply already HAS descriptors 0/1/2 open (a
 * shell or exec() sets them up before this program's first instruction
 * ever runs) and inherits every other still-open, non-close-on-exec
 * descriptor automatically, with the kernel itself as the only
 * bookkeeping authority for the raw numbers -- unlike NT, nothing here
 * needs to be TOLD which extra descriptors a parent meant to hand
 * down; a real fork()+execve() (src/process/linux/plat_process.c's own
 * __plat_process_spawn()) already carries every one of them across,
 * at the same real fd number, with no renumbering step this library
 * performs or could intercept.
 *
 * What IS this library's own job, and was missing entirely until
 * install_inherited() below: ntlibc's own __fds[] table in a freshly
 * exec'd process is a brand new, zeroed array that has never heard of
 * any of them. A raw descriptor surviving exec is perfectly usable at
 * the syscall level from the moment this function returns (exactly
 * like one a Linux program opens with a raw syscall of its own,
 * bypassing this library entirely, already works today) -- but every
 * ntlibc-level operation on it (read()/write()/fcntl()/close()/dup(),
 * all of which resolve a small integer to a handle by indexing
 * __fds[], never by asking the kernel "is this open") returns EBADF
 * until the table itself knows the slot is occupied. install_inherited()
 * closes that gap the same way this file's own install_std() already
 * establishes fd 0/1/2: by asking the kernel what is really there
 * (fcntl(F_GETFD) as an existence probe, statx(2) via classify_fd() to
 * classify it, fcntl(F_GETFL) for its access mode) rather than being
 * told, since nothing on this backend ever tells it.
 *
 * One case this does NOT cover, honestly: posix_spawn_file_actions_
 * adddup2() targeting a descriptor above 2 (src/process/posix_spawn.c
 * do_action()'s __SPAWN_DUP2 case) still uses __plat_dup() -- an
 * arbitrary-numbered duplicate -- rather than __plat_dup_to(), because
 * that call site's whole design (replay the actions on the PARENT's
 * own table, then undo them -- see posix_spawn.c's own banner) only
 * works because the duplicate's real number does NOT have to match
 * the target logical slot: forcing it to would mean dup3(2) closing
 * the PARENT's real descriptor at that exact number as an unavoidable
 * side effect, which is exactly the mutation posix_spawn() promises
 * not to leave behind, and which nothing could then undo (the parent's
 * original object at that number would simply be gone). Fixing that
 * case for real needs the target-fd wiring to happen in the CHILD,
 * after clone(2) but before execve(2) -- where __plat_process_spawn()
 * already does exactly this for fd 0/1/2's own mv[]/dup3 staging loop
 * -- generalized to whatever extra targets file actions name, which
 * is a larger change than this fix makes. A descriptor reaching a
 * child purely by not being FD_CLOEXEC (this file's own subject) is
 * unaffected by that gap and fully fixed by install_inherited() below;
 * only an EXPLICIT posix_spawn_file_actions_adddup2() to a target
 * above 2 still does not reach the child as the requested number.
 *
 * Classification (below, shared between __fd_init() and this file's
 * own __handle_type()) reuses src/fcntl/linux/plat_fcntl.c's own
 * statx()-based approach and constants (see that file's __plat_open()
 * for the fuller rationale). __handle_type() itself is declared in
 * src/internal/libc.h and called from a few genuinely portable front
 * doors (src/fcntl/fadvise.c, src/select/select.c) that never see the
 * NT-vs-Linux split directly -- this is that split's Linux half, the
 * same way src/internal/nt/plat_fd_init.c's is the NT half (a raw
 * NtQueryVolumeInformationFile/NtQueryInformationFile pair there,
 * where here a single statx(2) answers the same question). */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "libc.h"

#if defined(__aarch64__)
#define SYS_statx 291
#define SYS_fcntl 25
#elif defined(__x86_64__)
#define SYS_statx 332
#define SYS_fcntl 72
#elif defined(__i386__)
#define SYS_statx 383
#define SYS_fcntl 55
#else
#error "plat_fd_init.c: unsupported architecture"
#endif

/* fcntl(2) command numbers: F_GETFD/F_GETFL, unlike a syscall number,
 * have no per-architecture table to get wrong (uapi/asm-generic/
 * fcntl.h; none of aarch64/x86_64/i386 override either) -- the same
 * "no host-oracle probe needed" reasoning src/process/linux/
 * plat_process.c's own F_DUPFD_LX comment already gives for this
 * exact class of constant. */
#define F_GETFD_LX 1
#define F_GETFL_LX 3
#define FD_CLOEXEC_LX 1

#define AT_EMPTY_PATH_LX     0x1000
#define STATX_BASIC_STATS_LX 0x7ff

#define S_IFMT_LX   0170000
#define S_IFSOCK_LX 0140000
#define S_IFDIR_LX  0040000
#define S_IFCHR_LX  0020000
#define S_IFIFO_LX  0010000

/* Same 6-argument raw syscall trampoline every Linux backend defines
 * for itself (never the host's syscall(2) wrapper -- see plat_mem.c's
 * banner for why that collapses every failure's errno to the wrong
 * value). File-scoped by convention, not shared, the same as every
 * other Linux backend in this tree -- one body per arch's own calling
 * convention, see crt/linux/crt1.c's own raw_syscall() banner for the
 * fuller per-arch rationale. */
#if defined(__aarch64__)
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) // NOLINT(bugprone-easily-swappable-parameters) -- raw syscall ABI slots are positional and semantically distinct
{
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	register long x8 __asm__("x8") = nr;
	__asm__ volatile("svc #0"
	                 : "+r"(x0)
	                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
	                 : "memory", "cc");
	return x0;
}
#elif defined(__x86_64__)
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
	unsigned long long __rest[26]; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
};

/* Returns __FD_UNKNOWN (never 0) on a closed/invalid fd, matching
 * __handle_type()'s own NT contract -- 0 is not a member of the __FD_*
 * enum (src/internal/libc.h: __FD_FILE starts at 1), so a caller like
 * src/internal/fd.c's __fd_install_at() (`type ? type : __handle_type(h)`)
 * cannot mistake this for "no override, fall through". */
static int classify_fd(int fd)
{
	struct __lx_statx stx;
	long ret;

	memset(&stx, 0, sizeof stx);
	ret = raw_syscall(SYS_statx, (long)fd, (long)"", (long)AT_EMPTY_PATH_LX,
	                  (long)STATX_BASIC_STATS_LX, (long)&stx, 0L);
	if (is_sys_error(ret)) return __FD_UNKNOWN;

	switch (stx.stx_mode & S_IFMT_LX) {
	case S_IFDIR_LX:  return __FD_DIR;
	case S_IFIFO_LX:  return __FD_PIPE;
	case S_IFCHR_LX:  return __FD_CHAR;
	case S_IFSOCK_LX: return __FD_SOCKET;
	default:          return __FD_FILE;
	}
}

/* HANDLE here is always this backend's own boxed (fd + 1) encoding
 * (src/unistd/linux/plat_fd.c's banner) -- every caller of this
 * function already holds a __plat_handle_t this backend itself
 * produced, never a raw platform object the way NT's version queries
 * one. */
int __handle_type(HANDLE h)
{
	long fd = (long)h - 1;
	if (fd < 0) return __FD_UNKNOWN;
	return classify_fd((int)fd);
}

static void install_std(int fd)
{
	int type = classify_fd(fd);
	if (type == __FD_UNKNOWN) return; /* fd not actually open -- leave the slot empty */

	/* Boxed (fd + 1), same convention src/unistd/linux/plat_fd.c's own
	 * banner documents (__PLAT_HANDLE_NULL is 0, and fd 0/stdin is a
	 * real, valid descriptor that must not collide with "empty slot").
	 * O_RDONLY for fd 0, O_WRONLY for 1/2: the same asymmetry NT's
	 * install_std() encodes, and for the identical reason -- write()
	 * refuses an O_RDONLY descriptor and O_RDONLY is 0, so getting this
	 * wrong for an inherited stdout/stderr would silently break writes
	 * to it. */
	__fd_install_at(fd, (HANDLE)(long)(fd + 1), fd == 0 ? O_RDONLY : O_WRONLY, type);
}

/* Every descriptor above 2 that is still open when a freshly exec'd
 * process starts was inherited from whatever spawned it -- see this
 * file's own banner for why the raw number is already correct at the
 * kernel level and all that is missing is this library's OWN __fds[]
 * entry for it. Found by probing every candidate slot directly with
 * fcntl(F_GETFD) rather than by parsing /proc/self/fd/: this file's
 * own __fd_close_all_cloexec() (src/internal/fd.c) already loops the
 * identical FD_MAX bound at a comparable process-lifecycle boundary,
 * and a plain probe needs no directory-entry parsing and no procfs
 * mount to be present at all, unlike src/internal/linux/handle_path.c's
 * own (unrelated) use of /proc/self/fd for a different purpose.
 *
 * fcntl(F_GETFD) alone answers two questions in one syscall: whether
 * the slot is open at all (EBADF if not), and -- since a descriptor
 * that reached this point with FD_CLOEXEC set would already have been
 * closed by the execve(2) that got this process running, never
 * surviving to be seen here -- confirms that bit really is clear
 * rather than merely assuming it. F_GETFL supplies the access mode/
 * O_APPEND/O_NONBLOCK bits that classify_fd()'s statx(2) cannot: see
 * struct __fd's own `flags` field comment (src/internal/libc.h) for
 * why the access mode in particular is load-bearing, not decorative. */
static void install_inherited(void)
{
	int fd;
	for (fd = 3; fd < FD_MAX; fd++) {
		long fdflags = raw_syscall(SYS_fcntl, (long)fd, (long)F_GETFD_LX, 0L, 0L, 0L, 0L);
		long flflags;
		unsigned flags;
		int type;

		if (is_sys_error(fdflags)) continue; /* not open */

		type = classify_fd(fd);
		if (type == __FD_UNKNOWN) continue; /* raced closed between the two probes -- leave it out rather than guess */

		flflags = raw_syscall(SYS_fcntl, (long)fd, (long)F_GETFL_LX, 0L, 0L, 0L, 0L);
		flags = is_sys_error(flflags) ? 0 : ((unsigned)flflags & (O_ACCMODE | O_APPEND | O_NONBLOCK));
		if (fdflags & FD_CLOEXEC_LX) flags |= O_CLOEXEC; /* should not happen (see banner) -- recorded faithfully if it somehow does */

		__fd_install_at(fd, (HANDLE)(long)(fd + 1), flags, type);
	}
}

void __fd_init(void)
{
	install_std(0);
	install_std(1);
	install_std(2);
	install_inherited();
}

// NOLINTEND(misc-include-cleaner)
