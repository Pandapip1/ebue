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
 *   - __plat_thread_start(): its only caller was
 *     __sig_delivery_init() in the NT-only sigdelivery.c (src/signal/
 *     nt/sigdelivery.c, after this migration's own relocation), to
 *     launch the transport's own listener thread -- unreachable, and
 *     therefore not implemented, without the transport it exists to
 *     serve. src/signal/linux/sigdelivery.c's own real, portable
 *     __sig_delivery_init() needs no such thread at all.
 *
 * UPDATE, since the paragraph above was first written:
 * __plat_stop_event_create()/__plat_stop_event_probe() ARE now
 * implemented below, using a real (if scoped-down) design: the
 * filesystem namespace under /tmp, shared by every process on this
 * host exactly like \BaseNamedObjects is, plus O_CREAT|O_EXCL for
 * atomic create-vs-open detection and a MAP_SHARED mapping of a small
 * backing file so every process that opens the same path sees the
 * SAME futex word -- see src/thread/linux/plat_thread.c's own copy of
 * this same technique (named semaphores) for the fuller writeup of the
 * approach and its one disclosed race (a second opener's mmap can, in
 * principle, race the creator's ftruncate()).
 *
 * What IS implemented below is every function that is either required
 * (__plat_sigevent_create(), by this task's own instruction) or
 * genuinely NT-primitive-shaped-but-portable-in-spirit: event create/
 * wait/peek, signal.c's kill()-adjacent job-control primitives
 * (__plat_process_suspend{,_self}(), __plat_kill_{open,terminate}(),
 * __plat_segv_code()), and now the named stop-event pair -- none of
 * which touch the still-unimplemented pipe/mutant transport at all.
 *
 * UPDATE, since the paragraph above was first written: __plat_kill_open(),
 * __plat_process_suspend() and __plat_kill_terminate() below used to box
 * their process handle as fd+1, this file's OWN event-handle convention,
 * on the assumption that a process handle was always a fresh
 * pidfd_open(2) result. It is not: src/signal/signal.c's kill() also
 * feeds these functions `h` straight from struct __child's own .h field
 * for a tracked child, and src/process/linux/plat_process.c's box_pid()
 * sets that to the bare pid, no offset (that file's own banner states
 * the convention outright). Two functions sharing one argument,
 * disagreeing about its encoding, is exactly the bug that let
 * killpg/1-2.c (third_party/ltp's OPEN POSIX suite) leave an orphaned
 * child spinning in sigsuspend() forever: the SIGUSR1 meant to wake it
 * went through __plat_kill_terminate() with `h` misread as fd+1, handed
 * pidfd_send_signal(2) a garbage descriptor number, failed EBADF, and
 * nothing ever retried. Fixed by making these three functions agree with
 * plat_process.c's own choice instead of keeping a second, silently
 * incompatible one -- see __plat_process_suspend()'s own comment for the
 * detail and the one hazard this reopens (already disclosed and already
 * accepted, for the identical reason, by plat_process.c's own banner).
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include "libc.h"       /* struct _UNICODE_STRING's real definition (nt.h) --
                         * a type-only use, same as every other Linux backend
                         * in this tree that reads an NT-shaped struct passed
                         * across a still-NT-shaped seam; see plat_signal.h's
                         * own banner on why this one is unavoidable. */
#include "plat_signal.h"
#include "linux/sync.h"

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
#define SYS_mmap_ps           222
#define PROT_READ_PS          0x1
#define PROT_WRITE_PS         0x2

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

void __plat_signal_wait(__plat_handle_t wake_event, int has_timeout, long long ticks) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; timeout flag and duration have distinct roles
{
	struct timespec ts, *tsp;
	long long magnitude, ns;

	/* `ticks` carries NT's own relative/absolute LARGE_INTEGER encoding
	 * (plat_signal.h's own comment on this function: "passed through
	 * unchanged") -- negative means "relative, this many 100ns units
	 * from now", positive means "absolute NT time". Every real caller
	 * that reaches this backend (src/unistd/sleep.c's __alertable_delay(),
	 * signal.c's sigtimedwait()-shaped waits) only ever constructs a
	 * relative (negative) value -- confirmed by grep across every
	 * __sig_wait_delivery() call site in this tree, all of which pass
	 * either `-something` or a null timeout, never a raw positive
	 * deadline -- so decoding "negative" is the one real case; a
	 * genuinely absolute (positive) `ticks` has no Linux-native
	 * equivalent built here yet (it would need converting through
	 * __plat_query_system_time() to a relative offset first, unlike
	 * NtWaitForSingleObject()/NtDelayExecution(), which understand the
	 * absolute form natively), same class of disclosed, narrower-than-
	 * the-full-contract gap as this file's own banner already lists for
	 * the pipe/mutant transport. What is fixed here is a real,
	 * confirmed bug, not a new gap: this function used to pass `ticks`
	 * straight through to the `ns = ticks * 100L` conversion below with
	 * no sign handling at all, unlike src/thread/linux/plat_thread.c's
	 * own __plat_wait_one() (`ticks = relative_ticks < 0 ?
	 * -relative_ticks : relative_ticks`), which decodes the identical
	 * convention correctly. A relative (negative) `ticks` therefore
	 * produced a NEGATIVE ts.tv_sec/ts.tv_nsec handed straight to the
	 * real nanosleep(2)/ppoll(2) syscalls below, which the kernel
	 * rejects outright (EINVAL) instead of sleeping at all -- confirmed
	 * with strace against a real sleep(1) call reaching this function
	 * through __alertable_delay(): `nanosleep({tv_sec=-1, tv_nsec=0})
	 * = -1 EINVAL`, immediately, every time, never once actually
	 * sleeping. Silently ignoring that failure (this function has never
	 * checked the syscall's return value -- see the two bare
	 * `syscall(SYS_nanosleep, ...)` statements below) turned every
	 * timed __sig_wait_delivery() call into a zero-duration busy-spin:
	 * __alertable_delay()'s `while (ticks > 0)` loop calls this
	 * function, gets back instantly with nothing slept, subtracts
	 * whatever sub-microsecond amount __plat_time_now() advanced by,
	 * and calls again -- thousands of times over, per real second of
	 * requested sleep, pegging a CPU core instead of blocking. That is
	 * the real root cause behind the cluster of conformance-suite
	 * TIMEOUT results across fork, pthread_atfork, the sem_ family,
	 * mqueue, sigsuspend and sigwait: every one of those interfaces' test cases
	 * either calls sleep()/usleep() directly for parent/child
	 * synchronization (fork/1-1.c's own sleep(1), literally the case
	 * this bug was diagnosed against) or waits through this exact
	 * function's timed path (sigsuspend/sigwait/sigwaitinfo's own
	 * bounded waits, signal.c's sigtimedwait()-shaped loop above), and
	 * a thread pegged at 100% CPU failing to make timely progress is
	 * indistinguishable, from outside, from one that is genuinely
	 * hung. */
	magnitude = ticks < 0 ? -ticks : ticks;

	if (wake_event) {
		int fd = unbox(wake_event);
		struct pollfd pfd;
		unsigned long long val;

		pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
		if (has_timeout) {
			ns = magnitude * 100LL;
			ts.tv_sec = (time_t)(ns / 1000000000LL); ts.tv_nsec = (long)(ns % 1000000000LL);
			tsp = &ts;
		} else {
			tsp = 0;
		}
		if (syscall(SYS_ppoll, &pfd, 1L, tsp, 0L, 0L) > 0 && (pfd.revents & POLLIN))
			syscall(SYS_read, (long)fd, &val, 8L); /* consume one unit -- auto-reset */
		return;
	}
	if (has_timeout) {
		ns = magnitude * 100LL;
		ts.tv_sec = (time_t)(ns / 1000000000LL); ts.tv_nsec = (long)(ns % 1000000000LL);
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

/* `h` here is a process handle in src/process/linux/plat_process.c's own
 * domain -- the pid itself, cast straight through, NO offset (that
 * file's banner states the convention outright) -- NOT this file's own
 * box()/unbox() (fd+1), which is a DIFFERENT __plat_handle_t domain
 * belonging to event handles (__plat_sigevent_create() below): the two
 * happen to share a C type only because plat_signal.h/plat_process.h
 * inherited one universal `__plat_handle_t` typedef from the NT side,
 * where every kind of handle really is interchangeable.
 *
 * This was box()/unbox() (fd+1) here too, silently assuming a real
 * pidfd_open(2) result -- wrong on every path that actually matters:
 * signal.c's kill() populates `h` from struct __child's own .h field
 * (src/process/children.c), set at fork() time by
 * src/process/linux/plat_process.c's box_pid(), which is a documented
 * no-op (that file's own banner: "the pid itself, cast straight
 * through"). Feeding a raw pid through this file's fd+1 unbox() reads
 * pid-1 as an fd number and hands it to pidfd_send_signal(2), which
 * fails EBADF against whatever garbage descriptor that number names --
 * confirmed live: killpg/1-2.c (third_party/ltp's OPEN POSIX suite)
 * left an orphaned, un-signalable grandchild spinning forever in its own
 * sigsuspend() wait loop, because the SIGUSR1 delivery that was supposed
 * to wake it silently failed this way and the parent that would have
 * retried already exited. __plat_process_resume() (SIGCONT,
 * src/process/linux/plat_process.c) was ALREADY correct against this
 * exact `h` -- it is the pid-domain owner, unbox_pid() there is a plain
 * cast -- which is what exposed the split: two functions sharing one
 * argument, silently disagreeing about what it meant.
 *
 * Fixed by making every process-handle-consuming function in THIS file
 * (this one, __plat_kill_open(), __plat_kill_terminate() below) agree
 * with plat_process.c's own choice instead of inventing a second one:
 * `h` is the raw pid. A pidfd is opened here, used once, and closed --
 * still real pidfd_send_signal(2) delivery (SIGSTOP is uncatchable
 * regardless, but pidfd_send_signal keeps the same pid-reuse-immunity
 * property __plat_kill_open()'s existence probe already relies on,
 * rather than quietly downgrading to plain kill(2) the way
 * __plat_process_resume() already, separately, does). */
int __plat_process_suspend(__plat_handle_t h)
{
	long pid = (long)(int)(long)h;
	long fd = syscall(SYS_pidfd_open, pid, 0L);
	long ret;
	if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	ret = syscall(SYS_pidfd_send_signal, fd, (long)SIGSTOP, 0L, 0L);
	syscall(SYS_close, fd, 0L, 0L, 0L, 0L, 0L);
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

int __plat_kill_open(pid_t pid, int want_suspend_resume, __plat_handle_t *out) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; process ID and capability flag have distinct roles
{
	/* Linux has no "open a process object" step for most per-process
	 * syscalls (kill(2), and, via src/misc/linux/plat_misc.c,
	 * getpriority(2)/setpriority(2), all take a bare pid_t directly).
	 *
	 * This used to hand back a pidfd_open(2) handle, boxed fd+1, on the
	 * reasoning that the result has to survive a later plat_fd.h
	 * __plat_close() call from kill() (src/signal/signal.c:
	 * `if (!c) __plat_close(h);`) which only does the right thing on a
	 * real fd. That reasoning was sound for THAT one call site but broke
	 * every other consumer of this same __plat_handle_t: kill()'s other
	 * paths (sig_job_control(), __plat_kill_terminate() below) also
	 * receive `h` from struct __child's own .h field for a TRACKED
	 * child, which src/process/linux/plat_process.c's box_pid() sets to
	 * the bare pid, no offset (that file's own banner states the
	 * convention outright) -- so the same downstream functions were
	 * being fed fd+1 on one path and a bare pid on the other, silently
	 * disagreeing about what their own argument meant. See
	 * __plat_process_suspend()'s updated comment above for the live
	 * failure this produced (an orphaned, un-signalable child in
	 * killpg/1-2.c) and the fix: every process-handle consumer in this
	 * file now agrees with plat_process.c's choice -- `h` is the bare
	 * pid -- rather than each function guessing its own encoding.
	 *
	 * That does reopen the __plat_close() hazard this used to dodge: a
	 * bare pid handed to plat_fd.h's fd-domain close() reads pid-1 as an
	 * fd number and closes whatever real descriptor happens to have that
	 * value, if any. src/process/linux/plat_process.c's own banner
	 * already accepts the identical risk for struct __child's .h field
	 * (mark_children_inheritable()/__child_remove() call the same
	 * fd-domain __plat_dup()/__plat_close() on a bare-pid handle today)
	 * with the same disclosed reasoning: real pids on this host run past
	 * a million (that file's own report), so pid-1 reliably lands on an
	 * fd number this small a process never opened, and the close fails
	 * silently EBADF rather than closing something real. A coincidence
	 * of scale, not a proof, exactly as that banner says -- and now the
	 * SAME coincidence this function also leans on, rather than a new
	 * and different one.
	 *
	 * kill(pid, 0) is still the existence-and-permission probe
	 * kill.html's own semantics already want (no signal sent) -- its
	 * real errno IS the [EPERM]-vs-[ESRCH] distinction this contract
	 * asks for, a strictly more direct match than NT's
	 * STATUS_ACCESS_DENIED narrowing (src/signal/nt/plat_signal.c's own
	 * __plat_kill_open()), not an approximation of it.
	 * `want_suspend_resume` has nothing to translate: unlike NT's
	 * PROCESS_SUSPEND_RESUME access right, signalling a pid this process
	 * already has permission to signal at all needs no separate right. */
	long ret;
	(void)want_suspend_resume;
	ret = syscall(SYS_kill, (long)pid, 0L);
	if (is_sys_error(ret)) {
		errno = ((int)-ret == EPERM) ? EPERM : ESRCH;
		return -1;
	}
	*out = (__plat_handle_t)(long)pid;
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
	 * does.
	 *
	 * `h` is the bare pid, same as __plat_process_suspend() above and
	 * for the identical reason (see that function's updated comment) --
	 * a fresh pidfd is opened, used once for the kill, and closed. */
	long pid = (long)(int)(long)h;
	long fd = syscall(SYS_pidfd_open, pid, 0L);
	long ret;
	(void)exitcode;
	if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	ret = syscall(SYS_pidfd_send_signal, fd, (long)SIGKILL, 0L, 0L);
	syscall(SYS_close, fd, 0L, 0L, 0L, 0L, 0L);
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

/* ---- named stop-events, keyed by the filesystem namespace ----------------
 * See this file's own updated banner. `name`'s wide chars are ASCII by
 * construction (signal.c's own stop_event_name() builds them from a
 * fixed prefix plus hex digits), so narrowing byte-by-byte is exact,
 * not an approximation. */
#define SYS_openat_ps    56
#define SYS_ftruncate_ps 46
#define AT_FDCWD_PS      (-100)
#define O_RDWR_PS        02
#define O_CREAT_PS       0100
#define O_EXCL_PS        0200
#define MAP_SHARED_PS    0x01

/* name is required: name->Length is dereferenced unconditionally at
 * entry with no guard, and this file's own two real call sites
 * (open_shared_stop_event(), forwarded in turn from
 * __plat_stop_event_create()/__plat_stop_event_probe()) always supply
 * signal.c's own stop_event_name()-built &us, never NULL. buf is left
 * unmarked -- writes into it go through buf[j++], guarded at every
 * step by `j < bufsz - 1`, the same "extent, not nullness" class of
 * fact 9be895e's own frexp precedent already distinguishes. */
static void stop_event_path(const struct _UNICODE_STRING *name, char *buf, size_t bufsz)
    __attribute__((nonnull(1)));
static void stop_event_path(const struct _UNICODE_STRING *name, char *buf, size_t bufsz)
{
	static const char prefix[] = "/tmp/.ntlibc-stopev.";
	size_t plen = sizeof(prefix) - 1, i, j = 0;
	size_t n = name->Length / sizeof(unsigned short);
	for (i = 0; i < plen && j < bufsz - 1; i++) buf[j++] = prefix[i];
	for (i = 0; i < n && j < bufsz - 1; i++) {
		unsigned short c = name->Buffer[i];
		buf[j++] = (c == '\\' || c == 0) ? '_' : (char)c;
	}
	buf[j] = 0;
}

/* Opens-or-creates the backing file for `name` and hands back its
 * MAP_SHARED mapping plus whether THIS call created it. See
 * src/thread/linux/plat_thread.c's map_named_sem() for the identical
 * technique and its one disclosed race (a second opener's mmap can, in
 * principle, race the creator's ftruncate()) -- not repeated here. */
static int open_shared_stop_event(const struct _UNICODE_STRING *name, int *created,
                                  struct ntlibc_linux_sync **out)
{
	char path[128];
	long fd, r;

	stop_event_path(name, path, sizeof path);
	fd = syscall(SYS_openat_ps, (long)AT_FDCWD_PS, (long)path,
	            (long)(O_RDWR_PS | O_CREAT_PS | O_EXCL_PS), 0600L, 0L, 0L);
	if (is_sys_error(fd)) {
		*created = 0;
		fd = syscall(SYS_openat_ps, (long)AT_FDCWD_PS, (long)path, (long)O_RDWR_PS, 0L, 0L, 0L);
		if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	} else {
		*created = 1;
		syscall(SYS_ftruncate_ps, fd, (long)sizeof(struct ntlibc_linux_sync), 0L, 0L, 0L, 0L);
	}
	r = syscall(SYS_mmap_ps, 0L, (long)sizeof(struct ntlibc_linux_sync),
	           (long)(PROT_READ_PS | PROT_WRITE_PS), (long)MAP_SHARED_PS, fd, 0L);
	syscall(SYS_close, fd, 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(r)) { errno = (int)-r; return -1; }
	*out = (struct ntlibc_linux_sync *)r;
	if (*created) { (*out)->futex = 0; (*out)->max = 0; (*out)->kind = 2 /* NTLIBC_LX_SYNC_EVENT */; }
	return 0;
}

__plat_handle_t __plat_stop_event_create(const struct _UNICODE_STRING *name)
{
	struct ntlibc_linux_sync *obj;
	int created;
	if (open_shared_stop_event(name, &created, &obj) < 0) return __PLAT_HANDLE_NULL;
	return (__plat_handle_t)obj;
}

int __plat_stop_event_probe(const struct _UNICODE_STRING *name, __plat_handle_t *out,
                            int *already_existed)
{
	struct ntlibc_linux_sync *obj;
	int created;
	if (open_shared_stop_event(name, &created, &obj) < 0) return -1;
	*out = (__plat_handle_t)obj;
	*already_existed = !created;
	return 0;
}

// NOLINTEND(misc-include-cleaner)
