/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sleeping, and the one alarm clock.
 *
 * alarm() is a real NT waitable timer: NtCreateTimer once, NtSetTimer
 * for each request, NtCancelTimer to withdraw one.  Expiry queues a
 * user APC to the thread that armed it, and that APC calls
 * __raise_internal(SIGALRM) -- the same in-process delivery path
 * raise(), abort() and the vectored exception handler use, so a SIGALRM
 * handler installed with signal()/sigaction() runs exactly as it would
 * anywhere else.
 *
 * WHAT THAT DOES AND DOES NOT REACH, because the boundary is the whole
 * character of this implementation.  NT runs a queued user APC only
 * when the target thread is in an *alertable* wait; there is no
 * mechanism to interrupt a thread that is running.  So a thread sitting
 * in sleep(), nanosleep(), usleep() or pause() -- all four pass
 * Alertable=1 below -- gets its SIGALRM at the right instant, and the
 * sleep ends with the "unslept" amount POSIX asks for.  A thread that
 * is computing gets nothing until the next such wait, at which point
 * the APC is still queued and fires immediately (measured: an expiry
 * that arrives during a non-alertable NtDelayExecution is delivered at
 * the head of the next alertable one).  A program that never sleeps
 * never sees its SIGALRM at all.  That gap is recorded in
 * test/POSIX-GAP-ACCOUNTING.md rather than glossed over.
 *
 * A dedicated timer thread would close it -- it could NtAlertThread
 * the main thread from outside, or queue the APC to it -- and it was
 * rejected.  fork() here is RtlCloneUserProcess (src/process/fork.c),
 * which clones only the calling thread: every child would come back
 * carrying a timer thread that does not exist, and any ntdll lock that
 * thread held at the instant of the clone would be held forever in the
 * child.  fork.c's banner spends a page on exactly that class of
 * damage in the WOW64 case; buying compute-bound signal delivery with
 * a second helping of it is not a trade worth making for a library
 * whose fork is its most delicate call.  Revisit if this library ever
 * grows real threads, and an atfork story with them.
 *
 * The deadline is kept as an absolute NT system time, the same
 * NtQuerySystemTime clock time() and clock_gettime(CLOCK_REALTIME) read
 * and the same one NtSetTimer's absolute mode fires on.  alarm.html
 * says "realtime seconds", which is that clock and not the performance
 * counter; keeping both the kernel's deadline and this file's
 * arithmetic on it means the answer alarm() reports and the moment the
 * signal actually arrives cannot drift apart, including across a
 * clock_settime().
 */
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

/* NtDelayExecution's two "you were woken early" answers: STATUS_USER_APC
 * when a queued user APC ran (what a timer expiry produces, measured
 * under Wine and on NT alike) and STATUS_ALERTED for a bare
 * NtAlertThread.  Both mean the wait did not run to its timeout. */
static int alerted(NTSTATUS st) { return st == STATUS_USER_APC || st == STATUS_ALERTED; }

/* The process-wide alarm.  Created on the first alarm() that asks for
 * one and then kept for the life of the process: it is a single handle,
 * re-armable in place, and never closing it is what lets fork()'s child
 * simply forget it (see __alarm_reset_after_fork below) instead of
 * having to decide whether a handle number is safe to close. */
static HANDLE alarm_timer;
/* Absolute NT time the pending SIGALRM is due; 0 means no request. */
static long long alarm_due;

static void NTAPI alarm_apc(PVOID ctx, ULONG lo, LONG hi)
{
	LARGE_INTEGER now;
	(void)ctx; (void)lo; (void)hi;

	/* Do not trust the queue; re-read the deadline.  NtCancelTimer
	 * withdraws a timer, but it cannot recall an APC that the expiry has
	 * already handed to this thread -- and a handed-over APC is not
	 * *run* until the next alertable wait, so a program that lets an
	 * alarm expire while it is computing and only then calls alarm(0),
	 * or re-arms for later, reaches here with the request it names
	 * already gone.  No deadline means cancelled; a deadline still in
	 * the future belongs to a later request, whose own APC has not been
	 * queued yet.  Either way there is no SIGALRM owed. */
	NtQuerySystemTime(&now);
	if (!alarm_due || now < alarm_due) return;

	/* Cleared before delivery, not after: SIGALRM's default action is
	 * to terminate, so __raise_internal() does not return in that
	 * case, and a handler that calls alarm() must see "no request
	 * pending" rather than one that is already in the past. */
	alarm_due = 0;
	__raise_internal(SIGALRM);
}

