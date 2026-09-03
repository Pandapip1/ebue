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
 *   - __plat_thread_start(): its only caller is __sig_delivery_init()
 *     in the NT-only sigdelivery.c (src/signal/nt/sigdelivery.c), to
 *     launch the transport's own listener thread -- unreachable, and
 *     therefore not implemented, without the transport it exists to
 *     serve. src/signal/linux/sigdelivery.c's own real, portable
 *     __sig_delivery_init() needs no such thread at all.
 *
 * What IS implemented below is every function that is either required
 * (__plat_sigevent_create()) or genuinely NT-primitive-shaped-but-
 * portable-in-spirit: event create/wait/peek, signal.c's kill()-adjacent
 * job-control primitives (__plat_process_suspend{,_self}(),
 * __plat_kill_{open,terminate}(), __plat_segv_code()), and the named
 * stop-event pair -- __plat_stop_event_create()/__plat_stop_event_probe(),
 * using a real (if scoped-down) design: the filesystem namespace under
 * /tmp, shared by every process on this host exactly like
 * \BaseNamedObjects is, plus O_CREAT|O_EXCL for atomic create-vs-open
 * detection and a MAP_SHARED mapping of a small backing file so every
 * process that opens the same path sees the SAME futex word (see
 * src/thread/linux/plat_thread.c's own copy of this same technique --
 * named semaphores -- for the fuller writeup of the approach and its
 * one disclosed race: a second opener's mmap can, in principle, race
 * the creator's ftruncate()) -- none of which touch the still-
 * unimplemented pipe/mutant transport at all.
 *
 * __plat_kill_open(), __plat_process_suspend(), and
 * __plat_kill_terminate() below box their process handle as the bare
 * pid, matching src/process/linux/plat_process.c's own box_pid()
 * convention (that file's own banner states it outright) -- NOT fd+1,
 * this file's own event-handle convention elsewhere. Getting this
 * wrong is a real, confirmed bug, not a theoretical one:
 * src/signal/signal.c's kill() feeds these functions `h` straight from
 * struct __child's own .h field for a tracked child, which box_pid()
 * sets to the bare pid with no offset, so an fd+1 reading here would
 * misdecode it. That mismatch is exactly what let killpg/1-2.c
 * (third_party/ltp's OPEN POSIX suite) leave an orphaned child
 * spinning in sigsuspend() forever: the SIGUSR1 meant to wake it went
 * through __plat_kill_terminate() with `h` misread as fd+1, handed
 * pidfd_send_signal(2) a garbage descriptor number, failed EBADF, and
 * nothing ever retried. See __plat_process_suspend()'s own comment for
 * the one hazard this convention reopens (already disclosed and
 * already accepted, for the identical reason, by plat_process.c's own
 * banner).
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <fcntl.h>
#include "libc.h"       /* struct _UNICODE_STRING's real definition (nt.h) --
                         * a type-only use, same as every other Linux backend
                         * in this tree that reads an NT-shaped struct passed
                         * across a still-NT-shaped seam; see plat_signal.h's
                         * own banner on why this one is unavoidable. */
#include "plat_signal.h"
#include "linux/sync.h"

/* Linux syscall numbers -- aarch64 confirmed via a throwaway host program
 * printing the SYS_* macros from <sys/syscall.h>, the same oracle
 * technique src/mman/linux/plat_mem.c's banner describes; x86_64
 * confirmed against a real x86_64-linux-gnu glibc's own asm/unistd_64.h
 * (NOT the aarch64/generic "modern ABI" numbering src/fcntl/linux/
 * plat_fcntl.c's own banner already warns is a completely different
 * table from x86_64's legacy-derived one -- e.g. SYS_read is 63 on
 * aarch64 but 0 on x86_64; confirmed independently for every number
 * below, not assumed to be offset by some fixed amount from aarch64's). */
#if defined(__aarch64__)
#define SYS_eventfd2          19
#define SYS_ppoll             73
#define SYS_read              63
#define SYS_write             64
#define SYS_close             57
#define SYS_kill              129
#define SYS_rt_sigaction      134
#define SYS_rt_sigprocmask    135
#define SYS_nanosleep         101
#define SYS_msync             227
#define SYS_getpid            172
#define SYS_pidfd_open        434
#define SYS_pidfd_send_signal 424
#define SYS_mmap_ps           222
#elif defined(__x86_64__)
#define SYS_eventfd2          290
#define SYS_ppoll             271
#define SYS_read              0
#define SYS_write             1
#define SYS_close             3
#define SYS_kill              62
#define SYS_rt_sigaction      13
#define SYS_rt_sigprocmask    14
#define SYS_nanosleep         35
#define SYS_msync             26
#define SYS_getpid            39
#define SYS_pidfd_open        434 /* pidfd_open/pidfd_send_signal are recent
                                    * enough syscalls to share the same
                                    * number across every arch's table --
                                    * confirmed, not assumed, against the
                                    * same x86_64 oracle header. */
#define SYS_pidfd_send_signal 424
#define SYS_mmap_ps           9
#elif defined(__i386__)
#define SYS_eventfd2          328
#define SYS_ppoll             309
#define SYS_read                3
#define SYS_write               4
#define SYS_close               6
#define SYS_kill               37
#define SYS_rt_sigaction      174
#define SYS_rt_sigprocmask    175
#define SYS_nanosleep         162
#define SYS_msync             144
#define SYS_getpid              20
#define SYS_pidfd_open        434
#define SYS_pidfd_send_signal 424
#define SYS_mmap_ps            192 /* SYS_mmap2, matching crt/linux/crt1.c's
                                    * own i386 choice and its own comment
                                    * on why (offset in PAGE units, moot
                                    * here -- every mmap call site below
                                    * passes offset 0). */
#else
#error "plat_signal.c: unsupported architecture (expected __aarch64__, __x86_64__ or __i386__)"
#endif
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
 * is not exactly EPERM(1) -- this function issues the raw `svc #0`/
 * `syscall` directly instead, so every ret below really is the kernel's
 * own [-4095,-1]-encodes-errno value. x86_64's own branch mirrors
 * crt/linux/crt1.c's own raw_syscall() banner for the per-arch calling-
 * convention rationale (r10, not rcx, for the 4th argument; rcx/r11
 * clobbered by `syscall` itself) -- duplicated here per this tree's own
 * "own syscall table per file" discipline, just against this file's own
 * variadic six-argument shape rather than a fixed-arity one. */
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
static long syscall(long number, ...)
{
	va_list ap;
	long a1, a2, a3, a4, a5, a6, ret;
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
#elif defined(__i386__)
/* i386's register-starved array-based syscall shape (see crt/linux/
 * crt1.c's own i386 raw_syscall() banner for the full rationale) --
 * duplicated here per this tree's own "own syscall table per file"
 * discipline, just against this file's own variadic six-argument shape
 * rather than a fixed-arity one. */
static long syscall(long number, ...)
{
	va_list ap;
	long args[7];
	long ret;

	va_start(ap, number);
	args[0] = number;
	args[1] = va_arg(ap, long); args[2] = va_arg(ap, long); args[3] = va_arg(ap, long);
	args[4] = va_arg(ap, long); args[5] = va_arg(ap, long); args[6] = va_arg(ap, long);
	va_end(ap);

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

/* This process's __fds[] table (src/internal/fd.c) and the kernel's own
 * real fd numbering are the SAME numbering on this platform -- open()'s
 * own Linux path (src/fcntl/open.c) hands __fd_install() the raw fd
 * openat(2) just returned and trusts __fd_alloc(0) to independently
 * compute that identical number, which only holds if EVERY real,
 * long-lived fd this process ever creates is registered there. This
 * eventfd is real and long-lived (it lives as long as wake_event does,
 * this process's whole life) but was created via a raw syscall this
 * file's own banner already explains the need for (glibc's syscall()
 * would misdecode the error), which bypasses __fd_install() entirely --
 * so, unregistered, it silently desynchronizes the two numberings from
 * here on: __fd_alloc(0) can later hand out the SAME number the kernel
 * already gave this eventfd, and the next real open()/dup()-family call
 * that lands there closes the real kernel fd out from under wake_event
 * without this library ever knowing. Confirmed as a real, reproduced
 * failure (not a hypothetical one): test/posix-signal-crossproc.c's
 * test_orphaned_stop_gets_real_sighup() opens a marker file, dup()s its
 * own stdout, and redirects -- ordinary fd traffic that, once this
 * eventfd was silently taking a number out of turn for the first time
 * ever (see crt/linux/crt1.c's own history of never calling
 * __signal_init() at all before this), collided with it and closed the
 * WRONG real fd, observed directly under strace as a `dup3(1, N, 0)`
 * silently clobbering this eventfd's own real fd N. __fd_install()
 * below closes that gap the same way every other permanent fd this
 * library ever hands out already does. */
__plat_handle_t __plat_sigevent_create(int initially_signalled)
{
	long ret = syscall(SYS_eventfd2, (long)(initially_signalled ? 1 : 0),
	                   (long)(EFD_SEMAPHORE_LX | EFD_CLOEXEC_LX));
	int fd;
	if (is_sys_error(ret)) return __PLAT_HANDLE_NULL;
	fd = __fd_install((HANDLE)(long)ret, O_CLOEXEC, 0);
	if (fd < 0) { syscall(SYS_close, ret, 0L, 0L, 0L, 0L, 0L); return __PLAT_HANDLE_NULL; }
	return box(fd);
}

/* wake_event's own real "post" operation -- see this function's own
 * plat_signal.h comment for why __plat_event_set() (a completely
 * different __plat_handle_t domain on this platform) is NOT it. Writing
 * a real 8-byte counter value to an EFD_SEMAPHORE eventfd both increments
 * its counter (waking one blocked read(), the same auto-reset-one-waiter
 * semantics __plat_sigevent_create()'s own comment already claims for
 * this handle) and never blocks itself: eventfd(2)'s own write(2) surface
 * only ever fails EAGAIN when the counter would overflow near UINT64_MAX,
 * never because nothing is currently waiting to read it. */
int __plat_sigevent_set(__plat_handle_t ev)
{
	unsigned long long one = 1;
	long ret = syscall(SYS_write, (long)unbox(ev), (long)&one, 8L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* __plat_event_set() is declared in plat_signal.h but NOT defined here
 * -- it is ALSO declared in plat_thread.h and belongs to src/thread/'s
 * Linux backend, not this file (see src/signal/nt/plat_signal.c's
 * matching comment for the identical cross-file ownership rule on NT).
 * Defining it here too would be a second, colliding definition of the
 * same symbol; this file just uses it, as sigdelivery.c does. */

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
	 * the pipe/mutant transport. The magnitude computation below guards
	 * a real, confirmed bug, not a hypothetical one: passing `ticks`
	 * straight through to the `ns = ticks * 100L` conversion below with
	 * no sign handling at all, unlike src/thread/linux/plat_thread.c's
	 * own __plat_wait_one() (`ticks = relative_ticks < 0 ?
	 * -relative_ticks : relative_ticks`), which decodes the identical
	 * convention correctly, produces a NEGATIVE ts.tv_sec/ts.tv_nsec
	 * handed straight to the real nanosleep(2)/ppoll(2) syscalls below,
	 * which the kernel rejects outright (EINVAL) instead of sleeping at
	 * all -- confirmed with strace against a real sleep(1) call reaching
	 * this function through __alertable_delay(): `nanosleep({tv_sec=-1,
	 * tv_nsec=0}) = -1 EINVAL`, immediately, every time, never once actually
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

/* plat_signal.h's own comment on this function names its only two real
 * call sites -- both in signal.c's stop-event handling
 * (__sig_consume_child_stop(), stop_self()'s retraction path) -- and
 * both hand it a handle from __plat_stop_event_create()/
 * __plat_stop_event_probe() above: a raw struct ntlibc_linux_sync*
 * (src/internal/linux/sync.h), the SAME domain __plat_event_set()
 * (src/thread/linux/plat_thread.c) already uses for that handle, NOT
 * this file's own box()/unbox() eventfd domain __plat_signal_wait()
 * uses for `wake_event` -- a genuinely different __plat_handle_t
 * domain, same class of mismatch this file's own banner already
 * discloses for __plat_kill_open()'s bare-pid convention. Decoding a
 * sync-object pointer as `fd+1` here used to hand ppoll(2)/read(2) a
 * garbage descriptor built from the mmap address's low bits -- never
 * matching a real fd, so the poll always timed out and this always
 * reported "not signalled" even after __plat_event_set() genuinely set
 * it. That is a real, confirmed hang, not a theoretical one:
 * __sig_consume_child_stop() (signal.c) never saw a self-stop
 * (stop_self()'s SIGSTOP/SIGTSTP/SIGTTIN/SIGTTOU raise() case) recorded
 * this way, so waitpid(WUNTRACED) polled discover_self_stops() in a
 * busy retry loop that could never succeed, confirmed via strace
 * against test/posix-signal-crossproc.c's test_self_stop_is_waitable().
 *
 * __plat_event_set()'s own comment records that this platform's only
 * event kind is manual-reset (unlike NT's auto-reset
 * SynchronizationEvent this handle nominally represents), so the
 * auto-reset "peek and consume" contract this function's own header
 * comment promises has to be implemented here, not inherited from the
 * kernel primitive the way ppoll()+read() got it for free from
 * EFD_SEMAPHORE: a compare-exchange from 1 to 0 is exactly that --
 * atomic across the processes sharing this MAP_SHARED word, so a
 * concurrent __plat_event_set() cannot be lost and two concurrent
 * peeks cannot both consume the same signal. */
int __plat_event_peek(__plat_handle_t ev)
{
	struct ntlibc_linux_sync *obj = (struct ntlibc_linux_sync *)ev;
	int expected = 1;

	return __atomic_compare_exchange_n(&obj->futex, &expected, 0, 0,
	                                   __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
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
 * where every kind of handle really is interchangeable. See this
 * file's own banner for why getting this wrong is a real, confirmed
 * bug (killpg/1-2.c), not a theoretical one.
 *
 * A pidfd is opened here, used once, and closed -- still real
 * pidfd_send_signal(2) delivery (SIGSTOP is uncatchable regardless,
 * but pidfd_send_signal keeps the same pid-reuse-immunity property
 * __plat_kill_open()'s existence probe already relies on, rather than
 * quietly downgrading to plain kill(2) the way __plat_process_resume()
 * already, separately, does). */
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
 * primitive) and src/process/linux/plat_process.c on this side, not
 * this file; defining it here too would be the same ODR collision
 * __plat_event_set() above avoids. */

int __plat_kill_open(pid_t pid, int want_suspend_resume, __plat_handle_t *out) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; process ID and capability flag have distinct roles
{
	/* Linux has no "open a process object" step for most per-process
	 * syscalls (kill(2), and, via src/misc/linux/plat_misc.c,
	 * getpriority(2)/setpriority(2), all take a bare pid_t directly).
	 *
	 * `h` here is the bare pid, matching plat_process.c's box_pid()
	 * convention -- see this file's own banner for why (killpg/1-2.c).
	 * That does reopen one hazard: a bare pid handed to plat_fd.h's
	 * fd-domain close() (kill()'s `if (!c) __plat_close(h);` path,
	 * src/signal/signal.c) reads pid-1 as an fd number and closes
	 * whatever real descriptor happens to have that value, if any.
	 * src/process/linux/plat_process.c's own banner already accepts the
	 * identical risk for struct __child's .h field (mark_children_
	 * inheritable()/__child_remove() call the same fd-domain
	 * __plat_dup()/__plat_close() on a bare-pid handle today) with the
	 * same disclosed reasoning: real pids on this host run past a
	 * million (that file's own report), so pid-1 reliably lands on an
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
	 * that shape lives. What Linux DOES have, unlike NT, is a real
	 * per-signal pidfd_send_signal(2): this function's one real caller
	 * (signal.c's kill(), the last-resort arm after
	 * __sig_try_deliver_remote() -- src/signal/linux/sigdelivery.c's
	 * own stub, always reporting "no listener" on this platform today
	 * -- has already declined) always passes __ENCODE_SIGNAL_EXIT(sig), so
	 * the originally-requested signal number survives inside exitcode
	 * and is decoded back out below rather than discarded. Sending THAT
	 * signal, not an unconditional SIGKILL, matters because a raw
	 * kernel signal to a process with no handler installed still runs
	 * the kernel's own default action for it -- Term for most signals
	 * (so WTERMSIG() downstream matches what was actually asked for),
	 * but Ignore for others (SIGCHLD, SIGWINCH, SIGURG): forcing
	 * SIGKILL for those turned a delivery that should have been a
	 * silent no-op into an unconditional kill. A pre-encoded exitcode
	 * that ISN'T __ENCODE_SIGNAL_EXIT()-shaped never reaches this function
	 * today (this is its one call site), but SIGKILL is kept as the
	 * defensive fallback for that case, matching the old unconditional
	 * behaviour rather than sending signal 0.
	 *
	 * kill()'s tolerance for a target already exiting (NT's
	 * STATUS_PROCESS_IS_TERMINATING special case, this header's own
	 * comment) needs no equivalent special-casing here:
	 * pidfd_send_signal(fd, sig, ...) to a zombie that has not been
	 * reaped yet still succeeds (the pidfd is still valid), and ESRCH
	 * is returned only once the process is genuinely gone -- which is
	 * already the correct, honest POSIX answer for "no such process to
	 * kill", not a case this needs to paper over the way NT's status
	 * does.
	 *
	 * `h` is the bare pid, same as __plat_process_suspend() above and
	 * for the identical reason (see that function's comment) -- a fresh
	 * pidfd is opened, used once for the kill, and closed. */
	long pid = (long)(int)(long)h;
	long fd = syscall(SYS_pidfd_open, pid, 0L);
	long ret;
	int sig = __IS_SIGNAL_EXIT(exitcode) ? (exitcode & 0x7f) : SIGKILL;
	if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	ret = syscall(SYS_pidfd_send_signal, fd, (long)sig, 0L, 0L);
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

/* rt_sigaction(2)'s own kernel-ABI struct: handler, flags, restorer,
 * then a sigset_t sized for exactly _NSIG (64) kernel signals. NOT this
 * file's own (much larger, sig_valid()-checked-up-to-_NSIG-of-its-own)
 * sigset_t from <signal.h> -- a different type with a different size the
 * real syscall knows nothing about, which is why sigsetsize below is
 * RT_SIGSETSIZE, not sizeof(sigset_t). k_restorer is left null and
 * SA_RESTORER unset: only meaningful when the kernel actually calls back
 * into a user handler and has to return through it via rt_sigreturn(2),
 * which never happens for the only two handler values this function
 * ever installs, SIG_IGN and SIG_DFL -- see this file's own
 * plat_signal.h comment on this function for why a real caught handler
 * is out of scope here.
 *
 * Fields are named k_* rather than the POSIX sa_* names <signal.h>
 * itself uses: sa_handler there is a macro (that header's own struct
 * sigaction shares one storage slot between sa_handler and sa_sigaction
 * through a union, same as this file's own signal.c banner already
 * describes for handlers[]), and this struct's real ABI layout -- fixed
 * by the kernel, not by this header -- has no union to expand it into.
 *
 * k_mask's width is a REAL per-arch ABI difference, not just a syscall
 * number: the kernel's own sigset_t is always exactly 8 bytes (64 signal
 * bits) on every Linux architecture, REGARDLESS of native word size --
 * confirmed against a real rt_sigaction(2) man page and this project's
 * own aarch64/x86_64 usage already assuming it. A plain `unsigned long
 * k_mask` happens to be exactly 8 bytes on aarch64/x86_64 (both LP64),
 * which is how the original aarch64-only version of this struct got away
 * with it, but `unsigned long` is only 4 bytes on i386 (ILP32) -- so a
 * single `unsigned long k_mask` there would only cover 32 signal bits
 * and, worse, pass a sigsetsize of 4 rather than the 8 the kernel
 * strictly validates against (rt_sigaction(2) returns EINVAL on a
 * mismatched sigsetsize, per its own man page), silently failing to
 * install any handler on this arch at all. i386 gets a real two-word
 * array instead, matching the kernel's own `struct old_sigaction`-
 * adjacent rt_sigaction ABI (two 32-bit sigset words); sigsetsize is a
 * fixed RT_SIGSETSIZE (8) on every arch below, never derived from
 * sizeof(unsigned long), so it stays correct regardless of native word
 * width. */
#define RT_SIGSETSIZE 8
#if defined(__i386__)
struct kernel_sigaction {
	void (*k_handler)(int);
	unsigned long k_flags;
	void (*k_restorer)(void);
	unsigned long k_mask[2];
};
#else
struct kernel_sigaction {
	void (*k_handler)(int);
	unsigned long k_flags;
	void (*k_restorer)(void);
	unsigned long k_mask;
};
#endif

/* Zeroes k_mask regardless of its per-arch shape above (a single word on
 * aarch64/x86_64, a two-word array on i386) -- one small helper instead
 * of duplicating an #if at each of this file's three real call sites. */
static void kernel_sigaction_zero_mask(struct kernel_sigaction *act)
{
#if defined(__i386__)
	act->k_mask[0] = 0;
	act->k_mask[1] = 0;
#else
	act->k_mask = 0;
#endif
}

void __plat_sig_sync_kernel(int sig, int ignore)
{
	struct kernel_sigaction act;
	act.k_handler = ignore ? SIG_IGN : SIG_DFL;
	act.k_flags = 0;
	act.k_restorer = 0;
	kernel_sigaction_zero_mask(&act);
	syscall(SYS_rt_sigaction, (long)sig, &act, 0L, (long)RT_SIGSETSIZE);
}

void __plat_sig_default_terminate(int sig)
{
	/* Force the kernel-level disposition to SIG_DFL right here, rather
	 * than trust it to already be synced there by __plat_sig_sync_kernel()
	 * above: that IS the case for signal.c's own default-terminate path
	 * (handlers[sig] is already SIG_DFL, both levels, whenever that path
	 * is reached), but abort()'s own override of a blocked/ignored/
	 * caught-and-returned SIGABRT (see this function's plat_signal.h
	 * comment) reaches here with the kernel possibly still set to
	 * SIG_IGN or nothing at all done to it -- and abort.html requires
	 * termination regardless. Same rt_sigaction(2) shape as
	 * __plat_sig_sync_kernel() just above.
	 *
	 * kill(2) to this process's own pid, not tgkill(2) to a specific
	 * thread: a fatal signal's default action ends the WHOLE process
	 * regardless of which thread raised it, so there is nothing
	 * tgkill(2)'s extra tid buys here, and this file has never otherwise
	 * needed a gettid(2) syscall. Same self-signal shape
	 * __plat_process_suspend_self() above already uses for SIGSTOP.
	 *
	 * The rt_sigprocmask(2) unblock right before kill(2) is not optional,
	 * and is not the same case __plat_sig_sync_kernel()'s own comment
	 * above already covers for a caller with a bare userspace-level
	 * mask. This function's own contract ("end THIS process exactly as
	 * if by sig's real default action") needs `sig` to be genuinely
	 * unblocked at the REAL kernel level at the moment of delivery, and
	 * this is the one call site that CAN reach here with it genuinely
	 * blocked there: __plat_sig_install_fault_handlers()'s own
	 * fault_dispatch() (below) is a real rt_sigaction(2) handler that did
	 * not request SA_NODEFER, so the kernel auto-blocks `sig` for the
	 * whole time it runs -- and __raise_internal_info()'s own default-
	 * terminate branch (src/signal/signal.c) can call all the way through
	 * __exit_internal() to this function from INSIDE that same handler,
	 * for the exact signal it is still blocking. A kill(2) of a currently-
	 * blocked, software-generated signal does not force default action
	 * the way a genuinely synchronous re-fault would -- it is simply
	 * queued pending, delivered only once the handler returns -- so
	 * without this unblock, the kill() below returned immediately having
	 * done nothing, __exit_internal() fell through to its own simulated-
	 * termination fallback (__plat_terminate(), an ordinary exit_group(2)
	 * whose ENCODE_SIGNAL_EXIT() status byte happens to equal `sig`
	 * itself), and the parent's real wait4(2) reported WIFEXITED with
	 * that byte instead of the real WIFSIGNALED/WTERMSIG(sig) this
	 * function exists to guarantee. Confirmed as a real, reproduced
	 * failure (not a hypothetical one) by test/posix-signal-fault-
	 * linux.c's own default-disposition cases the first time a real
	 * hardware fault's default path was ever exercised on this platform.
	 * Unblocking unconditionally is still correct for every OTHER caller
	 * (abort(), the exec() stand-in's re-raise) -- none of them can ever
	 * be running as the real kernel handler for `sig` itself, so `sig`
	 * is never blocked there for a reason this call needs to preserve.
	 *
	 * Neither syscall's result is checked past the unblock: there is
	 * nothing left to do with a failure of any of the three but return
	 * and let __exit_internal()'s own fallback run, which is exactly what
	 * happens when this function simply falls off its own end. */
	struct kernel_sigaction act;
	/* A raw byte view of the kernel's own RT_SIGSETSIZE-byte sigset,
	 * not `unsigned long mask = 1UL << (sig - 1)` (the original
	 * aarch64/x86_64-only version of this function): that spelling
	 * happens to work on those two LP64 arches (a single 8-byte word IS
	 * the whole kernel sigset there) but is a real bug on i386, the
	 * same k_mask-width issue this file's own updated struct
	 * kernel_sigaction banner explains in full -- a 4-byte `unsigned
	 * long` there can only ever express signals 1..32, and `sizeof
	 * mask` would pass sigsetsize=4 rather than the 8 rt_sigprocmask(2)
	 * strictly requires. Setting bit (sig-1) by BYTE index instead
	 * (mask[(sig-1)/8] |= 1 << ((sig-1)%8)) sidesteps native word width
	 * entirely and is correct on every little-endian arch this file
	 * targets -- aarch64, x86_64 and i386 all are. */
	unsigned char mask[RT_SIGSETSIZE];
	long pid;
	int i;

	act.k_handler = SIG_DFL;
	act.k_flags = 0;
	act.k_restorer = 0;
	kernel_sigaction_zero_mask(&act);
	syscall(SYS_rt_sigaction, (long)sig, &act, 0L, (long)RT_SIGSETSIZE);

	for (i = 0; i < RT_SIGSETSIZE; i++) mask[i] = 0;
	mask[(sig - 1) / 8] = (unsigned char)(1U << ((sig - 1) % 8));
	syscall(SYS_rt_sigprocmask, (long)SIG_UNBLOCK, (long)mask, 0L, (long)RT_SIGSETSIZE);

	pid = syscall(SYS_getpid);
	syscall(SYS_kill, pid, (long)sig);
}

/* arch/aarch64/src/sigreturn_trampoline.S -- see that file's own banner
 * for the real ABI contract (SA_RESTORER, why the kernel calls real_
 * dispatch() below directly rather than this trampoline, and why LR
 * already points here by the time real_dispatch() returns). Declared
 * the same way src/thread/pthread_cancel.c declares its own trampoline
 * symbol: a plain function declaration, so `act.k_restorer =
 * __ntlibc_sigreturn_trampoline` below needs no cast -- the types
 * already match. */
void __ntlibc_sigreturn_trampoline(void); // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- libc-internal name is intentionally reserved against application collision

/* The one real kernel-level entry point installed below, for BOTH real
 * hardware faults (the fixed five __plat_sig_install_fault_handlers()
 * installs unconditionally at startup) and, as of Tier 2, any OTHER
 * signal a caller's own sigaction()/signal() call installed a real
 * catchable disposition for (__plat_sig_install_real_handler() below) --
 * installed with THIS function as k_handler and the trampoline above as
 * k_restorer, so the kernel invokes it exactly as it would any
 * SA_SIGINFO handler -- x0/x1/x2 = (sig, &info, &ucontext), with x30
 * already pointing at the trampoline (see that file's banner).  `info`
 * is the kernel's OWN siginfo_t, not one this library synthesizes the
 * way signal.c's make_siginfo() does for a self-raised signal: confirmed
 * field-for-field against this project's own <signal.h> siginfo_t by the
 * fault-injection test this change adds (test/posix-signal-fault-linux.c)
 * -- si_code for a real SIGSEGV/SIGBUS/SIGILL/SIGFPE already arrives as
 * SEGV_MAPERR/SEGV_ACCERR/BUS_ADRALN/ILL_ILLOPC/FPE_INTDIV directly from
 * the kernel, needing none of signal.c's NT-side exception_handler()
 * reverse-engineering (that function exists only because NT hands back a
 * raw exception code with no POSIX shape at all -- Linux's kernel has
 * already done that translation before this function is ever called).
 *
 * Routes into __raise_internal_info(), the SAME portable entry point
 * every other signal source already uses (raise(), kill() to self, NT's
 * own vectored exception handler) -- not a parallel path. Locked exactly
 * as every other caller of it already is: __sig_lock() is a real
 * recursive lock keyed by gettid() (sigdelivery.c's own comment on it),
 * so a fault that interrupts code on THIS thread that already holds it
 * (a fault inside sigaction() itself, say) re-enters without blocking --
 * confirmed correct under genuine async re-entrancy, not merely assumed
 * equivalent to NT's simulated-exception dispatch: gettid() is a plain
 * syscall, async-signal-safe on its own, and the owner/depth pair it
 * compares against was last written by this same thread and can only
 * ever be read back by that same thread here, never torn by a
 * concurrent writer. A fault on a thread that does NOT already hold the
 * lock blocks on the semaphore exactly as any other contender would --
 * the same risk profile signal.c's NT-side exception_handler() already
 * accepts for the identical lock, not a new one introduced here.
 *
 * __raise_internal_info() and everything it can reach from here were
 * audited for async-signal-safety before this was wired up: no malloc
 * anywhere on the path a fault with no installed handler takes. The one
 * real malloc on the wider call graph is src/process/children.c's
 * child_grow() -- but only past 256 concurrently-unreaped children, and
 * only from __child_add(), which this path never reaches:
 * __exit_internal()'s own __child_resume_stopped() (reached via
 * __raise_internal_info()'s default-terminate branch) only ever walks
 * and signals the EXISTING child table via clear_stops(), never grows
 * it. Every other function on that default-disposition path
 * (__exit_internal(), __plat_sig_default_terminate() above, __plat_kill_
 * terminate()) is raw syscalls and stack locals throughout. A caught
 * handler this library dispatches to (the non-default-disposition
 * branch) inherits the same async-signal-safety obligation any POSIX
 * signal handler already has -- not a new risk this wiring introduces.
 * Widening this same entry point to arbitrary signals (Tier 2) adds no
 * new async-signal-safety exposure: it is still the identical function,
 * still reached only via a real kernel signal, still doing nothing more
 * than __sig_lock()/__raise_internal_info()/__sig_unlock() regardless of
 * which signal number the kernel happened to invoke it for.
 *
 * A signal whose disposition changes back to SIG_IGN or SIG_DFL after
 * this was installed for it needs no matching "uninstall" here:
 * __raise_internal_info() re-reads handlers[sig] itself on every call,
 * so it already does the right thing (ignore, or the default action --
 * including, for SIGABRT and friends, routing back through
 * __plat_sig_default_terminate() to force the real kernel-level
 * disposition to SIG_DFL and re-raise for a genuine WIFSIGNALED/
 * WTERMSIG, exactly the mechanism that function's own comment already
 * documents for the fixed five) whether or not this dispatch function is
 * still the one the kernel would call. Leaving it installed is simply
 * the honest state: this process DID once ask the kernel to route `sig`
 * through this library, and __raise_internal_info() is still the correct
 * place for that to land.
 *
 * SA_ONSTACK: honored the same way NT's own exception_handler() ->
 * __raise_internal() -> sig_dispatch() path already is -- signal.c's
 * sig_dispatch() is widened by this same change to run
 * __sig_call_on_altstack() (src/signal/aarch64/altstack.S, already built
 * for this arch, just never wired to a non-_WIN32 caller before now) on
 * real Linux too, not only NT. */
static void real_dispatch(int sig, siginfo_t *info, void *ucontext)
{
	(void)ucontext;
	__sig_lock();
	__raise_internal_info(sig, info);
	__sig_unlock();
}

/* SA_NODEFER is deliberately NOT set below: leaving the kernel's own
 * default auto-block of `sig` in place for the duration of real_
 * dispatch() means a SECOND real delivery of the SAME signal, raised by
 * a bug in this delivery path itself rather than by whatever disposition
 * it ends up calling (an application handler's own async-signal-safety
 * is its own problem, like any POSIX handler), forces the kernel's own
 * default action -- process death -- instead of recursing into real_
 * dispatch() again on a stack that, for a stack-overflow SIGSEGV
 * specifically, is already exhausted. A hard kill is the safe failure
 * mode to pick here, not a softer one to engineer around; this reasoning
 * is not specific to hardware faults, so it applies unchanged to every
 * signal this function installs real_dispatch() for, not only the fixed
 * five. */
void __plat_sig_install_real_handler(int sig)
{
	struct kernel_sigaction act;

	act.k_handler = (void (*)(int))(void *)real_dispatch; // NOLINT(bugprone-casting-through-void) -- same sigaction union-slot recovery signal.c's own SA_SIGINFO cast documents
	act.k_flags = SA_SIGINFO | SA_RESTORER;
	act.k_restorer = __ntlibc_sigreturn_trampoline;
	kernel_sigaction_zero_mask(&act);
	syscall(SYS_rt_sigaction, (long)sig, &act, 0L, (long)RT_SIGSETSIZE);
}

void __plat_sig_install_fault_handlers(void)
{
	static const int fault_sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP };
	int i, n = (int)(sizeof fault_sigs / sizeof fault_sigs[0]);

	for (i = 0; i < n; i++) __plat_sig_install_real_handler(fault_sigs[i]);
}

/* Linux has a real per-signal kernel default action and a real
 * pidfd_send_signal(2): kill()'s own cross-process arm
 * (src/signal/linux/sigdelivery.c's __sig_try_deliver_remote()) reaches
 * __plat_kill_terminate() above, which decodes the real signal back out
 * of the __ENCODE_SIGNAL_EXIT() encoding and delivers it for real,
 * applying whatever the TARGET process itself last synced as its own
 * real kernel-level disposition -- SIG_IGN is a genuine no-op, SIG_DFL
 * runs the kernel's own default action, and, as of Tier 2's widening
 * (__plat_sig_install_real_handler() above, called from signal.c's
 * sigaction()/signal()), a real caught handler genuinely runs too.  See
 * this function's plat_signal.h comment. */
int __plat_sig_deliverable_to_other_process(void)
{
	return 1;
}

/* ---- named stop-events, keyed by the filesystem namespace ----------------
 * See this file's own banner. `name`'s wide chars are ASCII by
 * construction (signal.c's own stop_event_name() builds them from a
 * fixed prefix plus hex digits), so narrowing byte-by-byte is exact,
 * not an approximation. */
#if defined(__aarch64__)
#define SYS_openat_ps    56
#define SYS_ftruncate_ps 46
#elif defined(__x86_64__)
#define SYS_openat_ps    257
#define SYS_ftruncate_ps 77
#elif defined(__i386__)
#define SYS_openat_ps    295
#define SYS_ftruncate_ps 93
#else
#error "plat_signal.c: unsupported architecture (expected __aarch64__, __x86_64__ or __i386__)"
#endif
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
 * step by `j < bufsz - 1`, the same "extent, not nullness" distinction
 * this tree's own ownership annotations already draw elsewhere. */
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

/* ---- remote-disposition probe -----------------------------------------
 * See __plat_sig_remote_disposition_nondefault()'s own plat_signal.h
 * comment for why sigdelivery.c's __sig_try_deliver_remote_nondefault()
 * needs to ask this question at all. Reuses SYS_openat_ps/AT_FDCWD_PS
 * from the stop-event section just above rather than redefining them a
 * second time under a different name. */

/* Builds "/proc/<pid>/status" into buf, hand-rolled the same way
 * stop_event_path() above builds its own path rather than reaching for
 * a formatting function. pid is always a real, positive pid_t by the
 * time this is called -- kill()'s own pid<=0 arms (self, process group,
 * "every process") are all handled long before __sig_try_deliver_
 * remote_nondefault() is ever reached -- so no sign handling is needed. */
static void proc_status_path(pid_t pid, char *buf, size_t bufsz)
{
	static const char prefix[] = "/proc/";
	static const char suffix[] = "/status";
	size_t plen = sizeof prefix - 1, slen = sizeof suffix - 1;
	unsigned long v = (unsigned long)pid;
	char digits[20];
	size_t nd = 0, i, j = 0;

	if (v == 0) digits[nd++] = '0';
	while (v > 0 && nd < sizeof digits) {
		digits[nd++] = (char)('0' + (v % 10));
		v /= 10;
	}
	for (i = 0; i < plen && j < bufsz - 1; i++) buf[j++] = prefix[i];
	for (i = 0; i < nd && j < bufsz - 1; i++) buf[j++] = digits[nd - 1 - i];
	for (i = 0; i < slen && j < bufsz - 1; i++) buf[j++] = suffix[i];
	buf[j] = 0;
}

/* Scans the first `n` bytes actually read from /proc/pid/status (`buf`)
 * for a line beginning with `field` ("SigCgt:" or "SigIgn:", proc(5))
 * and reports bit (sig-1) of the 64-bit hex bitmask that follows it --
 * see __plat_sig_remote_disposition_nondefault()'s own plat_signal.h
 * comment for why that bit means what it means. 0 if `field` never
 * appears at all: a /proc/pid/status this never expects to see the
 * shape of for real, but "not proven nondefault" is the same safe
 * default this function returns for every other failure below. */
static int status_field_bit(const char *buf, long n, const char *field, int sig)
{
	long flen = 0, i;

	while (field[flen]) flen++;

	for (i = 0; i + flen <= n; i++) {
		int match = 1, k;
		long j;
		unsigned long long mask = 0;

		if (i != 0 && buf[i - 1] != '\n') continue;
		for (k = 0; k < flen; k++)
			if (buf[i + k] != field[k]) { match = 0; break; }
		if (!match) continue;

		j = i + flen;
		while (j < n && (buf[j] == ' ' || buf[j] == '\t')) j++;
		while (j < n && buf[j] != '\n') {
			char c = buf[j];
			int digit;
			if (c >= '0' && c <= '9') digit = c - '0';
			else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
			else break;
			mask = (mask << 4) | (unsigned long long)digit;
			j++;
		}
		return (int)((mask >> (sig - 1)) & 1ULL);
	}
	return 0;
}

int __plat_sig_remote_disposition_nondefault(pid_t pid, int sig)
{
	char path[40];
	char buf[4096];
	long fd, got = 0, r;

	if (pid <= 0 || sig < 1 || sig > 64) return 0;

	proc_status_path(pid, path, sizeof path);
	fd = syscall(SYS_openat_ps, (long)AT_FDCWD_PS, (long)path, 0L, 0L, 0L, 0L);
	if (is_sys_error(fd)) return 0;

	while (got < (long)sizeof buf) {
		r = syscall(SYS_read, fd, (long)&buf[got], (long)(sizeof buf - (size_t)got), 0L, 0L, 0L);
		if (is_sys_error(r) || r == 0) break;
		got += r;
	}
	syscall(SYS_close, fd, 0L, 0L, 0L, 0L, 0L);

	if (status_field_bit(buf, got, "SigCgt:", sig)) return 1;
	return status_field_bit(buf, got, "SigIgn:", sig);
}

// NOLINTEND(misc-include-cleaner)
