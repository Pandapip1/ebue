/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_process.h -- see src/mman/
 * linux/plat_mem.c's own banner for the general discipline every Linux
 * backend file in this tree follows (raw syscall(2), no host libc,
 * -nostdinc against ntlibc's OWN headers, aarch64 syscall numbers
 * confirmed against this build host's real glibc as an oracle).
 *
 * Process-handle encoding (this backend's own concern, like plat_fd.c's
 * fd+1 boxing is theirs): a Linux __plat_handle_t here is the pid
 * itself, cast straight through -- (__plat_handle_t)(long)pid -- with
 * NO offset. Unlike a file descriptor, 0 is never a valid pid (pid 1 is
 * the lowest a wait4()-able child can ever have), so __PLAT_HANDLE_NULL
 * (0) can never collide with a real one and no +1 trick is needed.
 *
 * That plain encoding has one real, deliberately-not-fully-solved
 * consequence: fork.c's mark_children_inheritable() and children.c's
 * __child_remove() call __plat_dup()/__plat_close() --
 * src/unistd/linux/plat_fd.c's fd-domain functions -- directly on a
 * __children[] entry's process handle, not just on real fd-table
 * handles. That is correct on NT, where a process handle and a file
 * handle are the same HANDLE domain; it has no correct Linux
 * equivalent, because a pid and an fd are different kernel namespaces
 * entirely -- there is nothing to "duplicate" about a pid, and
 * __plat_close()'s fd-domain unbox() would reinterpret the boxed pid
 * as (pid - 1) and issue a real close(2) on whatever descriptor number
 * that happens to be.
 *
 * Not fixed here (would need either a shared handle-domain tag across
 * every backend, or upgrading a Linux process handle to a real fd via
 * pidfd_open(2) so it lives in the same namespace close()/dup() already
 * operate on correctly). What keeps the plain-pid encoding from
 * corrupting a live descriptor in practice: pids are drawn from a
 * namespace many orders of magnitude larger than the handful of fds a
 * process ever has open, so close(pid - 1) reliably lands on a
 * descriptor number nothing has ever opened and fails silently with
 * EBADF, which every call site above already discards. That is a
 * coincidence of scale, not a proof.
 *
 * The other structural difference from NT: NtWaitForSingleObject()
 * merely *signals* that a process handle became signalled, and a
 * separate NtQueryInformationProcess() can read its exit code and
 * times afterward, any number of times, because the handle itself
 * keeps the object alive. Linux's wait4()/waitpid() do both in one
 * shot -- reporting a child's exit status IS what reaps it,
 * irreversibly, and a second wait4() on the same pid fails ECHILD. So
 * __plat_process_wait() below does the real, one-time reap itself (the
 * only call in this file that can), and stashes the translated exit
 * code and CPU times in a small fixed-size table for
 * __plat_process_exit_code()/__plat_process_times() to read back --
 * the split the header's contract expects, implemented on this side of
 * the interface instead of trusted to the kernel object a second time.
 * The exit code stashed is deliberately encoded the same way the NT
 * backend's is (a plain 0-255 value, or __ENCODE_SIGNAL_EXIT(sig) for a
 * signal death -- see libc.h), so src/process/wait.c's
 * __wait_encode_status(), written once and shared by every backend,
 * reconstructs the identical POSIX wait status Linux's own wait4()
 * status already encoded, without either backend needing its own copy
 * of that decoding.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <errno.h>
#include <stdlib.h>     /* malloc()/free() -- __plat_process_spawn()'s own
                         * comment on `struct fd_move` below explains the
                         * one call site that needs them, deep in the
                         * cloned child, and why that is safe: no threads,
                         * and no CLONE_VM to make the child's heap share
                         * live state with the parent's. */
#include <sys/wait.h>   /* WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG -- see
                         * __plat_process_wait()'s own comment below for why
                         * these apply directly to a raw Linux wait4(2)
                         * status with no translation of their own. */
#include "libc.h"
#include "plat_process.h"

/* Linux syscall numbers -- aarch64 confirmed against this build host's
 * own <sys/syscall.h> (via a throwaway host-gcc program, not assumed;
 * see the report). aarch64 has no fork(2) syscall at all -- glibc's own
 * fork() is built on clone(2) -- so __plat_process_fork() below uses
 * clone(SIGCHLD, 0, 0, 0, 0), confirmed by a standalone probe on this
 * host to return 0 in the child / the child's pid in the parent exactly
 * like fork(), with the (flags, stack, parent_tid, child_tid, tls)
 * argument order this call relies on. x86_64 has a real, separate fork(2)
 * syscall too, but is not given one here: x86_64's own clone(2) accepts
 * the identical (flags, stack, ptid, ctid, tls) argument shape (this
 * project's own crt1.c/plat_thread.c already rely on x86_64 clone(2) for
 * other call sites), so the SAME clone(SIGCHLD, 0, 0, 0, 0) call this
 * file's __plat_process_fork()/__plat_process_spawn() already make for
 * aarch64 needs no separate x86_64 fork(2) arm at all -- one call, one
 * number swap, not a second code path. x86_64 numbers confirmed against
 * a real x86_64-linux-gnu glibc's own asm/unistd_64.h, a genuinely
 * different table from aarch64's (see src/signal/linux/plat_signal.c's
 * own updated banner for the same warning). */