/* alarm.html RETURN VALUE: "a non-zero value that is the number of
 * seconds until the previous request would have generated a SIGALRM
 * signal".  Rounded *up*: any part-second still owed is a request with
 * time remaining, and 0 is the reserved answer for "there was no such
 * request", so truncating would report a live alarm as absent. */
static unsigned alarm_remaining(long long now)
{
	long long left;
	if (!alarm_due) return 0;
	left = alarm_due - now;
	/* Due, or overdue and not yet delivered because this thread has
	 * not been in an alertable wait since (see the header comment).
	 * No time remains either way, so 0 is the honest answer. */
	if (left <= 0) return 0;
	return (unsigned)((left + __TICKS_PER_SEC - 1) / __TICKS_PER_SEC);
}

unsigned alarm(unsigned s)
{
	LARGE_INTEGER now, due;
	unsigned prev;

	NtQuerySystemTime(&now);
	prev = alarm_remaining(now);

	/* "If seconds is 0, a pending alarm request, if any, is canceled"
	 * -- and a new request replaces the old one, so both paths start
	 * by withdrawing what is there. */
	if (alarm_timer) NtCancelTimer(alarm_timer, NULL);
	alarm_due = 0;

	if (s) {
		if (!alarm_timer) {
			OBJECT_ATTRIBUTES oa;
			memset(&oa, 0, sizeof oa);
			oa.Length = sizeof oa;
			/* Unnamed and, deliberately, not OBJ_INHERIT: fork()
			 * must not hand the child a live copy of the parent's
			 * alarm (fork.html), and leaving the handle
			 * non-inheritable means RtlCloneUserProcess's
			 * INHERIT_HANDLES never copies it in the first place. */
			if (!NT_SUCCESS(NtCreateTimer(&alarm_timer, TIMER_ALL_ACCESS, &oa, NotificationTimer))) {
				/* alarm.html ERRORS: "The alarm() function is
				 * always successful, and no return value is
				 * reserved to indicate an error."  There is
				 * nothing to report with, so report the previous
				 * request's remaining time and leave no alarm
				 * set -- the same shape as a request the system
				 * silently could not honour. */
				alarm_timer = 0;
				return prev;
			}
		}
		due = now + (long long)s * __TICKS_PER_SEC;
		if (NT_SUCCESS(NtSetTimer(alarm_timer, &due, alarm_apc, NULL, 0, 0, NULL)))
			alarm_due = due;
	}
	return prev;
}

/* fork()'s child side only.  RtlCloneUserProcess copies the address
 * space, so alarm_due and alarm_timer arrive in the child holding the
 * parent's values -- fork.html: "The time left until an alarm clock
 * signal shall be reset to zero, and the alarm, if any, shall be
 * canceled."  Forgetting them is the whole job and is deliberately not
 * a cancel-and-close: the timer object was created without OBJ_INHERIT
 * (above), so that handle number was never duplicated into the child
 * and NT is free to have handed it out again for something else.
 * Closing it would close whatever that is. */
void __alarm_reset_after_fork(void)
{
	alarm_due = 0;
	alarm_timer = 0;
}

