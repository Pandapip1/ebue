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
 * POSIX timers now use a dedicated manager thread (src/time/timer.c).
 * That thread signals the delivery event when it queues or catches a
 * signal, waking __alertable_delay() below and turning a caught handler
 * into EINTR for sleep()/nanosleep().
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

/* The process-wide alarm.  Created on the first alarm() that asks for
 * one and then kept for the life of the process: it is a single handle,
 * re-armable in place, and never closing it is what lets fork()'s child
 * simply forget it (see __alarm_reset_after_fork below) instead of
 * having to decide whether a handle number is safe to close. */
static HANDLE alarm_timer;
/* Absolute NT time the pending SIGALRM is due; 0 means no request. */
static long long alarm_due;
/* Which request the pending APC belongs to.  Bumped by every alarm(),
 * so a queued APC can name the request that queued it; see alarm_apc. */
static unsigned long alarm_seq;

static void NTAPI alarm_apc(PVOID ctx, ULONG lo, LONG hi)
{
	(void)lo; (void)hi;

	/* Do not trust the queue: this APC may belong to a request that is
	 * already gone.  NtCancelTimer withdraws a timer, but it cannot
	 * recall an APC that the expiry has already handed to this thread
	 * -- and a handed-over APC is not *run* until the next alertable
	 * wait, so a program that lets an alarm expire while it is
	 * computing and only then calls alarm(0), or re-arms for later,
	 * reaches here with the request it names already spent.
	 *
	 * WHICH REQUEST THIS APC IS FOR IS ASKED BY IDENTITY, NOT BY THE
	 * CLOCK, and that distinction is the whole of this comment.  The
	 * obvious test -- re-read the time and drop the APC if the deadline
	 * has not passed -- is wrong, because it cannot tell a superseded
	 * APC from a punctual one: an NT timer may run its APC a hair
	 * BEFORE NtQuerySystemTime() agrees the due time has arrived, the
	 * two being different clocks sampled at different moments.  A
	 * dropped APC is not retried, and alarm_due stays set, so that
	 * loses the SIGALRM permanently.  Measured under Wine before this
	 * was changed, with the comparison instrumented: the APC ran with
	 * `now - alarm_due` at -9886 ticks (0.99ms early) and the signal
	 * was silently swallowed, on roughly one run in three; the same
	 * binary passed the other two.  A tolerance would only move the
	 * boundary, not remove it.
	 *
	 * The generation counter answers the question exactly instead.
	 * Every alarm() bumps alarm_seq and hands the new value to
	 * NtSetTimer as the APC context, so `ctx == alarm_seq` is true for
	 * precisely the APC of the current request and false for every
	 * stale one -- no clock is read, and an early expiry is delivered
	 * as the expiry it is.  alarm_due == 0 covers the cancelled case,
	 * where there is no current request for any APC to match. */
	if (!alarm_due || (unsigned long)(ULONG_PTR)ctx != alarm_seq) return;

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
	/* Retire the old request's identity before arming a new one, so an
	 * APC already handed over for it can no longer match (alarm_apc). */
	alarm_seq++;

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
		if (NT_SUCCESS(NtSetTimer(alarm_timer, &due, alarm_apc,
		                          (PVOID)(ULONG_PTR)alarm_seq, 0, 0, NULL)))
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
	alarm_seq++;
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
 * Elapsed time is measured on NtQuerySystemTime rather than the
 * performance counter, which would be immune to a clock step: alarm()
 * deadlines use the system clock, and having the two agree matters more
 * here than making either one step-proof. The delivery event permits one
 * wait for the whole remaining interval while retaining exact elapsed-time
 * accounting across early wakes. */
int __alertable_delay(long long ticks, long long *left, const char *operation)
{
	unsigned long caught = __sig_caught_count();
	LARGE_INTEGER start, now, t;

	__pthread_cancel_unsafe_enter(operation);
	NtQuerySystemTime(&start);
	__pthread_testcancel();
	__sig_drain_pending();
	if (__sig_caught_count() != caught) {
		if (left) *left = ticks;
		errno = EINTR;
		__pthread_cancel_unsafe_leave();
		return -1;
	}
	while (ticks > 0) {
		/* The signal-delivery event closes the check/wait race and wakes this
		 * thread as soon as a background source queues or catches a signal. */
		t = -ticks;
		if (!t) t = -1;   /* a 0 timeout means "yield", not "no wait" */
		__sig_wait_delivery(&t);
		__pthread_testcancel();
		__sig_drain_pending();

		NtQuerySystemTime(&now);
		ticks -= now - start;
		start = now;
		if (__sig_caught_count() != caught) {
			if (left) *left = ticks > 0 ? ticks : 0;
			errno = EINTR;
			__pthread_cancel_unsafe_leave();
			return -1;
		}
	}
	__pthread_cancel_unsafe_leave();
	return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
	long long ticks, owed = 0;

	if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0) { errno = EINVAL; return -1; }
	ticks = __duration_ticks(req->tv_sec, req->tv_nsec);
	if (__alertable_delay(ticks, &owed, "nanosleep()") < 0) {
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
	if (__alertable_delay((long long)s * __TICKS_PER_SEC, &owed,
	    "sleep()") < 0)
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
	long long ticks = ((long long)us * __TICKS_PER_SEC + 999999) / 1000000;
	return __alertable_delay(ticks, 0, "usleep()");
}

int pause(void)
{
	unsigned long caught = __sig_caught_count();
	/* pause.html: "suspend the calling thread until delivery of a
	 * signal whose action is either to execute a signal-catching
	 * function or to terminate the process", after which "-1 shall be
	 * returned and errno set" to [EINTR].  Alertable, so an alarm()
	 * APC ends it.  Signals delivered by a background delivery thread
	 * set the delivery event and are observed through the caught counter.
	 * An ignored signal changes no counter and therefore leaves pause()
	 * waiting, as POSIX requires. */
	__pthread_cancel_unsafe_enter("pause()");
	while (__sig_caught_count() == caught) {
		__sig_drain_pending();
		if (__sig_caught_count() != caught) break;
		__sig_wait_delivery(0);
	}
	__pthread_cancel_unsafe_leave();
	errno = EINTR;
	return -1;
}