#if defined(__aarch64__)
#define SYS_clone      220
#define SYS_execve     221
#define SYS_wait4      260
#define SYS_exit_group 94
#define SYS_kill       129
#define SYS_openat     56
#define SYS_close      57
#define SYS_fstat      80
#define SYS_pipe2      59
#define SYS_dup3       24
#define SYS_fcntl      25   /* same aarch64 number src/unistd/linux/plat_fd.c
                             * already uses; see __plat_process_spawn()'s
                             * own comment below for what this one call
                             * site uses it for (F_DUPFD staging). */
#define SYS_read       63
#define SYS_write      64
#define SYS_nanosleep  101
#elif defined(__x86_64__)
#define SYS_clone      56
#define SYS_execve     59
#define SYS_wait4      61
#define SYS_exit_group 231
#define SYS_kill       62
#define SYS_openat     257
#define SYS_close      3
#define SYS_fstat      5
#define SYS_pipe2      293
#define SYS_dup3       292
#define SYS_fcntl      72   /* same x86_64 number src/fcntl/linux/
                             * plat_fcntl.c already uses. */
#define SYS_read       0
#define SYS_write      1
#define SYS_nanosleep  35
#elif defined(__i386__)
#define SYS_clone      120
#define SYS_execve     11
#define SYS_wait4      114
#define SYS_exit_group 252
#define SYS_kill       37
#define SYS_openat     295
#define SYS_close      6
#define SYS_fstat      108
#define SYS_pipe2      331
#define SYS_dup3       330
#define SYS_fcntl      55   /* same i386 number src/fcntl/linux/
                             * plat_fcntl.c already uses. */
#define SYS_read       3
#define SYS_write      4
#define SYS_nanosleep  162
#else
#error "plat_process.c: unsupported architecture (expected __aarch64__, __x86_64__ or __i386__)"
#endif

#define AT_FDCWD_LX     (-100)
#define O_CLOEXEC_LX    0x80000  /* octal 02000000 -- confirmed against the host */
#define WNOHANG_LX      1
#define SIGCHLD_LX      17
#define SIGCONT_LX      18
/* F_DUPFD is command 0 on every Linux architecture (uapi/asm-generic/
 * fcntl.h; none of aarch64/x86_64/i386 override it) -- unlike a syscall
 * number, an fcntl(2) command constant has no per-arch table to get
 * wrong, so this needs no "confirmed against the host" probe the way
 * the syscall numbers above do. */
#define F_DUPFD_LX      0

