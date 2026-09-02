/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * adjtime(): include/sys/time.h's own declaration carries an "undefined-
 * ok" marker whose reasoning is entirely NT-specific -- the only ntdll/
 * kernel32 primitive this library has for setting the clock at all is
 * NtSetSystemTime (src/time/stime.c), a single hard jump, with no W32Time
 * -style gradual-slew API reachable from a plain process. That marker
 * stays true of, and only checked against, the NT build. It has no
 * bearing here: Linux has had a real NTP-style slewing API,
 * adjtimex(2)/clock_adjtime(2), since long before this library existed.
 *
 * BSD adjtime()'s own semantics -- slew the clock by `delta` a little at
 * a time rather than jumping it, and report whatever part of a still-
 * pending previous slew had not yet been applied -- are not something
 * this file has to build out of adjtimex(2)'s general NTP machinery by
 * hand: the kernel already speaks this exact dialect natively via the
 * ADJ_OFFSET_SINGLESHOT mode (0x8001, xntp 3.4's own "old-fashioned
 * adjtime" name for it, confirmed against a real host <bits/timex.h>,
 * whose own comment on that value names it exactly that), which sets a
 * one-shot, non-PLL offset in microseconds -- precisely BSD adjtime()'s
 * own unit and semantics -- and ADJ_OFFSET_SS_READ (0xa001, that same
 * header's "read-only adjtime"), which reads back the remainder of a
 * still-pending one without modifying it, precisely adjtime()'s own
 * `delta == NULL` query form. So this is a direct, one-syscall mapping,
 * not a reimplementation of slewing logic in this file.
 *
 * struct timex's layout (bits/timex.h on a real LP64 host, confirmed
 * against this host's own glibc headers rather than assumed) has two
 * shapes gated on `__USE_TIME64_REDIRECTS || (__TIMESIZE==64 &&
 * __WORDSIZE==32)`; this library's own build is LP64 on every target it
 * has (tools/linux-build.sh's own banner), which takes the `#else`
 * branch -- plain `long` fields throughout, no `long long` widening --
 * reproduced field-for-field below with the compiler's own natural
 * alignment doing the same padding insertion the kernel's own struct
 * definition relies on (both compiled for the identical LP64 ABI, so
 * there is no layout mismatch to guard against the way a cross-width
 * field would need one).
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/time.h>
#include <string.h>
#include <errno.h>

/* aarch64 Linux syscall number -- confirmed against this host's own
 * <sys/syscall.h> via a throwaway host-glibc oracle program (the same
 * technique src/time/linux/plat_time.c's own banner describes; this
 * build is -nostdinc against ntlibc's own generated headers, never
 * glibc's, so the number cannot come from a host header in this file
 * itself). Oracle output on this host: SYS_adjtimex=171. */
#define SYS_adjtimex 171

#define ADJ_OFFSET_SINGLESHOT 0x8001
#define ADJ_OFFSET_SS_READ    0xa001

/* Raw kernel struct timex, LP64 layout -- see this file's own banner.
 * Only the fields adjtime() actually reads or writes are named
 * precisely; everything else is exactly as wide as the kernel's own
 * definition so the fields that come after line up, even though this
 * file never touches them. */
struct linux_timex {
	unsigned int modes;
	long offset;
	long freq;
	long maxerror;
	long esterror;
	int status;
	long constant;
	long precision;
	long tolerance;
	long time_sec;
	long time_usec;
	long tick;
	long ppsfreq;
	long jitter;
	int shift;
	long stabil;
	long jitcnt;
	long calcnt;
	long errcnt;
	long stbcnt;
	int tai;
	int pad[11];
};

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path -- see src/time/linux/plat_time.c's own raw_syscall()
 * banner for why this file defines its own rather than sharing one. */
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

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

int adjtime(const struct timeval *delta, struct timeval *olddelta)
{
	struct linux_timex tx;
	long ret;
	long long usec;

	memset(&tx, 0, sizeof tx);
	if (delta) {
		/* adjtime.html leaves the exact overflow boundary
		 * implementation-defined; this library's own choice is to
		 * refuse rather than silently truncate a delta that would not
		 * round-trip through the kernel's own `long` microsecond
		 * field. */
		usec = (long long)delta->tv_sec * 1000000LL + (long long)delta->tv_usec;
		if (usec > 2147483647LL || usec < -2147483648LL) {
			errno = EINVAL;
			return -1;
		}
		tx.modes = ADJ_OFFSET_SINGLESHOT;
		tx.offset = (long)usec;
	} else {
		/* adjtime()'s own NULL-`delta` query form: read the remaining
		 * unapplied offset without touching it, exactly ADJ_OFFSET_
		 * SS_READ's own documented behaviour. */
		tx.modes = ADJ_OFFSET_SS_READ;
	}
	ret = raw_syscall(SYS_adjtimex, (long)&tx, 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	if (olddelta) {
		olddelta->tv_sec = (time_t)(tx.offset / 1000000L);
		olddelta->tv_usec = (suseconds_t)(tx.offset % 1000000L);
	}
	return 0;
}

// NOLINTEND(misc-include-cleaner)
