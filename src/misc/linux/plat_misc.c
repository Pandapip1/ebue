/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_misc.h -- see src/mman/linux/
 * plat_mem.c's own banner for the general discipline this file follows
 * too (raw syscall(2), no host libc, -nostdinc against ntlibc's own
 * headers, aarch64 syscall numbers confirmed against this host's own
 * <sys/syscall.h>).
 *
 * Process handle encoding: NT needs __plat_process_open{,_checked}() to
 * turn a bare pid into a real kernel object handle before anything else
 * here can touch it, because almost every NT process query/set call
 * takes a HANDLE, not a pid, and that handle has a real, closeable
 * lifetime -- and src/unistd/linux/plat_fd.c's __plat_close() (plat_fd.h)
 * is the SAME close function every front door here (src/misc/sched.c's
 * process_exists(), src/misc/resource.c's getpriority()/setpriority())
 * calls on a handle this file vends, boxed the same fd+1 way as any
 * other Linux fd. That constrains this backend's own choice: a Linux
 * process handle must actually BE something close(2) can correctly
 * close, or __plat_close(h) on a handle from __plat_process_open() would
 * silently close an unrelated, unlucky-numbered fd instead (or worse,
 * a real one, like stdin) -- a real bug, not a hypothetical one, since
 * it would go uncaught in isolation and only misbehave the moment the
 * boxed pid happened to collide with a live fd number.
 *
 * pidfd_open(2) (Linux 5.3+; confirmed present on this host, kernel
 * 7.1.10) is Linux's own answer to exactly the same problem NT's
 * process HANDLE solves: a real, closeable, kernel-refcounted reference
 * to one specific process, immune to pid reuse for as long as it stays
 * open -- so this backend's __plat_handle_t for a process is a boxed
 * pidfd (fd+1, identical to every other Linux fd in this project),
 * making __plat_close() correct here for free, with no special-casing
 * needed in that shared function at all.
 *
 * getpriority(2)/setpriority(2) are the one place this still needs a
 * bare pid_t rather than a pidfd -- no pidfd-taking priority syscall
 * exists. A small fixed side table (pidfd_pid_table[] below) records
 * the pid each pidfd this file opens actually belongs to, the same
 * kind of "this library's own bookkeeping, not an NT-specific
 * translation" src/mman/mman.c's reservation table and src/internal/
 * fd.c's own table already are; see pid_for_pidfd()'s own comment for
 * its bounded-size tradeoff.
 *
 * Cross-subsystem note, honestly flagged rather than silently assumed:
 * a handle can also reach this file as struct __child's own `h` field
 * (src/internal/libc.h), set by whichever Linux backend src/process/
 * gets from a parallel migration session this file cannot see. If that
 * session's own process handle is NOT a boxed pidfd the same way, a
 * foreign child's handle handed to __plat_priority_get()/_set() here
 * will not be found in this file's side table and will fail (ESRCH)
 * rather than silently misinterpret an arbitrary integer as a pidfd --
 * a safe failure mode, but not full interoperability, and the same
 * kind of independent-but-colliding-design gap __plat_event_set()/
 * __plat_process_resume() (this header's own banner, and plat_signal.h)
 * already needed a follow-up reconciliation commit for once.
 */
#include <errno.h>
#include <sys/resource.h>
#include "plat_misc.h"
#include "plat_fd.h"

/* aarch64 Linux syscall numbers (confirmed via a throwaway host program
 * printing the SYS_* macros from <sys/syscall.h>, the same oracle
 * technique src/mman/linux/plat_mem.c's banner describes). */
#if defined(__aarch64__)
#define SYS_sched_yield       124
#define SYS_kill              129
#define SYS_setpriority       140
#define SYS_getpriority       141
#define SYS_getrusage         165
#define SYS_lseek             62
#define SYS_prlimit64         261
#define SYS_pidfd_open        434
#define SYS_pidfd_send_signal 424
#elif defined(__x86_64__)
#define SYS_sched_yield       24
#define SYS_kill              62
#define SYS_setpriority       141
#define SYS_getpriority       140
#define SYS_getrusage         98
#define SYS_lseek             8
#define SYS_prlimit64         302
#define SYS_pidfd_open        434
#define SYS_pidfd_send_signal 424
#else
#error "plat_misc.c: unsupported architecture"
#endif

/* A genuine raw syscall trampoline, NOT a call through the host's own
 * glibc syscall(2) wrapper -- discovered necessary, not assumed, while
 * proving __plat_segv_code()'s ENOMEM classification against this
 * pilot's real native-ELF test build (fuzz/linux_pilot_test_misc.c):
 * that build links against the host's real libc for printf()/etc (see
 * its own banner), and glibc's OWN syscall() already translates a
 * kernel failure into the ISO C convention -- exactly -1, with the
 * real code left in GLIBC's own errno, a completely different storage
 * location from ntlibc's own <errno.h> (this file's own errno symbol,
 * -nostdinc, resolves to src/internal/errno.c's, not glibc's) -- so
 * `errno = (int)-ret` below would misdecode any failure whose real
 * code is not exactly EPERM(1) as EPERM regardless, since ret is
 * always exactly -1 under that wrapper, never the real negative code.
 * This function bypasses glibc's wrapper entirely and issues the raw
 * `svc #0` instruction directly, so `ret` really is the kernel's own
 * [-4095,-1]-encodes-errno value everywhere below, exactly the
 * contract is_sys_error()'s own comment already described (and exactly
 * what a real, no-libc ntlibc target build's own syscall() will
 * naturally be too, once one exists for Linux). aarch64-only, matching
 * this whole pilot's own single-host-architecture scope (tools/
 * linux-build.sh's own banner).
 *
 * Reads six va_arg(long)s regardless of how many a caller below
 * actually supplied for a given syscall number (e.g. __plat_yield()'s
 * bare `syscall(SYS_sched_yield)`) -- technically unspecified by ISO C
 * for the unsupplied tail, but harmless in practice on this ABI
 * (AAPCS64 always passes the first 8 integer arguments in x0-x7
 * regardless of the callee's actual parameter count, so the "extra"
 * reads just pick up whatever garbage was already sitting in x1-x5
 * from the caller's own prior register use, which the syscall itself
 * never inspects for an argument it does not take), and is the same
 * risk the plain `extern long syscall(long, ...)` declaration this
 * replaces already carried throughout this project's other Linux
 * backends (src/mman/linux/plat_mem.c, src/unistd/linux/plat_fd.c) --
 * masked there only because those call sites happen to route through
 * glibc's own hand-written assembly implementation instead of a C
 * va_arg reimplementation. */
#include <stdarg.h>
#if defined(__aarch64__)
static long syscall(long number, ...)
{
	va_list ap;
	long a1, a2, a3, a4, a5, a6;
	register long x8 __asm__("x8");
	register long x0 __asm__("x0");
	register long x1 __asm__("x1");
	register long x2 __asm__("x2");
	register long x3 __asm__("x3");
	register long x4 __asm__("x4");
	register long x5 __asm__("x5");

	va_start(ap, number);
	a1 = va_arg(ap, long); a2 = va_arg(ap, long); a3 = va_arg(ap, long);
	a4 = va_arg(ap, long); a5 = va_arg(ap, long); a6 = va_arg(ap, long);
	va_end(ap);

	x8 = number; x0 = a1; x1 = a2; x2 = a3; x3 = a4; x4 = a5; x5 = a6;
	__asm__ volatile("svc #0"
	                 : "+r"(x0)
	                 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
	                 : "memory", "cc");
	return x0;
}
#elif defined(__x86_64__)
/* Same variadic-capture trick as the aarch64 version above, just this
 * arch's own `syscall` register convention (see crt/linux/crt1.c's own
 * raw_syscall() banner for the fuller per-arch calling-convention
 * rationale): the x86-64 SysV ABI's own variadic-function contract
 * (a register save area a callee's own prologue spills into before
 * va_start ever runs) makes reading six va_arg(long)s here just as
 * sound as it is on aarch64, regardless of how many a given call site
 * actually supplied. */
static long syscall(long number, ...)
{
	va_list ap;
	long a1, a2, a3, a4, a5, a6;
	long ret;
	register long r10 __asm__("r10");
	register long r8  __asm__("r8");
	register long r9  __asm__("r9");

	va_start(ap, number);
	a1 = va_arg(ap, long); a2 = va_arg(ap, long); a3 = va_arg(ap, long);
	a4 = va_arg(ap, long); a5 = va_arg(ap, long); a6 = va_arg(ap, long);
	va_end(ap);

	r10 = a4; r8 = a5; r9 = a6;
	__asm__ volatile("syscall"
	                 : "=a"(ret)
	                 : "a"(number), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
	                 : "rcx", "r11", "memory");
	return ret;
}
#endif

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

static int unbox_fd(__plat_handle_t h) { return (int)((long)h - 1); }
static __plat_handle_t box_fd(int fd) { return (__plat_handle_t)(long)(fd + 1); }

/* pidfd -> pid side table for __plat_priority_get()/_set(): fixed size,
 * round-robin overwrite when full. Bounded rather than dynamic because
 * this backend never has more than a handful of foreign-process handles
 * open at once in practice (one per in-flight getpriority()/
 * setpriority()/process_exists() call, each closed again before the
 * next), and a stale, overwritten entry only ever produces a safe
 * ESRCH-on-lookup-miss for __plat_priority_get()/_set(), never a wrong
 * answer for the wrong pid -- pidfd_pid_table[] stores a pidfd, and a
 * closed pidfd's slot being reused for an unrelated later pidfd number
 * cannot happen while the original handle is still open (this table
 * gains an entry only when __plat_process_open{,_checked}() hands out a
 * live pidfd, and a stale entry for an already-closed pidfd is simply
 * never looked up again by anything holding the closed handle). */
#define PIDFD_TABLE_MAX 32
static struct { int pidfd; pid_t pid; int used; } pidfd_pid_table[PIDFD_TABLE_MAX];
static int pidfd_table_next;

static void record_pidfd_pid(int pidfd, pid_t pid)
{
	pidfd_pid_table[pidfd_table_next].pidfd = pidfd;
	pidfd_pid_table[pidfd_table_next].pid = pid;
	pidfd_pid_table[pidfd_table_next].used = 1;
	pidfd_table_next = (pidfd_table_next + 1) % PIDFD_TABLE_MAX;
}

/* -1 if `pidfd` was never recorded (or its slot has since been
 * overwritten) by this file's own open functions -- see this file's
 * banner for why that is a safe "not found" rather than a wrong pid. */
static pid_t pid_for_pidfd(int pidfd)
{
	int i;
	for (i = 0; i < PIDFD_TABLE_MAX; i++)
		if (pidfd_pid_table[i].used && pidfd_pid_table[i].pidfd == pidfd)
			return pidfd_pid_table[i].pid;
	return (pid_t)-1;
}

/* kill(pid, 0): existence/permission checked, no signal sent
 * (kill.html) -- the real POSIX idiom sched.c's process_exists() wants,
 * and one that hands back the exact [EPERM]-vs-[ESRCH] distinction NT's
 * generic-status-vs-STATUS_ACCESS_DENIED narrowing (src/misc/nt/
 * plat_misc.c's open_process()) has to reconstruct. pidfd_open(2) does
 * NOT perform this same up-front permission check (it only requires the
 * pid to exist at all -- a real permission check happens later, at
 * signal-send time), so this probe is still needed even once the
 * caller goes on to open a pidfd. */
static int probe_pid(pid_t pid)
{
	long ret = syscall(SYS_kill, (long)pid, 0L);
	if (is_sys_error(ret)) return ((int)-ret == EPERM) ? EPERM : ESRCH;
	return 0;
}

void __plat_yield(void)
{
	syscall(SYS_sched_yield);
}

/* Common to both open functions: probe (when `checked`), then
 * pidfd_open(2) for a real, closeable handle, boxed fd+1 like any other
 * Linux fd (see this file's banner for why that boxing is load-
 * bearing, not cosmetic) and recorded in the pid side table for the
 * priority functions below. */
/* out required: written unconditionally (`*out = box_fd((int)fd);`) on
 * the success path with no NULL check; both real callers below
 * forward their own now-required out with no guard of their own. */
static int open_process(pid_t pid, int checked, __plat_handle_t *out)
    __attribute__((nonnull(3)));
static int open_process(pid_t pid, int checked, __plat_handle_t *out)
{
	long fd;
	if (checked) {
		int e = probe_pid(pid);
		if (e) { errno = e; return -1; }
	}
	fd = syscall(SYS_pidfd_open, (long)pid, 0L);
	if (is_sys_error(fd)) { errno = checked ? (int)-fd : ESRCH; return -1; }
	record_pidfd_pid((int)fd, pid);
	*out = box_fd((int)fd);
	return 0;
}

int __plat_process_open_checked(pid_t pid, __plat_handle_t *out)
{
	return open_process(pid, 1, out);
}

int __plat_process_open(pid_t pid, __plat_handle_t *out)
{
	/* Not checked with a separate kill(pid, 0) probe first: this
	 * contract reports [ESRCH] uniformly for every failure anyway (no
	 * [EPERM] distinction to make, see plat_misc.h's own banner), and
	 * pidfd_open(2)'s own failure (ESRCH for a nonexistent pid) already
	 * gives exactly that answer directly. */
	if (open_process(pid, 0, out) < 0) { errno = ESRCH; return -1; }
	return 0;
}

int __plat_process_alive(__plat_handle_t h)
{
	/* NT's own check goes further than existence -- it reads
	 * ProcessBasicInformation.ExitStatus to exclude a reaped-but-still-
	 * openable process object (see plat_misc.h's own banner). Linux has
	 * no equivalent "openable but already gone" state to exclude: a
	 * pidfd still valid for pidfd_send_signal(fd, 0, ...) really is
	 * either a live process or a not-yet-reaped zombie, and POSIX
	 * process_exists() callers (the only caller here, src/misc/
	 * sched.c) mean "does this pid still name something in the process
	 * table", which a zombie still does. pidfd_send_signal(fd, 0, ...)
	 * is used rather than kill(pid, 0): this handle is a pidfd, and the
	 * pidfd form is immune to the original pid having been recycled by
	 * an unrelated later process in the meantime -- strictly more
	 * correct than a bare kill(pid, 0) on a raw pid would be here. */
	long ret = syscall(SYS_pidfd_send_signal, (long)unbox_fd(h), 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = ESRCH; return 0; }
	return 1;
}

int __plat_process_times_self(unsigned long long *user100ns, unsigned long long *kernel100ns)
{
	/* struct rusage's timeval fields (usec resolution) are this file's
	 * own portable stand-in for KERNEL_USER_TIMES's 100ns fields -- the
	 * caller (src/misc/resource.c's getrusage()) converts from the
	 * 100ns unit either way, so all this needs to do is scale usec up
	 * by 10. ntlibc's own struct rusage (include/sys/resource.h) is
	 * bit-identical to the raw getrusage(2) kernel ABI's own layout
	 * (ru_utime/ru_stime first, as struct timeval, matching musl/glibc
	 * both), so it can be handed straight to the raw syscall as its
	 * destination buffer with no translation struct of its own needed.
	 * RUSAGE_SELF (0) is the raw getrusage(2) `who` value. */
	struct rusage ru;
	long ret = syscall(SYS_getrusage, 0L /* RUSAGE_SELF */, &ru);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*user100ns = (unsigned long long)ru.ru_utime.tv_sec * 10000000ULL +
	             (unsigned long long)ru.ru_utime.tv_usec * 10ULL;
	*kernel100ns = (unsigned long long)ru.ru_stime.tv_sec * 10000000ULL +
	               (unsigned long long)ru.ru_stime.tv_usec * 10ULL;
	return 0;
}

int __plat_priority_get(__plat_handle_t h, int *nice_out)
{
	/* getpriority(2)'s raw kernel ABI returns 20-nice (range [1,40]) on
	 * success, never a negative value, so it cannot be confused with
	 * this syscall convention's [-4095,-1] error window -- see the
	 * kernel's sys_getpriority(): it deliberately biases the result up
	 * by 20 for exactly this reason, which glibc's own getpriority()
	 * wrapper undoes the same way this does. Unlike NT, this needs no
	 * base-priority-class mapping (include/sys/resource.h's own writeup
	 * of why NT needs one at all): getpriority(2) already speaks nice
	 * values natively -- it just needs a bare pid, not this handle's
	 * own pidfd, hence the side-table lookup (this file's banner). */
	long ret;
	pid_t pid = pid_for_pidfd(unbox_fd(h));
	if (pid < 0) { errno = ESRCH; return -1; }
	ret = syscall(SYS_getpriority, (long)0 /* PRIO_PROCESS */, (long)pid);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*nice_out = 20 - (int)ret;
	return 0;
}

int __plat_priority_set(__plat_handle_t h, int foreground, int nice_value)
{
	long ret;
	pid_t pid;
	/* `foreground` is PROCESS_PRIORITY_CLASS's Foreground bit
	 * (plat_misc.h) -- an NT-only concept (foreground-boost scheduling)
	 * with no Linux nice-value analog; src/misc/resource.c always
	 * passes 0 anyway (see its own comment on why this library never
	 * sets NT's bit either), so there is nothing to translate. */
	(void)foreground;
	pid = pid_for_pidfd(unbox_fd(h));
	if (pid < 0) { errno = ESRCH; return -1; }
	ret = syscall(SYS_setpriority, (long)0 /* PRIO_PROCESS */, (long)pid, (long)nice_value);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_priority_set_self(int foreground, int nice_value)
{
	(void)foreground;
	{
		long ret = syscall(SYS_setpriority, (long)0 /* PRIO_PROCESS */, 0L /* self */, (long)nice_value);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	}
	return 0;
}

int __plat_write_start_offset(__plat_handle_t h, int append, long long *out)
{
	/* `h` here is an fd handle (src/unistd/linux/plat_fd.c's own fd+1
	 * boxing), NOT a process handle -- this is the one function in this
	 * header that plat_fd.h's own __plat_seek_query() would otherwise
	 * duplicate exactly (see plat_misc.h's own comment on why they stay
	 * separate: this one must never touch errno). append asks for
	 * SEEK_END, otherwise SEEK_CUR -- exactly lseek(2)'s own two modes,
	 * with the offset argument 0 either way. Deliberately does NOT use
	 * plat_fd.h's __plat_seek_query()/unbox(), which is a different
	 * translation unit with no shared helper between them; the fd+1
	 * boxing convention is fixed by src/internal/libc.h's struct __fd
	 * across every backend, so re-deriving it here is not a guess. */
	int fd = (int)((long)h - 1);
	long ret = syscall(SYS_lseek, (long)fd, 0L, append ? 2L /* SEEK_END */ : 1L /* SEEK_CUR */);
	if (is_sys_error(ret)) return -1;
	*out = (long long)ret;
	return 0;
}

/* NT's lazily-created job object (src/misc/nt/plat_misc.c's
 * ensure_job()/job_handle) exists purely to give NT -- which otherwise
 * has no notion of "this process's own resource limits" -- something to
 * attach RLIMIT_{NPROC,CPU,AS,DATA} to. Linux already has real,
 * kernel-enforced per-process rlimits with no such indirection needed
 * at all: prlimit64(2) on pid 0 (self) sets them directly, one syscall
 * per resource, matching this backend's general "does not exist on
 * Linux at all" pattern for NT-only indirection layers (see src/mman/
 * linux/plat_mem.c's own banner for the precedent). RLIM_INFINITY here
 * is ntlibc's own (include/sys/resource.h: `(~0ULL)`), which is bit-
 * identical to the raw prlimit64(2) ABI's own RLIM64_INFINITY, so no
 * translation is needed for that value either -- passed straight
 * through, matching this project's already-established "ntlibc's own
 * constant VALUES already match the real Linux kernel ABI" pattern for
 * PROT_/MAP_/O_* elsewhere.
 *
 * RLIMIT_{CPU,DATA,NPROC,AS} (0, 2, 6, 9 -- include/sys/resource.h) are
 * also already the real Linux kernel ABI's own numbering
 * (asm-generic/resource.h), confirmed by reading that header, not
 * assumed: no translation needed for the resource numbers either. */
#define RLIMIT_CPU_LX   0
#define RLIMIT_DATA_LX  2
#define RLIMIT_NPROC_LX 6
#define RLIMIT_AS_LX    9

static void apply_one(int resource, rlim_t cur)
{
	unsigned long long lim[2]; /* struct rlimit64 { rlim64_t cur, max; } */
	lim[0] = (unsigned long long)cur;
	lim[1] = (unsigned long long)cur; /* soft==hard: this backend only ever
	                                    * pushes the current soft value,
	                                    * exactly like the NT backend's
	                                    * eli.BasicLimitInformation does. */
	syscall(SYS_prlimit64, 0L /* self */, (long)resource, lim, 0L);
}

void __plat_job_apply_limits(rlim_t nproc_cur, rlim_t cpu_cur, rlim_t as_cur, rlim_t data_cur)
{
	/* Best-effort, like the NT backend: prlimit64(2) can only ever
	 * LOWER RLIMIT_NPROC/AS/DATA's hard limit here (both cur and max are
	 * pushed to the same value), and an unprivileged process cannot
	 * raise its own hard limit back up -- setrlimit()'s own front door
	 * (src/misc/resource.c) already enforces "new rlim_cur <= new
	 * rlim_max" itself before this is ever reached, so within a single
	 * process's lifetime this can only ratchet down, never up, matching
	 * real Linux setrlimit() semantics for an unprivileged caller
	 * exactly (nothing NT-specific to preserve here at all). Failure is
	 * silently ignored, same as the NT backend: getrlimit() never asks
	 * the kernel to confirm what was actually accepted (see
	 * resource.c's own comment). */
	if (nproc_cur != RLIM_INFINITY) apply_one(RLIMIT_NPROC_LX, nproc_cur);
	if (cpu_cur != RLIM_INFINITY) apply_one(RLIMIT_CPU_LX, cpu_cur);
	if (as_cur != RLIM_INFINITY) apply_one(RLIMIT_AS_LX, as_cur);
	if (data_cur != RLIM_INFINITY) apply_one(RLIMIT_DATA_LX, data_cur);
}