/* Regular-file / execute-permission-bit masks, standard POSIX values. */
#define S_IFMT_LX  0170000
#define S_IFREG_LX 0100000
#define S_IXUSR_LX 0100
#define S_IXGRP_LX 0010
#define S_IXOTH_LX 0001

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all. NOT `extern long syscall(long, ...)`: that
 * symbol is satisfied by the HOST's real glibc at link time (this
 * build is -nostdinc, not -nostdlib -- only compiling avoids the host
 * headers, the final link step still pulls in host libc), and glibc's
 * syscall() performs its own error translation: on failure it returns
 * exactly -1 and sets glibc's OWN errno (a different memory location
 * than ntlibc's own errno global, src/internal/errno.c) to the real
 * code -- it does NOT hand back the raw kernel -errno in [-4095,-1]
 * this file's is_sys_error()/`errno = (int)-ret` translation requires.
 * Confirmed both by inspecting the linked pilot binary (nm -D shows an
 * undefined `syscall@GLIBC_*`, resolved by ld-linux at runtime) and
 * independently by src/thread/linux/plat_thread.c's own port, which
 * hit the identical bug and is this fix's model. aarch64's syscall
 * calling convention: x8 = syscall number, x0..x5 = up to 6 arguments,
 * result (or -errno in [-4095,-1]) in x0. Notably: __plat_process_fork()
 * below relies on this returning zero exactly once from the *child*
 * side of a real clone(2), the same way a bare `svc #0` does and glibc's
 * own fork()/clone() wrappers must -- this is not merely an errno-
 * correctness fix for this file, a glibc `syscall(SYS_clone, ...)` call
 * would also be unsafe here for the reason src/thread/linux/plat_thread.c's
 * own banner documents for its clone() trampoline (a C function's
 * still-live stack frame across the child/parent split), though
 * SIGCHLD-only clone (no CLONE_VM) happens to share the parent's stack
 * so that particular hazard does not apply to this file's simpler use. */
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
 * calling-convention rationale, and this file's own updated banner above
 * for why __plat_process_fork()'s clone(SIGCHLD, 0, 0, 0, 0) call is
 * still safe issued through this ordinary (non-CLONE_VM) function on
 * x86_64 too: a `syscall` instruction returning twice, sharing the
 * calling thread's own stack, is exactly as safe here as `svc #0`
 * returning twice already is on aarch64 -- neither arch's variant of
 * this function ever runs on a fresh/shared stack the way src/thread/
 * linux/clone_aarch64.S's own CLONE_VM trampoline has to guard against
 * (see that file's banner). */
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
/* See crt/linux/crt1.c's own raw_syscall() banner (i386 branch) for the
 * full register-pressure rationale -- duplicated here per this tree's
 * own "own syscall table per file" discipline. `int $0x80` returning
 * twice from __plat_process_fork()'s clone(SIGCHLD, 0, 0, 0, 0) call is
 * exactly as safe here as the other two arches' branches above: this
 * asm block itself never touches %esp beyond the paired push/pop it
 * restores before returning, so both the parent and child return through
 * an intact, ordinary C stack frame -- the same "no CLONE_VM, so no
 * fresh/shared stack to worry about" reasoning already given above. */
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

static int box_pid(int pid) { return pid; }   /* documentation no-op: see this file's banner */
static int unbox_pid(__plat_handle_t h) { return (int)(long)h; }

/* std[0..2]'s three slots are NOT this file's own process-handle
 * domain: they come straight from the fd table (src/process/spawn.c's
 * own comment: "the front door's own fd-table lookup"), boxed the way
 * src/unistd/linux/plat_fd.c encodes them (fd+1, so a real fd 0 is
 * never confused with __PLAT_HANDLE_NULL). Unboxing them here mirrors
 * that file's own convention deliberately -- it is not this file's
 * encoding leaking, it is reading someone else's. */
static int unbox_fd(__plat_handle_t h) { return (int)((long)h - 1); }

/* ---- find_program.c: is this file something Linux's loader/kernel --- */
/* ---- can start directly? ---------------------------------------------- */

/* A minimal, byte-exact mirror of the leading fields of Linux's real
 * kernel struct stat, padded out to the kernel's real total size so
 * fstat(2) never writes past this buffer. Only st_mode is ever read.
 *
 * GENUINELY DIFFERENT FIELD ORDER per arch, not just different widths --
 * confirmed against a real x86_64-linux-gnu glibc's own <bits/struct_
 * stat.h>/<asm/stat.h> headers, not assumed to be a scaled copy of the
 * aarch64 layout: aarch64 (and every other "generic 64-bit ABI" arch)
 * orders st_mode BEFORE st_nlink, both `unsigned int`, at byte offset 16
 * (dev(8)+ino(8)); x86_64's raw kernel struct stat orders st_nlink
 * BEFORE st_mode instead (both still `unsigned int`), so st_mode sits at
 * offset 24 (dev(8)+ino(8)+nlink(8) -- st_nlink is `unsigned long`/8
 * bytes wide on x86_64, unlike aarch64's 4-byte `unsigned int` one);
 * i386's classic (pre-LFS) struct stat is different again: st_dev/
 * st_ino are 4-byte `unsigned long` (i386's own native word size, not
 * aarch64/x86_64's 8-byte one) and st_mode/st_nlink are 2-byte
 * `unsigned short`, ordered mode-then-nlink like aarch64, landing
 * st_mode at offset 8 (dev(4)+ino(4)). Whole-struct sizes differ to
 * match: 128 bytes (aarch64), 144 bytes (x86_64), 64 bytes (i386, this
 * arch's SYS_fstat is the OLD non-LFS fstat(2), 32-bit st_size/times
 * included -- moot here, this file only ever reads st_mode). */
#if defined(__aarch64__)
struct raw_stat_prefix {
	unsigned long st_dev;
	unsigned long st_ino;
	unsigned int  st_mode;
	unsigned int  st_nlink;
	unsigned char reserved[128 - 24];
};
#define RAW_STAT_SIZE 128
#elif defined(__x86_64__)
struct raw_stat_prefix {
	unsigned long st_dev;
	unsigned long st_ino;
	unsigned long st_nlink;
	unsigned int  st_mode;
	unsigned char reserved[144 - 28];
};
#define RAW_STAT_SIZE 144
#elif defined(__i386__)
struct raw_stat_prefix {
	unsigned long  st_dev;
	unsigned long  st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned char  reserved[64 - 12];
};
#define RAW_STAT_SIZE 64
#else
#error "plat_process.c: unsupported architecture (expected __aarch64__, __x86_64__ or __i386__)"
#endif
/* Sanity-checked once, at compile time, rather than trusted by
 * inspection alone: reserved[]'s own size above already forces
 * sizeof(struct raw_stat_prefix) to RAW_STAT_SIZE for a compiler that
 * lays out the fields with no unexpected extra padding -- this
 * static_assert is what turns "unexpected extra padding" from a silent
 * fstat(2) buffer-overflow risk into a build failure instead. */
_Static_assert(sizeof(struct raw_stat_prefix) == RAW_STAT_SIZE,
               "raw_stat_prefix size mismatch for this architecture");

/* Unlike NT (no execute-permission bit on the filesystem, so
 * find_program.c's own $LXMOD-plus-content-sniff dance exists at all --
 * see that file's banner) and unlike NT's own __plat_is_program()
 * (which also has to dodge waking a cloud-backed placeholder file --
 * see src/process/nt/plat_process.c), Linux has a real execute
 * permission bit and its own kernel already runs a "#!" script directly
 * through binfmt_script -- there is nothing here for this backend to
 * distinguish that NT's loader needs help with. A regular file with any
 * execute bit set is answered "yes"; anything else, including any
 * failure to open or stat it, is "no", same as the NT backend's rule
 * for a failure of any kind. */
int __plat_is_program(const char *path)
{
	long fd = raw_syscall(SYS_openat, (long)AT_FDCWD_LX, (long)path, 0L /* O_RDONLY */, 0L, 0L, 0L);
	struct raw_stat_prefix st = {0};
	long ret;

	if (is_sys_error(fd)) return 0;
	ret = raw_syscall(SYS_fstat, fd, (long)&st, 0L, 0L, 0L, 0L);
	raw_syscall(SYS_close, fd, 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) return 0;
	if ((st.st_mode & S_IFMT_LX) != S_IFREG_LX) return 0;
	if (!(st.st_mode & (S_IXUSR_LX | S_IXGRP_LX | S_IXOTH_LX))) return 0;
	return 1;
}

/* ---- fork.c: clone(2) and the (nonexistent) suspended-thread resume -- */

int __plat_process_fork(struct __plat_fork_result *out)
{
	long pid = raw_syscall(SYS_clone, (long)SIGCHLD_LX, 0L, 0L, 0L, 0L, 0L);

	if (pid == 0) return __PLAT_FORK_CHILD;
	if (is_sys_error(pid)) { errno = (int)-pid; return -1; }

	out->process = (__plat_handle_t)(long)box_pid((int)pid);
	/* No suspended clone thread exists to hand back here -- clone(2)
	 * without CLONE_VM starts the child running immediately, at the
	 * same point in this very call, exactly like glibc's own fork()
	 * (see this file's banner). __PLAT_HANDLE_NULL is safe to pass to
	 * __plat_thread_resume() below precisely because that function
	 * never looks at it. */
	out->thread = __PLAT_HANDLE_NULL;
	/* No job-object concept on Linux -- the kernel already folds a
	 * reaped child's own recursively-accumulated cutime/cstime into
	 * this process's accounting at wait4() time, natively, which is
	 * exactly what the NT backend's own job field exists to
	 * reconstruct (see plat_process.h and src/internal/libc.h's struct
	 * __child.job).  Always NULL here; __plat_process_times() below
	 * ignores it unconditionally, so this is documentation, not a
	 * functional requirement. */
	out->job = __PLAT_HANDLE_NULL;
	out->pid = (int)pid;
	return __PLAT_FORK_PARENT;
}

int __plat_thread_resume(__plat_handle_t th)
{
	(void)th;
	/* Canonical implementation for both plat_process.h and (by the
	 * cross-subsystem collision plat_process.h's own banner and this
	 * project's history document) plat_thread.h's identically-named
	 * declaration. On Linux, nothing this pilot creates -- a fork()ed
	 * child, an exec()ed process, or (once a Linux thread backend
	 * lands) a clone()d thread -- is ever created in a suspended state
	 * needing a resume in the first place: clone(2)/execve(2) hand back
	 * something already running. So this is unconditionally a no-op
	 * success, independent of whatever encoding `th` turns out to carry
	 * on whichever backend eventually defines a real one. */
	return 0;
}

/* ---- wait.c: wait4(2), and the peek/query split Linux's one-shot ----- */
/* ---- reap does not offer natively -------------------------------------- */

#define REAP_CACHE_MAX 256   /* matches CHILD_MAX_'s own static-table sizing (libc.h) */

struct reap_entry {
	int pid;                 /* 0 == free slot; a real pid is never 0 */
	int code;                /* NT-shaped: __wait_encode_status()'s input */
	unsigned long long ktime100ns;
	unsigned long long utime100ns;
};

static struct reap_entry reap_cache[REAP_CACHE_MAX];

static struct reap_entry *reap_find(int pid)
{
	int i;
	for (i = 0; i < REAP_CACHE_MAX; i++)
		if (reap_cache[i].pid == pid) return &reap_cache[i];
	return 0;
}

static struct reap_entry *reap_alloc(int pid)
{
	int i;
	for (i = 0; i < REAP_CACHE_MAX; i++)
		if (!reap_cache[i].pid) { reap_cache[i].pid = pid; return &reap_cache[i]; }
	return 0;   /* table exhausted -- see the fallback note below */
}

/* Return a slot to the pool once its info has been delivered -- see
 * __plat_process_reap_release()'s own comment (called from here) and
 * plat_process.h's for the full lifecycle argument. pid == 0 is exactly
 * "already free" (struct reap_entry's own comment), so a redundant or
 * not-found release -- __plat_process_wait()'s degraded "table was full,
 * nothing was ever cached for this pid" path, this file's own comment
 * further down -- is silently a no-op, same as reap_find() already
 * treats it. */
static void reap_free(int pid)
{
	struct reap_entry *e = reap_find(pid);
	if (e) e->pid = 0;
}

/* Byte-exact mirror of Linux's real struct rusage (confirmed against
 * the host: ru_utime at offset 0, ru_stime at offset 16, both
 * `struct timeval`, whole struct 144 bytes -- see the report), padded
 * to the kernel's real size for the same reason raw_stat_prefix is. */
struct raw_timeval { long tv_sec; long tv_usec; };
struct raw_rusage {
	struct raw_timeval ru_utime;
	struct raw_timeval ru_stime;
	unsigned char reserved[144 - 32];
};

/* tv required: dereferenced unconditionally (`tv->tv_sec`) as the very
 * first thing this function does; its only real call sites pass
 * &ru.ru_stime/&ru.ru_utime, addresses of a local, never NULL. */
static unsigned long long tv_to_100ns(struct raw_timeval *tv)
    __attribute__((nonnull(1)));
static unsigned long long tv_to_100ns(struct raw_timeval *tv)
{
	return (unsigned long long)tv->tv_sec * 10000000ULL +
	       (unsigned long long)tv->tv_usec * 10ULL;
}

int __plat_process_wait(__plat_handle_t h, int mode)
{
	int pid = unbox_pid(h);
	int status = 0;
	long options;
	long ret;
	struct raw_rusage ru;
	struct reap_entry *e;
	int i;

	/* Already reaped by an earlier call to this very function -- report
	 * "still signalled" without touching the kernel again, since the
	 * pid may since have been recycled onto an unrelated live process. */
	if (reap_find(pid)) return 1;

	switch (mode) {
	case __PLAT_WAIT_NOHANG:
	case __PLAT_WAIT_POLL:   options = WNOHANG_LX; break;
	default:                 options = 0; break;
	}

	for (i = 0; i < (int)sizeof ru; i++) ((unsigned char *)&ru)[i] = 0;
	ret = raw_syscall(SYS_wait4, (long)pid, (long)&status, options, (long)&ru, 0L, 0L);

	if (mode == __PLAT_WAIT_POLL && ret == 0) {
		/* Not yet exited: sleep ~10ms, the same short poll interval
		 * the NT backend's own __PLAT_WAIT_POLL case builds into its
		 * single NtWaitForSingleObject call (see plat_process.h), so a
		 * WUNTRACED caller re-checking in a loop does not spin. */
		struct { long tv_sec; long tv_nsec; } ts;
		ts.tv_sec = 0; ts.tv_nsec = 10000000L;
		raw_syscall(SYS_nanosleep, (long)&ts, 0L, 0L, 0L, 0L, 0L);
		return 0;
	}
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	if (ret == 0) return 0;   /* WNOHANG: not exited yet */

	e = reap_alloc(pid);
	if (WIFSIGNALED(status)) {
		int code_val = WTERMSIG(status);
		int nt_code = (int)(0xE0DE0000u | ((unsigned)code_val & 0x7fu));
		if (e) e->code = nt_code;
	} else {
		int exitcode = WEXITSTATUS(status) & 0xff;
		if (e) e->code = exitcode;
	}
	if (e) {
		e->ktime100ns = tv_to_100ns(&ru.ru_stime);
		e->utime100ns = tv_to_100ns(&ru.ru_utime);
	}
	/* A full reap-cache table (REAP_CACHE_MAX simultaneously-unreaped
	 * children) has nowhere left to stash this exit status: the child
	 * is already, unavoidably, reaped by the wait4() above, so
	 * __plat_process_exit_code() would have nothing to read back. This
	 * pilot degrades by still reporting the process signalled -- losing
	 * only the exit-code/rusage detail, not correctness of the reap
	 * itself, which already happened and cannot be undone. */
	return 1;
}

int __plat_process_exit_code(__plat_handle_t h, int *code)
{
	struct reap_entry *e = reap_find(unbox_pid(h));
	if (!e) { errno = ECHILD; return -1; }
	*code = e->code;
	return 0;
}

int __plat_process_times(__plat_handle_t h, __plat_handle_t job,
                          unsigned long long *ktime100ns, unsigned long long *utime100ns)
{
	struct reap_entry *e = reap_find(unbox_pid(h));
	/* job is always __PLAT_HANDLE_NULL on this backend (see
	 * __plat_process_fork()'s own comment) and never consulted: the
	 * wait4(2) rusage this file already captured into *e (this file's
	 * banner, "the resource usage ... already recursively folds in
	 * grandchildren") is the complete answer on its own. */
	(void)job;
	if (!e) { errno = ECHILD; return -1; }
	*ktime100ns = e->ktime100ns;
	*utime100ns = e->utime100ns;
	return 0;
}

/* See plat_process.h's own comment on this call for the general
 * contract; this backend is the one that actually needs it. A
 * reap_cache slot exists only to bridge __plat_process_wait()'s real,
 * irreversible wait4(2) reap to the __plat_process_exit_code()/
 * __plat_process_times() reads do_waitpid() makes immediately
 * afterward, in the same call -- once wait.c has both answers it never
 * asks this backend again for the same handle (a WNOWAIT repeat read
 * comes back out of struct __child.status one layer up instead, see
 * plat_process.h). So the slot has no reason to survive past that
 * point, and REAP_CACHE_MAX exists at all only because two calls happen
 * to need it live at once, not because it is meant to be a durable
 * history of past children. Freeing it here, rather than leaving it
 * allocated for the rest of this process's life, is what keeps a
 * long-running process (a shell, crond, atd, anything that forks in a
 * loop) from exhausting all REAP_CACHE_MAX slots after its 257th total
 * reap and then failing every wait*() on a real child from then on. */
void __plat_process_reap_release(__plat_handle_t h)
{
	reap_free(unbox_pid(h));
}

/* ---- signal.c's job-control resume, via kill()'s job-control arm ----- */
/* ---- (src/process/children.c's clear_stops() also calls this) -------- */

int __plat_process_resume(__plat_handle_t h)
{
	/* Canonical implementation for both plat_process.h and
	 * plat_signal.h's identically-named declaration (see this project's
	 * history for why process is the one owner of both -- plat_process.h's
	 * own file banner). Linux's job-control suspend/resume IS a real
	 * signal, unlike NT's NtSuspendProcess/NtResumeProcess pair, which
	 * this project's signal subsystem otherwise has to synthesize with a
	 * self-stop marker (see plat_signal.h's own banner): SIGCONT here is
	 * the actual kernel mechanism a Linux plat_signal.c's own
	 * __plat_process_suspend() (SIGSTOP) would pair with, not a
	 * substitute for one. */
	long ret = raw_syscall(SYS_kill, (long)unbox_pid(h), (long)SIGCONT_LX, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* ---- spawn.c: fork + dup the standard descriptors + execve ----------- */

/* One entry in __plat_process_spawn()'s child-side descriptor staging --
 * see that function's own comment for the full algorithm. `target` is
 * the descriptor number the entry must end up AT (0, 1 or 2 for a
 * standard descriptor; posix_spawn_file_actions_adddup2()'s own
 * `newfd`, always > 2, for one of struct __spawn_dup2_target's own
 * entries). `orig` is the real fd number the content started at, before
 * this function touched anything; `mv` is the CURRENT best-known real fd
 * number for that same content, initially a copy of `orig` and
 * overwritten with a scratch descriptor if that original number needed
 * staging out of the target zone first. `mv < 0` means "this target
 * should end up closed" -- only possible for a std[0..2] entry, since
 * struct __spawn_dup2_target never records one for a target that
 * ultimately closed (posix_spawn.c's build_dup2_targets() own comment). */
struct fd_move {
	int target;
	int mv;
	int orig;
};

int __plat_process_spawn(const char *path, char *const argv[], char *const envp[],
                         const __plat_handle_t std[3], __plat_handle_t *out_process,
                         __plat_handle_t *out_job)
{
	long pfd_ret;
	int pipefd[2];
	long pid;

	/* A self-pipe, close-on-exec on the write end, is the standard way
	 * to make an otherwise-asynchronous fork()+execve() report a real
	 * execve() failure (ENOENT, ENOEXEC, EACCES, ...) back to the
	 * caller of THIS call, synchronously -- the same atomic-outcome
	 * contract NtCreateUserProcess gives the NT backend for free (see
	 * plat_process.h's banner: "the specific-NTSTATUS-to-errno
	 * decisions ... that only make sense with the real status in
	 * hand"). A successful execve() replaces the child's image (and
	 * every fd flagged O_CLOEXEC closes as part of that), so the pipe's
	 * write end closes itself and the parent's read below sees EOF; a
	 * failed execve() leaves the child able to write its errno first. */
	{
		/* pipe2(2) writes exactly two `int`s -- NOT two register-width
		 * values -- into this buffer; declaring it as anything wider
		 * would leave pipefd[1] reading uninitialized stack past what
		 * the kernel actually wrote. */
		int fds[2] = {-1, -1};
		pfd_ret = raw_syscall(SYS_pipe2, (long)fds, (long)O_CLOEXEC_LX, 0L, 0L, 0L, 0L);
		if (is_sys_error(pfd_ret)) { errno = (int)-pfd_ret; return -1; }
		pipefd[0] = fds[0];
		pipefd[1] = fds[1];
	}

	pid = raw_syscall(SYS_clone, (long)SIGCHLD_LX, 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(pid)) {
		int e = (int)-pid;
		raw_syscall(SYS_close, (long)pipefd[0], 0L, 0L, 0L, 0L, 0L);
		raw_syscall(SYS_close, (long)pipefd[1], 0L, 0L, 0L, 0L, 0L);
		errno = e;
		return -1;
	}

	if (pid == 0) {
		/* Child. std[i] == __PLAT_HANDLE_NULL means "closed" (spawn.c
		 * already turned a close-on-exec descriptor into that, exactly
		 * like the NT backend expects -- plat_process.h's banner): an
		 * actual close(2) represents that on Linux, where there is no
		 * NT-style value-blind-placeholder dance to work around (see
		 * src/process/nt/plat_process.c's own file banner for why NT
		 * needs one and Linux does not: a closed Linux fd number is
		 * simply absent, and nothing refills it from outside). */
		int i, ntotal, extra_n = 0;
		struct fd_move *fm;
		const struct __spawn_dup2_target *extra;
		int max_target;
		raw_syscall(SYS_close, (long)pipefd[0], 0L, 0L, 0L, 0L, 0L);

		/* posix_spawn_file_actions_adddup2() targets above 2 -- see
		 * struct __spawn_dup2_target's own comment (libc.h) and
		 * src/process/posix_spawn.c's build_dup2_targets(). Folded into
		 * the SAME staging pass as std[0..2] below, not a separate one
		 * run before or after it: a source this loop would otherwise
		 * treat as "safe" because it sits outside 0..2 could still
		 * collide with one of THESE targets, or one of these targets'
		 * own source could collide with 0, 1 or 2 -- the hazard the rest
		 * of this comment describes is exactly as real across the two
		 * groups as within either one alone, so both need the same
		 * target-zone floor to stage against. */
		extra = __spawn_pending_dup2s(&extra_n);
		ntotal = 3 + extra_n;

		/* One malloc rather than three: `target`/`mv`/`orig` (the
		 * per-entry destination, current-best-known source, and
		 * pre-staging source this file's own history already needed
		 * for fd 0/1/2 alone, ->target/->mv/->orig below) only ever
		 * travel together, so a single struct keeps the ownership
		 * story this project's own allocator-token checker follows
		 * (see tools/lint.sh) to exactly one alloc/free pair instead of
		 * three. Sized ntotal, never less than 3, so this covers fd
		 * 0/1/2 even when extra_n is 0 -- the common case, and the
		 * reason a heap allocation here (where the unfixed code used a
		 * fixed `int mv[3]`) is an acceptable cost: posix_spawn()
		 * already fails with ENOMEM well before reaching this point
		 * for other reasons (src/process/posix_spawn.c's own sv/extra
		 * allocations), so this is one more of those, not a new class
		 * of failure. */
		fm = malloc((size_t)ntotal * sizeof *fm);
		if (!fm) {
			int e = ENOMEM;
			raw_syscall(SYS_write, (long)pipefd[1], (long)&e, (long)sizeof e, 0L, 0L, 0L);
			raw_syscall(SYS_close, (long)pipefd[1], 0L, 0L, 0L, 0L, 0L);
			raw_syscall(SYS_exit_group, 127L, 0L, 0L, 0L, 0L, 0L);
			/* unreachable */
			return -1;
		}

		max_target = 2;
		for (i = 0; i < 3; i++) {
			fm[i].target = i;
			fm[i].orig = std[i] ? unbox_fd(std[i]) : -1;
		}
		for (i = 0; i < extra_n; i++) {
			fm[3 + i].target = extra[i].fd;
			fm[3 + i].orig = unbox_fd(extra[i].h);
			if (extra[i].fd > max_target) max_target = extra[i].fd;
		}

		/* srcfd (a raw Linux fd number) is a completely different
		 * namespace from the target it is being moved TO, and nothing
		 * keeps the two apart: srcfd can itself equal ANY target in
		 * this combined list. Mutating a target in place -- close(), or
		 * dup3(), which closes whatever was there first -- would
		 * silently destroy that raw fd if a later target still needs it
		 * as ITS OWN source (dup3()'s return value is not checked
		 * below, so that failure was never reported: the redirect just
		 * silently never happens, execve() runs anyway, and the target
		 * fd comes up as whatever the process already had there, not
		 * what the caller asked to redirect it to). Confirmed live for
		 * the fd-0/1/2-only case this generalizes: closing fd 0 in the
		 * parent, then adding a dup2-onto-fd-2 file action whose own
		 * __plat_dup() (posix_spawn.c do_action) reused that
		 * just-freed fd 0 for its new descriptor, reproduced exactly
		 * this -- the child's stderr redirect silently failed to reach
		 * the target pipe.
		 *
		 * Fixed the general way, not just for the three standard
		 * descriptors: every live source at or below the highest
		 * target in the WHOLE list is first duplicated (F_DUPFD) to a
		 * scratch descriptor guaranteed to sit above every target,
		 * before any target is touched at all. Once staged, no
		 * target's dup3()/close() can reach a scratch fd (none of them
		 * sit within the target zone by construction), so the
		 * placement pass below is safe regardless of which raw fd
		 * numbers std[0..2] or the extra targets originally named --
		 * including a full swap among any of them. A source already
		 * above every target needs no such staging: nothing this loop
		 * does can ever reach it. */
		for (i = 0; i < ntotal; i++) {
			fm[i].mv = fm[i].orig;
			if (fm[i].mv < 0 || fm[i].mv > max_target) continue;
			{
				long t = raw_syscall(SYS_fcntl, (long)fm[i].mv,
				                      (long)F_DUPFD_LX, (long)(max_target + 1), 0L, 0L, 0L);
				/* F_DUPFD failing here (fd-table exhaustion) is the
				 * only way fm[i].mv can still land inside the target
				 * zone -- fall back to the original, unstaged source
				 * rather than lose the redirect outright; this is no
				 * worse than the unfixed code was for every case, and
				 * better for most of them. */
				if (!is_sys_error(t)) fm[i].mv = (int)t;
			}
		}
		for (i = 0; i < ntotal; i++) {
			if (fm[i].mv < 0) {
				raw_syscall(SYS_close, (long)fm[i].target, 0L, 0L, 0L, 0L, 0L);
			} else if (fm[i].mv != fm[i].target) {
				raw_syscall(SYS_dup3, (long)fm[i].mv, (long)fm[i].target, 0L, 0L, 0L, 0L);
			}
		}
		/* Close the scratch copies -- two different rules for the two
		 * groups sharing this array, both already true of the fd-0/1/2
		 * code this generalizes:
		 *
		 * A std[0..2] entry (i < 3) preserves an unstaged source
		 * (fm[i].mv == fm[i].orig) rather than closing it: that source
		 * is the CALLER's own real descriptor (src/process/spawn.c's
		 * own f0/f1/f2 lookup, forwarded through unchanged), which just
		 * happens to also be a standard descriptor's source -- closing
		 * it here would take away a descriptor the caller never asked
		 * to lose, on top of placing the redirect. It survives into
		 * the child (or not) exactly as an ordinary non-close-on-exec
		 * descriptor already would.
		 *
		 * An extra target (i >= 3) has no such caller-visible original
		 * to preserve: build_dup2_targets() (posix_spawn.c) only ever
		 * records a handle do_action()'s own __plat_dup() minted
		 * purely to feed THIS spawn, at whatever arbitrary real number
		 * dup(2) happened to hand back -- nothing else in the process
		 * ever refers to that number by design (see posix_spawn.c's
		 * own banner on why a plain, arbitrary duplicate is made at
		 * all rather than forcing the real number to match the
		 * target). Leaving it open here would leak a second,
		 * unrequested descriptor into the child pointing at the exact
		 * same object as the one correctly placed at the target, so it
		 * is always closed once placed -- whether or not F_DUPFD
		 * staging ever ran for it (fm[i].mv == fm[i].orig only means
		 * "unstaged" here, not "keep": for this group a throwaway
		 * duplicate is a throwaway duplicate either way). */
		for (i = 0; i < ntotal; i++) {
			if (fm[i].mv < 0 || fm[i].mv == fm[i].target) continue;
			if (i < 3 && fm[i].mv == fm[i].orig) continue;
			raw_syscall(SYS_close, (long)fm[i].mv, 0L, 0L, 0L, 0L, 0L);
		}
		free(fm);
		{
			long ret = raw_syscall(SYS_execve, (long)path, (long)argv, (long)envp, 0L, 0L, 0L);
			int e = is_sys_error(ret) ? (int)-ret : EINVAL;
			raw_syscall(SYS_write, (long)pipefd[1], (long)&e, (long)sizeof e, 0L, 0L, 0L);
			raw_syscall(SYS_close, (long)pipefd[1], 0L, 0L, 0L, 0L, 0L);
		}
		raw_syscall(SYS_exit_group, 127L, 0L, 0L, 0L, 0L, 0L);
		/* unreachable */
		return -1;
	}

	/* Parent. */
	raw_syscall(SYS_close, (long)pipefd[1], 0L, 0L, 0L, 0L, 0L);
	{
		int e = 0;
		long n = raw_syscall(SYS_read, (long)pipefd[0], (long)&e, (long)sizeof e, 0L, 0L, 0L);
		raw_syscall(SYS_close, (long)pipefd[0], 0L, 0L, 0L, 0L, 0L);
		if (n == (long)sizeof e) {
			/* execve() failed in the child; it has already called
			 * _exit(127) (via exit_group) or is about to, so reap it
			 * here rather than leaving a zombie the front door's own
			 * child table was never told about. */
			raw_syscall(SYS_wait4, pid, 0L, 0L, 0L, 0L, 0L);
			errno = e;
			return -1;
		}
	}

	*out_process = (__plat_handle_t)(long)box_pid((int)pid);
	/* No job-object concept on Linux -- see __plat_process_fork()'s own
	 * comment just above, which applies here identically. */
	*out_job = __PLAT_HANDLE_NULL;
	return (int)pid;
}

/* ---- exec.c: a real, in-place execve(2) -- the primitive NT's own --- */
/* ---- backend has nothing to offer for; see plat_process.h's own ----- */
/* ---- comment on this function for why it exists on this backend ----- */
/* ---- alone. --------------------------------------------------------- */

int __plat_process_exec(const char *path, char *const argv[], char *const envp[])
{
	long ret = raw_syscall(SYS_execve, (long)path, (long)argv, (long)envp, 0L, 0L, 0L);
	/* execve(2) returns to its caller at all only on failure -- a raw
	 * kernel -errno in [-4095,-1], the same convention is_sys_error()
	 * above already translates for every other syscall in this file.
	 * A success return is never seen here: the kernel replaces this
	 * thread's whole address space and resumes at the new image's
	 * entry point instead of returning from the `svc #0` at all, so
	 * there is no "ret == 0" success case to distinguish. */
	errno = is_sys_error(ret) ? (int)-ret : EINVAL;
	return -1;
}

// NOLINTEND(misc-include-cleaner)
