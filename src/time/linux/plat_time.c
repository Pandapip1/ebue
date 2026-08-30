/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_time.h -- see src/mman/linux/
 * plat_mem.c's own banner for the general discipline every Linux backend
 * file keeps (raw syscall(2), no host libc, -nostdinc against ntlibc's
 * OWN generated headers, aarch64 syscall numbers confirmed against this
 * host's real glibc rather than assumed).
 *
 * The interface this backend implements is expressed entirely in NT's
 * own units (100ns ticks since 1601-01-01 -- see plat_time.h's banner:
 * "the epoch conversion ... stay[s] exactly where [it] already was ...
 * and [is] NOT part of this interface"). That is not NT leaking into a
 * platform-neutral seam by accident: every front door under src/time/
 * already calls the shared, portable src/internal/libc.h helpers
 * (__nt_to_unix_sec/__nt_to_unix_nsec/__unix_to_nt) to cross between
 * that unit and a POSIX timespec, so this backend reuses those same
 * helpers rather than inventing a second, parallel conversion -- the
 * NT-tick representation is just this seam's chosen wire format, the
 * same way __plat_mem_map_file()'s prot/flags bits are already POSIX
 * values on both backends (see plat_mem.c's own banner) rather than
 * each backend picking its own vocabulary.
 *
 * ntlibc's own CLOCK_REALTIME (0) and CLOCK_MONOTONIC (1)
 * (include/time.h) are confirmed, not assumed, to already match the
 * Linux kernel's own clockid_t values for clock_gettime(2)/
 * clock_settime(2) -- verified against this build host's real
 * <time.h> (POSIX/Linux both fix these at 0 and 1) -- so
 * this file passes them straight through with no translation table,
 * unlike a backend for a platform whose clock IDs did not already
 * line up. ntlibc's own struct timespec (include/alltypes.h.in:
 * "STRUCT timespec { time_t tv_sec; long tv_nsec; }", both 8-byte
 * fields, no padding) was likewise confirmed field-order- and width-
 * compatible with the raw 16-byte structure the Linux clock_gettime(2)
 * syscall itself writes on both this build's generated-header
 * convention (x86_64, always LP64 regardless of host --
 * tools/linux-build.sh's own banner) and this host's actual aarch64
 * kernel ABI (also LP64) before being relied on below -- a raw syscall
 * writes bytes with no type-checking to catch a mismatch, so this was
 * confirmed with a throwaway host oracle program rather than assumed
 * from "it's just seconds and nanoseconds".
 */
#include <time.h>
#include <sys/resource.h>
#include <errno.h>
#include "libc.h"
#include "plat_time.h"

/* aarch64 Linux syscall numbers -- confirmed against this host's own
 * <sys/syscall.h>/<time.h>/<sys/resource.h> via a throwaway host-glibc
 * oracle program (see src/mman/linux/plat_mem.c's banner for why they
 * cannot come from a host header in this file itself: this build is
 * -nostdinc against ntlibc's own generated headers, never glibc's).
 * Oracle output on this host: SYS_clock_gettime=113, SYS_clock_settime=112,
 * SYS_getrusage=165, CLOCK_REALTIME=0, CLOCK_MONOTONIC=1,
 * sizeof(struct timespec)=16, sizeof(time_t)=sizeof(long)=8,
 * sizeof(struct rusage)=144 (matching include/sys/resource.h's own
 * layout exactly -- see below). */
#define SYS_clock_gettime 113
#define SYS_clock_settime 112
#define SYS_getrusage     165

extern long syscall(long number, ...);

/* A raw Linux syscall returns the result on success, or -errno (as an
 * unsigned value in [-4095, -1]) on failure -- see plat_mem.c's own
 * comment on this same helper. */
static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

void __plat_realtime_get(long long *nt_ticks)
{
	/* Zero-initialized so a (realistically unreachable -- clock_gettime()
	 * with a valid stack address and CLOCK_REALTIME has no failure mode
	 * on a running kernel) syscall failure still leaves a well-defined
	 * timespec behind rather than reading an uninitialized local, the
	 * same "no documented failure mode ... never checked" contract
	 * plat_time.h's own comment describes for this function. */
	struct timespec ts = {0, 0};
	syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
	/* __unix_to_nt() can only reject an out-of-range input (a `sec` many
	 * millennia from now); *nt_ticks is left at whatever it already held
	 * in that unreachable case, matching this function's own "never
	 * checked" contract -- there is no error channel to report through. */
	__unix_to_nt(ts.tv_sec, ts.tv_nsec, nt_ticks);
}

int __plat_realtime_set(long long nt_ticks)
{
	struct timespec ts;
	long ret;
	ts.tv_sec = (time_t)__nt_to_unix_sec(nt_ticks);
	ts.tv_nsec = __nt_to_unix_nsec(nt_ticks);
	ret = syscall(SYS_clock_settime, CLOCK_REALTIME, &ts);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_perfcounter_get(long long *count, long long *freq)
{
	struct timespec ts = {0, 0};
	long ret = syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	/* Express CLOCK_MONOTONIC as a nanosecond counter running at a fixed
	 * 1e9 Hz "frequency": src/internal/libc.h's __clock_qpc_to_timespec()
	 * (sec = count/freq, nsec = (count%freq) scaled to a ns fraction)
	 * reduces exactly to sec=ts.tv_sec, nsec=ts.tv_nsec for this choice
	 * of freq, so the front door (src/time/clock_gettime.c's
	 * monotonic_get()) recovers the original timespec bit-for-bit with
	 * no translation loss, the same contract NT's QPC pair already
	 * promises with its own arbitrary frequency. */
	if (ts.tv_sec < 0 ||
	    ts.tv_sec > (INT64_MAX - ts.tv_nsec) / 1000000000LL) {
		errno = EOVERFLOW;
		return -1;
	}
	*count = ts.tv_sec * 1000000000LL + ts.tv_nsec;
	*freq = 1000000000LL;
	return 0;
}

int __plat_process_cpu_ticks(long long *kernel, long long *user)
{
	/* include/sys/resource.h's own struct rusage banner already commits
	 * to reporting exactly the raw kernel ABI's fields, in the kernel's
	 * own order, with no trailing padding (a deliberate choice recorded
	 * there for the native-build symbol-preemption reason its comment
	 * describes) -- so unlike plat_fd.c's SEEK_END comment (which had to
	 * hand-roll a local struct because ntlibc's own headers had nothing
	 * to reuse), this backend can and does use ntlibc's own
	 * sys/resource.h struct rusage/getrusage() prototype directly. Its
	 * 144-byte size was confirmed to match this host's raw
	 * SYS_getrusage output exactly via the same oracle program. */
	struct rusage ru;
	long ret = syscall(SYS_getrusage, RUSAGE_SELF, &ru);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	/* getrusage() reports microsecond resolution; the interface wants
	 * 100ns ticks (__TICKS_PER_SEC == 1e7), so scale by 10 -- the same
	 * "no extra validation, __clock_combine_cpu_ticks() is the trust
	 * boundary every front door already applies" division of
	 * responsibility src/time/nt/plat_time.c's own
	 * __plat_process_cpu_ticks() keeps toward KERNEL_USER_TIMES. */
	*user = ru.ru_utime.tv_sec * __TICKS_PER_SEC + ru.ru_utime.tv_usec * 10;
	*kernel = ru.ru_stime.tv_sec * __TICKS_PER_SEC + ru.ru_stime.tv_usec * 10;
	return 0;
}

/* --- timer.c's per-process manager thread: deliberately unimplemented --
 *
 * The clock-query functions above are the part of this interface that
 * genuinely simplifies on Linux (no reservation table, no EOF/zero-fill
 * workaround -- see plat_mem.c's banner for the analogous story on the
 * mman side). The manager thread is the opposite: a correct
 * implementation needs real thread creation plus a real cross-thread
 * wake primitive, and this pass deliberately did not attempt either,
 * for a specific reason beyond "out of scope":
 *
 *   1. Thread creation via clone(2) (CLONE_VM|CLONE_FS|CLONE_FILES|
 *      CLONE_THREAD|CLONE_SIGHAND|CLONE_SETTLS|CLONE_PARENT_SETTID|
 *      CLONE_CHILD_CLEARTID, ...) needs this backend to own a stack
 *      allocation (an mmap()'d region -- clone() takes a bare stack
 *      pointer, unlike NtCreateThreadEx, which manages the new thread's
 *      stack itself) and to reap or reset that allocation across
 *      timer.c's __timer_reinit_after_fork() path.
 *   2. The manager thread needs a wake primitive the caller thread can
 *      signal without a race: futex(2) (FUTEX_WAIT/FUTEX_WAKE on a
 *      shared word) or an eventfd(2) read()/write() pair. Either is a
 *      small surface on its own, but the property NT's auto-reset event
 *      gives for free -- see __plat_timer_manager_wait's own doc
 *      comment in plat_time.h: "auto-reset wake closes the scan/wait
 *      race" -- has to be rebuilt by hand on Linux: the futex word's
 *      value has to distinguish "a wake was requested before I started
 *      waiting" from "no wake yet", or a timer_settime()/timer_delete()
 *      landing in the manager's narrow scan-then-wait window is missed
 *      silently until the next unrelated wakeup happens to occur. That
 *      is not a hang and not a crash -- it is a timer that fires late,
 *      exactly the shape of bug a short smoke test does not reliably
 *      catch (this project's own instruction for this work says so
 *      explicitly), and the accuracy bar the rest of this migration
 *      holds itself to (fuzz/linux_pilot_test.c's msync()-through-a-
 *      real-fd round trip, not just "the mapping that wrote it") is not
 *      one a rushed attempt at both (1) and (2) together would clear
 *      with confidence.
 *   3. Both of the above have to work with no libc runtime underneath
 *      them at all (this file's own -nostdinc, no-host-libc discipline):
 *      clone()'s child begins executing with a valid CLONE_VM
 *      environment from its very first instruction, with nothing to
 *      fall back on if that setup is subtly wrong.
 *
 * Returning failure here costs less than it might look like: timer.c's
 * start_manager() is only reached for a timer_create() whose event is
 * either omitted (defaults to SIGEV_SIGNAL/SIGALRM) or explicitly
 * SIGEV_SIGNAL -- a caller who passes SIGEV_NONE skips it entirely, and
 * timer_settime()/timer_gettime() for a SIGEV_NONE timer derive
 * remaining time purely from clock_gettime() (see timer.c's
 * timer_value()/clock_ticks()), which this backend already serves
 * correctly above. Only SIGEV_SIGNAL notification -- the manager thread
 * actually firing a signal on a deadline -- is unavailable here, exactly
 * mirroring the native (non-NT) sanitizer shim's own EAGAIN path in
 * src/time/nt/plat_time.c's #ifdef _NTLIBC_NATIVE_BUILD branch, which
 * has the identical "no thread/signal-delivery transport" limitation
 * for a different reason. */
int __plat_timer_manager_start(void (*loop)(void), __plat_handle_t *wake_out)
{
	(void)loop;
	(void)wake_out;
	errno = EAGAIN;
	return -1;
}

/* Unreachable on this backend: __plat_timer_manager_start() above always
 * fails, so timer.c's manager_wake is never nonzero and neither of these
 * is ever called (timer_settime()/timer_delete() both guard their call
 * with `if (manager_wake)`, and timer_manager() -- the only caller of
 * __plat_timer_manager_wait() -- never runs since no thread was ever
 * created to run it on). Real, harmless bodies are still required so the
 * link succeeds: timer.c calls both unconditionally in source, and this
 * backend's -nostdinc build has no way to prove either call statically
 * dead. */
void __plat_timer_wake(__plat_handle_t wake)
{
	(void)wake;
}

void __plat_timer_manager_wait(__plat_handle_t wake, long long ticks, int has_deadline)
{
	(void)wake;
	(void)ticks;
	(void)has_deadline;
}
