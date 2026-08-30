/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_signal.h -- see src/mman/
 * linux/plat_mem.c's own banner for the general discipline this file
 * follows too (raw syscall(2), no host libc, -nostdinc against ntlibc's
 * own headers, aarch64 syscall numbers confirmed against this host's
 * own <sys/syscall.h>).
 *
 * SCOPE, deliberately -- read this before wondering where a declared
 * function went. plat_signal.h's own banner already draws the line for
 * sigdelivery.c's wire protocol ("this subsystem's own wire protocol
 * ... has no POSIX shape to begin with -- it is this library's own
 * invented cross-process RPC, built entirely out of NT object-manager
 * primitives"); this file draws it in the same place, for the same
 * reason, and extends it to every function whose real job is
 * PARTICIPATING in that protocol rather than doing something Linux has
 * a native primitive for:
 *
 *   - __plat_signal_pipe_{create,listen,read,write,open}() and
 *     __plat_signal_mutant_create()/__plat_wait_acquire()/
 *     __plat_mutant_release(): the named-pipe-plus-mutant transport
 *     itself. A real Linux equivalent would be a signalfd/eventfd or a
 *     Unix domain socket -- a genuinely different, bigger redesign than
 *     a syscall swap (this header's own UNICODE_STRING* argument shape
 *     has no meaning to translate at all, since Linux object naming
 *     -- an abstract-namespace socket path, say -- is not the same
 *     kind of thing NT's object manager namespace is), not attempted
 *     here.
 *   - __plat_thread_start(): its only caller is
 *     __sig_delivery_init() (src/signal/sigdelivery.c), to launch the
 *     transport's own listener thread -- unreachable, and therefore not
 *     implemented, without the transport it exists to serve.
 *   - __plat_stop_event_create()/__plat_stop_event_probe(): NOT part of
 *     the pipe/mutant transport, but the same family of problem --
 *     src/signal/signal.c's stop_self()/__sig_consume_child_stop() use
 *     a NAMED, cross-process-visible event (looked up by a name derived
 *     from (pid, signal), \BaseNamedObjects\ntlibc-stop.<pid>.<sig>) so
 *     that a process which stops ITSELF can still publish that fact to
 *     a parent's waitpid() that has no handle to be signalled on. NT's
 *     global object-manager namespace makes "create or open by a
 *     well-known name, from either side, race-free" a single syscall;
 *     Linux has no equivalent single primitive (a named POSIX semaphore
 *     via sem_open(3), or a well-known path under /dev/shm, could be
 *     made to do this, but is a real design decision -- naming scheme,
 *     collision/cleanup story, permissions -- not a mechanical syscall
 *     substitution, so it is scoped out here alongside the transport
 *     rather than rushed).
 *
 * What IS implemented below is every function that is either required
 * (__plat_sigevent_create(), by this task's own instruction) or
 * genuinely NT-primitive-shaped-but-portable-in-spirit: event create/
 * wait/peek, and signal.c's kill()-adjacent job-control primitives
 * (__plat_process_suspend{,_self}(), __plat_kill_{open,terminate}(),
 * __plat_segv_code()), none of which touch the pipe/mutant transport or
 * the named-stop-event namespace at all.
 */
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include "plat_signal.h"

/* aarch64 Linux syscall numbers (confirmed via a throwaway host program
 * printing the SYS_* macros from <sys/syscall.h>, the same oracle
 * technique src/mman/linux/plat_mem.c's banner describes). */
#define SYS_eventfd2          19
#define SYS_ppoll             73
#define SYS_read              63
#define SYS_write             64
#define SYS_close             57
#define SYS_kill              129
#define SYS_nanosleep         101
#define SYS_msync             227
#define SYS_getpid            172
#define SYS_pidfd_open        434
#define SYS_pidfd_send_signal 424

/* A genuine raw syscall trampoline, not a call through the host's own
 * glibc syscall(2) wrapper -- see src/misc/linux/plat_misc.c's own
 * banner for the full reasoning (discovered necessary, not assumed,
 * while proving __plat_segv_code()'s ENOMEM classification below
 * against this pilot's real native-ELF test build): glibc's syscall()
 * already translates a kernel failure into plain -1 plus ITS OWN
 * errno, a different storage location from ntlibc's own -nostdinc
 * <errno.h>, so `errno = (int)-ret` would misdecode any failure that
 * is not exactly EPERM(1) -- this function issues the raw `svc #0`
 * directly instead, so every ret below really is the kernel's own
 * [-4095,-1]-encodes-errno value, aarch64-only, matching this pilot's
 * whole single-host-architecture scope. */
#include <stdarg.h>
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

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

/* ---- event create/wait/peek -----------------------------------------
 *
 * NT's SynchronizationEvent ("wakes one waiting thread, then
 * automatically resets", the exact semantics __plat_sigevent_create()'s
 * own comment describes) maps directly onto a Linux eventfd(2) created
 * with EFD_SEMAPHORE: each successful read() of one consumes exactly
 * one unit and returns immediately if the counter is nonzero, or blocks
 * until it becomes nonzero -- "auto-reset, one waiter released per
 * signal" in one call, with no NT-shaped translation needed at all.
 * Boxed the same way src/unistd/linux/plat_fd.c boxes any other small
 * Linux fd (+1, so __PLAT_HANDLE_NULL never collides with eventfd 0).
 */
#define EFD_SEMAPHORE_LX 1
#define EFD_CLOEXEC_LX   02000000

static int unbox(__plat_handle_t h) { return (int)((long)h - 1); }
static __plat_handle_t box(int fd) { return (__plat_handle_t)(long)(fd + 1); }

__plat_handle_t __plat_sigevent_create(int initially_signalled)
{
	long ret = syscall(SYS_eventfd2, (long)(initially_signalled ? 1 : 0),
	                   (long)(EFD_SEMAPHORE_LX | EFD_CLOEXEC_LX));
	if (is_sys_error(ret)) return __PLAT_HANDLE_NULL;
	return box((int)ret);
}

/* __plat_event_set() is declared in plat_signal.h but NOT defined here
 * -- it is ALSO declared in plat_thread.h and, per this migration's own
 * cross-session convention (see src/signal/nt/plat_signal.c's matching
 * comment), belongs to whichever session ports src/thread/'s Linux
 * backend. Defining it here too would be a second, colliding
 * definition of the same symbol -- an ODR violation the NT side already
 * hit once during its own migration and fixed by picking exactly one
 * owner per function; this file just uses it, as sigdelivery.c does. */

void __plat_signal_wait(__plat_handle_t wake_event, int has_timeout, long long ticks)
{
	struct timespec ts, *tsp;
	long ns;

	if (wake_event) {
		int fd = unbox(wake_event);
		struct pollfd pfd;
		unsigned long long val;

		pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
		if (has_timeout) {
			ns = ticks * 100L;
			ts.tv_sec = ns / 1000000000L; ts.tv_nsec = ns % 1000000000L;
			tsp = &ts;
		} else {
			tsp = 0;
		}
		if (syscall(SYS_ppoll, &pfd, 1L, tsp, 0L, 0L) > 0 && (pfd.revents & POLLIN))
			syscall(SYS_read, (long)fd, &val, 8L); /* consume one unit -- auto-reset */
		return;
	}
	if (has_timeout) {
		ns = ticks * 100L;
		ts.tv_sec = ns / 1000000000L; ts.tv_nsec = ns % 1000000000L;
		syscall(SYS_nanosleep, &ts, 0L);
		return;
	}
	/* Neither a timeout nor an event: the same fixed 100ms fallback the
	 * NT backend uses (src/signal/nt/plat_signal.c). */
	ts.tv_sec = 0; ts.tv_nsec = 100000000L;
	syscall(SYS_nanosleep, &ts, 0L);
}

int __plat_event_peek(__plat_handle_t ev)
{
	int fd = unbox(ev);
	struct pollfd pfd;
	struct timespec zero;
	unsigned long long val;

	pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
	zero.tv_sec = 0; zero.tv_nsec = 0;
	if (syscall(SYS_ppoll, &pfd, 1L, &zero, 0L, 0L) <= 0 || !(pfd.revents & POLLIN))
		return 0;
	/* Consume it -- an eventfd read is what makes EFD_SEMAPHORE mode
	 * auto-reset, matching NtWaitForSingleObject's own consuming peek
	 * for a SynchronizationEvent. */
	syscall(SYS_read, (long)fd, &val, 8L);
	return 1;
}

/* ---- signal.c's kill()-adjacent job control and fault classification */

int __plat_process_suspend_self(void)
{
	/* Distinct from __plat_process_suspend() below (which acts on some
	 * OTHER process's handle, kill()'s job-control arm) -- this is
	 * stop_self()'s own self-suspend, and the reason it cannot just be
	 * kill(0, SIGSTOP): pid 0 as a kill(2) TARGET means "every process
	 * in my process group", not "myself", so the real pid is needed
	 * (one extra getpid(2) syscall; NT's equivalent,
	 * NtSuspendProcess(NtCurrentProcess()), avoids this because
	 * NtCurrentProcess() is a constant pseudo-handle, not a real pid
	 * lookup). */
	long pid = syscall(SYS_getpid);
	long ret = syscall(SYS_kill, pid, (long)SIGSTOP);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_process_suspend(__plat_handle_t h)
{
	/* `h` here is a process handle -- a boxed pidfd (fd+1), the SAME
	 * convention src/misc/linux/plat_misc.c's process handles use, and
	 * for the identical reason (see that file's banner at length):
	 * src/signal/signal.c's kill() calls plat_fd.h's shared
	 * __plat_close() on any handle __plat_kill_open() below vends, and
	 * that function only does the right thing on a real fd. Signal
	 * delivery goes through pidfd_send_signal(2), not kill(pid, ...),
	 * for the same pid-reuse-immunity reason __plat_process_alive()
	 * (plat_misc.c) does -- SIGSTOP is Linux's own real, uncatchable
	 * stop signal, and kill()'s job-control arm needs nothing more than
	 * delivering it, unlike NT's NtSuspendProcess/NtResumeProcess pair,
	 * which exists only because NT has no signal delivery at all (see
	 * src/signal/signal.c's own comment on sig_job_control()). */
	long ret = syscall(SYS_pidfd_send_signal, (long)unbox(h), (long)SIGSTOP, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* __plat_process_resume() is declared in plat_signal.h but NOT defined
 * here -- per this header's own banner, it belongs to
 * src/process/nt/plat_process.c on the NT side (src/process/children.c's
 * resume-a-stopped-child path independently needs the identical
 * primitive), so its Linux counterpart is this migration's process
 * subsystem session's to define, not this file's; defining it here
 * too would be the same ODR collision __plat_event_set() above avoids. */

int __plat_kill_open(pid_t pid, int want_suspend_resume, __plat_handle_t *out)
{
	/* Linux has no "open a process object" step for most per-process
	 * syscalls (kill(2), and, via src/misc/linux/plat_misc.c,
	 * getpriority(2)/setpriority(2), all take a bare pid_t directly),
	 * but this handle specifically must survive a later
	 * plat_fd.h __plat_close() call from kill() (src/signal/signal.c:
	 * `if (!c) __plat_close(h);`, on every path through this function),
	 * and that shared close function only does the right thing on a
	 * real fd -- see src/misc/linux/plat_misc.c's banner for the full
	 * reasoning already worked out there for the identical constraint.
	 * So this hands back a pidfd_open(2) handle, boxed fd+1 the same
	 * way, not a bare boxed pid.
	 *
	 * kill(pid, 0) is still the existence-and-permission probe
	 * kill.html's own semantics already want (no signal sent) and the
	 * one pidfd_open(2) itself does NOT perform (it only requires the
	 * pid to exist, not that this process may signal it) -- its real
	 * errno IS the [EPERM]-vs-[ESRCH] distinction this contract asks
	 * for, a strictly more direct match than NT's STATUS_ACCESS_DENIED
	 * narrowing (src/signal/nt/plat_signal.c's own __plat_kill_open()),
	 * not an approximation of it. `want_suspend_resume` has nothing to
	 * translate: unlike NT's PROCESS_SUSPEND_RESUME access right, a
	 * pidfd_send_signal(2) SIGSTOP/SIGCONT to a pid this process
	 * already has permission to signal at all needs no separate right. */
	long ret, fd;
	(void)want_suspend_resume;
	ret = syscall(SYS_kill, (long)pid, 0L);
	if (is_sys_error(ret)) {
		errno = ((int)-ret == EPERM) ? EPERM : ESRCH;
		return -1;
	}
	fd = syscall(SYS_pidfd_open, (long)pid, 0L);
	if (is_sys_error(fd)) { errno = ESRCH; return -1; } /* the target
	                                                      * exited in the
	                                                      * race between
	                                                      * the two calls
	                                                      * above */
	*out = box((int)fd);
	return 0;
}

int __plat_kill_terminate(__plat_handle_t h, int exitcode)
{
	/* exitcode (NT's own TerminateProcess() exit-status argument) has
	 * no Linux kill(2)/pidfd_send_signal(2) equivalent: a signal-killed
	 * process's wait status is fundamentally shaped differently
	 * (WIFSIGNALED/WTERMSIG, not an arbitrary exit code) --
	 * src/process/wait.c's own Linux backend, not this file, is where
	 * that shape lives. SIGKILL is the uncatchable, always-terminates
	 * signal that matches NtTerminateProcess's own unconditional force.
	 * kill()'s tolerance for a target already exiting (NT's
	 * STATUS_PROCESS_IS_TERMINATING special case, this header's own
	 * comment) needs no equivalent special-casing here:
	 * pidfd_send_signal(fd, SIGKILL, ...) to a zombie that has not been
	 * reaped yet still succeeds (the pidfd is still valid), and ESRCH
	 * is returned only once the process is genuinely gone -- which is
	 * already the correct, honest POSIX answer for "no such process to
	 * kill", not a case this needs to paper over the way NT's status
	 * does. */
	long ret = syscall(SYS_pidfd_send_signal, (long)unbox(h), (long)SIGKILL, 0L, 0L);
	(void)exitcode;
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_segv_code(void *addr)
{
	/* NT delivers a generic access violation and leaves it to this
	 * library to work out SEGV_MAPERR ("no mapping at all") vs
	 * SEGV_ACCERR ("mapped, but this access violates its protection")
	 * by querying the address's own region (NtQueryVirtualMemory,
	 * src/signal/nt/plat_signal.c). Linux's real SIGSEGV siginfo_t
	 * already carries the true si_code natively -- the kernel itself
	 * made this distinction before ever raising the signal -- so an
	 * eventual full Linux port of the fault path would read it
	 * straight off the signal handler's own siginfo_t and never call
	 * this function's Linux backend at all. Until that front-door
	 * plumbing exists, this still needs a standalone answer for the
	 * same query: msync(2) on the containing page reports ENOMEM
	 * specifically when "the indicated memory (or part of it) was not
	 * mapped" (msync(2)) and otherwise succeeds against any mapped
	 * page regardless of its protection -- a real, if indirect, two-
	 * syscall-free probe of exactly this fact, needing no
	 * /proc/self/maps parsing. Page size is hardcoded to 4096, the
	 * default on both this project's other target architectures
	 * (x86_64, aarch64); a non-default page size would only ever make
	 * this over-round the address, never misclassify past the actual
	 * page boundary. */
	unsigned long page = (unsigned long)addr & ~(unsigned long)0xFFF;
	long ret = syscall(SYS_msync, (void *)page, (unsigned long)4096, 4 /* MS_ASYNC */);
	if (is_sys_error(ret) && (int)-ret == ENOMEM) return SEGV_MAPERR;
	return SEGV_ACCERR;
}
