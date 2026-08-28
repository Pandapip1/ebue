/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX per-process timers. One detached manager thread waits until the
 * nearest absolute deadline or until a timer operation signals its wake
 * event. That is deliberately one thread for the process, not one thread per
 * timer: timer_create() has a fixed, honest TIMER_MAX resource bound, and
 * fork() has only one background thread to forget and restart. Signal
 * delivery uses signal.c's process-pending queue, so the application
 * thread's mask -- not the manager thread's empty TLS mask -- decides when a
 * notification is consumed. A blocked timer signal therefore coalesces and
 * its missed interval expirations become timer_getoverrun()'s count.
 */
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

struct posix_timer {
	int active;
	clockid_t clock;
	struct sigevent event;
	long long due;
	long long interval;
	int overrun;
	int notification_pending;
};

static struct posix_timer timers[TIMER_MAX];
static int manager_started;
static HANDLE manager_wake;

static int clock_supported(clockid_t clock)
{
	return clock == CLOCK_REALTIME || clock == CLOCK_MONOTONIC ||
	       clock == CLOCK_PROCESS_CPUTIME_ID;
}

static int timespec_valid(const struct timespec *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec < 1000000000L;
}

static long long timespec_ns(const struct timespec *ts)
{
	return (long long)ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

static void ns_timespec(long long ns, struct timespec *ts)
{
	if (ns < 0) ns = 0;
	ts->tv_sec = (time_t)(ns / 1000000000LL);
	ts->tv_nsec = (long)(ns % 1000000000LL);
}

static long long clock_ns(clockid_t clock)
{
	struct timespec now;
	if (clock_gettime(clock, &now) < 0) return 0;
	return timespec_ns(&now);
}

static struct posix_timer *timer_lookup(timer_t id)
{
	int i;
	for (i = 0; i < TIMER_MAX; i++)
		if (id == (timer_t)&timers[i] && timers[i].active) return &timers[i];
	return 0;
}

static void timer_signal(struct posix_timer *timer, long long expirations)
{
	siginfo_t si;
	long long overrun;

	if (timer->event.sigev_notify != SIGEV_SIGNAL) return;
	if (__sig_pending_member(timer->event.sigev_signo)) {
		overrun = (long long)timer->overrun + expirations;
		timer->overrun = overrun > INT_MAX ? INT_MAX : (int)overrun;
		timer->notification_pending = 1;
		return;
	}
	/* A notification which was pending on the preceding poll has just
	 * been consumed.  Fold expirations from this narrow hand-off window
	 * into that notification rather than racing a second signal past the
	 * handler before it can call timer_getoverrun(). */
	if (timer->notification_pending) {
		overrun = (long long)timer->overrun + expirations;
		timer->overrun = overrun > INT_MAX ? INT_MAX : (int)overrun;
		timer->notification_pending = 0;
		return;
	}
	timer->overrun = expirations > INT_MAX ? INT_MAX : (int)(expirations - 1);
	memset(&si, 0, sizeof si);
	si.si_signo = timer->event.sigev_signo;
	si.si_code = SI_TIMER;
	si.si_timerid = (int)(timer - timers);
	si.si_overrun = timer->overrun;
	si.si_value = timer->event.sigev_value;
	/* A timer expiration is process-directed. The manager is an internal
	 * service thread, so delivering against its TLS mask would make a signal
	 * blocked by the application look unblocked and would run the handler on
	 * the wrong thread. Queue it for an application signal-aware point. */
	__sig_queue_process_info(si.si_signo, &si);
	timer->notification_pending = __sig_pending_member(si.si_signo);
}

static void timer_expire(struct posix_timer *timer, long long now)
{
	long long expirations;
	if (!timer->due || now < timer->due) return;
	expirations = 1;
	if (timer->interval) {
		expirations += (now - timer->due) / timer->interval;
		timer->due += expirations * timer->interval;
	} else {
		timer->due = 0;
	}
	timer_signal(timer, expirations);
}

static ULONG NTAPI timer_manager(PVOID unused)
{
	(void)unused;
	for (;;) {
		LARGE_INTEGER timeout, *wait = 0;
		long long nearest = 0;
		int i;

		__sig_lock();
		for (i = 0; i < TIMER_MAX; i++) {
			struct posix_timer *timer = &timers[i];
			long long now, left;
			if (!timer->active || !timer->due) continue;
			now = clock_ns(timer->clock);
			timer_expire(timer, now);
			if (!timer->due) continue;
			left = timer->due - now;
			if (left < 0) left = 0;
			/* A process-CPU clock has no wall-clock deadline to wait on: its
			 * time advances only while a process thread runs. Keep that one
			 * optional clock on a bounded probe while all wall clocks use their
			 * exact next deadline. */
			if (timer->clock == CLOCK_PROCESS_CPUTIME_ID && left > 1000000)
				left = 1000000; /* 1 ms of wall time */
			if (!nearest || left < nearest) nearest = left;
		}
		__sig_unlock();

		if (nearest) {
			long long ticks = (nearest + 99) / 100;
			timeout = -(ticks ? ticks : 1);
			wait = &timeout;
		}
		/* Auto-reset manager_wake closes the scan/wait race: a settime() or
		 * delete() between the scan above and this wait leaves the event
		 * signalled, so the wait returns immediately and recomputes state. */
		NtWaitForSingleObject(manager_wake, FALSE, wait);
	}
	return 0;
}

static int start_manager(void)
{
#ifdef _NTLIBC_NATIVE_BUILD
	/* The native sanitizer shim has no NT thread or signal-delivery
	 * transport.  SIGEV_NONE timers need neither: their remaining time
	 * is derived from the selected clock whenever it is queried. */
	errno = EAGAIN;
	return -1;
#else
	OBJECT_ATTRIBUTES oa;
	HANDLE event, thread;
	NTSTATUS st;
	if (manager_started) return 0;
	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	st = NtCreateEvent(&event, EVENT_ALL_ACCESS, &oa,
	                   SynchronizationEvent, FALSE);
	if (!NT_SUCCESS(st)) { errno = EAGAIN; return -1; }
	manager_wake = event;
	st = NtCreateThreadEx(&thread, THREAD_ALL_ACCESS, 0, NtCurrentProcess(),
	                      (PVOID)timer_manager, 0, 0, 0, 0, 0, 0);
	if (!NT_SUCCESS(st)) {
		manager_wake = 0;
		NtClose(event);
		errno = EAGAIN;
		return -1;
	}
	manager_started = 1;
	NtClose(thread);
	return 0;
#endif
}

int timer_create(clockid_t clock, struct sigevent *event, timer_t *id)
{
	struct posix_timer *timer = 0;
	int i;

	if (!clock_supported(clock)) { errno = EINVAL; return -1; }
	if (event && event->sigev_notify != SIGEV_SIGNAL &&
	    event->sigev_notify != SIGEV_NONE) { errno = EINVAL; return -1; }
	if (event && event->sigev_notify == SIGEV_SIGNAL &&
	    (event->sigev_signo <= 0 || event->sigev_signo >= _NSIG)) {
		errno = EINVAL;
		return -1;
	}
	__sig_lock();
	for (i = 0; i < TIMER_MAX; i++)
		if (!timers[i].active) { timer = &timers[i]; break; }
	if (!timer) { __sig_unlock(); errno = EAGAIN; return -1; }
	if ((!event || event->sigev_notify == SIGEV_SIGNAL) &&
	    start_manager() < 0) { __sig_unlock(); return -1; }
	memset(timer, 0, sizeof *timer);
	timer->active = 1;
	timer->clock = clock;
	if (event) timer->event = *event;
	else {
		timer->event.sigev_notify = SIGEV_SIGNAL;
		timer->event.sigev_signo = SIGALRM;
		timer->event.sigev_value.sival_ptr = timer;
	}
	*id = (timer_t)timer;
	__sig_unlock();
	return 0;
}

static void timer_value(struct posix_timer *timer, struct itimerspec *value)
{
	long long left = timer->due ? timer->due - clock_ns(timer->clock) : 0;
	ns_timespec(left, &value->it_value);
	ns_timespec(timer->interval, &value->it_interval);
}

int timer_settime(timer_t id, int flags, const struct itimerspec *value,
                  struct itimerspec *old)
{
	struct posix_timer *timer;
	long long first;
	LONG previous;

	if ((flags & ~TIMER_ABSTIME) || !timespec_valid(&value->it_value) ||
	    !timespec_valid(&value->it_interval)) { errno = EINVAL; return -1; }
	__sig_lock();
	timer = timer_lookup(id);
	if (!timer) { __sig_unlock(); errno = EINVAL; return -1; }
	if (old) timer_value(timer, old);
	first = timespec_ns(&value->it_value);
	timer->interval = timespec_ns(&value->it_interval);
	timer->overrun = 0;
	if (!first) timer->due = 0;
	else if (flags & TIMER_ABSTIME) timer->due = first;
	else timer->due = clock_ns(timer->clock) + first;
	if (manager_wake) NtSetEvent(manager_wake, &previous);
	__sig_unlock();
	return 0;
}

int timer_gettime(timer_t id, struct itimerspec *value)
{
	struct posix_timer *timer;
	__sig_lock();
	timer = timer_lookup(id);
	if (!timer) { __sig_unlock(); errno = EINVAL; return -1; }
	timer_value(timer, value);
	__sig_unlock();
	return 0;
}

int timer_getoverrun(timer_t id)
{
	struct posix_timer *timer;
	int result;
	__sig_lock();
	timer = timer_lookup(id);
	if (!timer) { __sig_unlock(); errno = EINVAL; return -1; }
	/* Account for sub-millisecond intervals even when the manager thread
	 * has not yet received a scheduling slice.  Expirations are a clock
	 * property, not a count of manager wakeups. */
	timer_expire(timer, clock_ns(timer->clock));
	result = timer->overrun;
	timer->notification_pending = 0;
	__sig_unlock();
	return result;
}

int timer_delete(timer_t id)
{
	struct posix_timer *timer;
	LONG previous;
	__sig_lock();
	timer = timer_lookup(id);
	if (!timer) { __sig_unlock(); errno = EINVAL; return -1; }
	memset(timer, 0, sizeof *timer);
	if (manager_wake) NtSetEvent(manager_wake, &previous);
	__sig_unlock();
	return 0;
}

void __timer_reinit_after_fork(void)
{
	memset(timers, 0, sizeof timers);
	manager_started = 0;
	manager_wake = 0;
}