/* Wait out `ticks` 100ns units, alertable, so an alarm APC can be
 * delivered in the middle of it (see the header comment).
 *
 * Returns 0 if the whole interval elapsed.  Returns -1 with errno set
 * to EINTR, and *left set to the 100ns units still owed, if a
 * signal-catching function ran first -- sleep.html and nanosleep.html
 * both end the wait only on a signal "whose action is to invoke a
 * signal-catching function or to terminate the process", so a SIGALRM
 * that was ignored has to leave the interval running.  That is what the
 * __sig_caught_count() comparison distinguishes; __raise_internal()
 * returns 0 for the handled and the ignored case alike, and the
 * terminate case does not come back here at all.
 *
 * The elapsed time is measured on NtQuerySystemTime rather than the
 * performance counter, which would be immune to a clock step: the only
 * thing that can interrupt this wait is an alarm whose own deadline is
 * on the system clock, and having the two agree matters more here than
 * having either one step-proof.  A wait that is never alerted -- every
 * call that is not interrupted -- is a single relative
 * NtDelayExecution and reads no clock at all. */
static int alertable_delay(long long ticks, long long *left)
{
	unsigned long caught = __sig_caught_count();
	LARGE_INTEGER start, now, t;

	NtQuerySystemTime(&start);
	for (;;) {
		t = -ticks;
		if (!t) t = -1;   /* a 0 timeout means "yield", not "no wait" */
		if (!alerted(NtDelayExecution(1, &t))) return 0;

		NtQuerySystemTime(&now);
		ticks -= now - start;
		start = now;
		if (__sig_caught_count() != caught) {
			if (left) *left = ticks > 0 ? ticks : 0;
			errno = EINTR;
			return -1;
		}
		if (ticks <= 0) return 0;
	}
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
	long long ticks, owed = 0;

	if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0) { errno = EINVAL; return -1; }
	ticks = req->tv_sec * __TICKS_PER_SEC + (req->tv_nsec + 99) / 100;
	if (alertable_delay(ticks, &owed) < 0) {
		/* nanosleep.html: "If the rmtp argument is non-NULL, the
		 * timespec structure referenced by it is updated to contain
		 * the amount of time remaining in the interval (the requested
		 * time minus the time actually slept)." */
		if (rem) {
			rem->tv_sec = (time_t)(owed / __TICKS_PER_SEC);
			rem->tv_nsec = (long)(owed % __TICKS_PER_SEC) * 100;
		}
		return -1;
	}
	if (rem) rem->tv_sec = rem->tv_nsec = 0;
	return 0;
}

unsigned sleep(unsigned s)
{
	long long owed = 0;
	if (alertable_delay((long long)s * __TICKS_PER_SEC, &owed) < 0)
		/* sleep.html RETURN VALUE: "If sleep() returns because the
		 * requested time has elapsed, the value returned shall be 0.
		 * If sleep() returns due to the delivery of a signal, the
		 * value returned shall be the 'unslept' amount ... in
		 * seconds."  Rounded up for the same reason alarm()'s is:
		 * 0 is the value that means the full interval elapsed. */
		return (unsigned)((owed + __TICKS_PER_SEC - 1) / __TICKS_PER_SEC);
	return 0;
}

int usleep(unsigned us)
{
	struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
	return nanosleep(&ts, 0);
}

int pause(void)
{
	unsigned long caught = __sig_caught_count();
	LARGE_INTEGER never = 0x7fffffffffffffffLL;
	/* pause.html: "suspend the calling thread until delivery of a
	 * signal whose action is either to execute a signal-catching
	 * function or to terminate the process", after which "-1 shall be
	 * returned and errno set" to [EINTR].  Alertable, so an alarm()
	 * APC ends it; nothing else in this library can, so a pause() with
	 * no alarm pending waits out the maximal timeout (see this file's
	 * header comment, and src/signal/signal.c's).  The loop is for the
	 * signal that was *ignored*: pause.html ends the wait only on one
	 * "whose action is either to execute a signal-catching function or
	 * to terminate the process", so an alert that ran no handler goes
	 * back into the wait.  A return that is not an alert at all falls
	 * straight out, which is also what keeps this from spinning on a
	 * host whose delay cannot express the maximal timeout
	 * (fuzz/ntstubs.c). */
	while (alerted(NtDelayExecution(1, &never)) && __sig_caught_count() == caught)
		;
	errno = EINTR;
	return -1;
}
