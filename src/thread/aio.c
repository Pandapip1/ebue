/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX asynchronous I/O.  A single detached native worker services a
 * bounded FIFO.  Besides avoiding a thread-per-request resource explosion,
 * serial service is the property cancellation needs: when a socket write is
 * blocked, later requests remain genuinely queued and can be canceled.
 *
 * The signal subsystem's recursive event mutex protects the queue.  It is
 * already initialized before user code can submit AIO, is safe to enter from
 * completion handlers, and avoids introducing a second home-grown lock.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libc.h"
#include "plat_thread.h"
#include "plat_fd.h"

enum request_state {
	REQ_FREE,
	REQ_QUEUED,
	REQ_RUNNING,
	REQ_DONE
};

enum request_op {
	OP_READ,
	OP_WRITE,
	OP_SYNC
};

struct aio_group {
	int active;
	int remaining;
	struct sigevent event;
};

struct aio_request {
	int state;
	int op;
	int error;
	ssize_t result;
	unsigned long long sequence;
	struct aiocb *cb;
	struct aio_group *group;
};

/* Each aio_suspend() call owns one event and registers this stack record while
 * blocked. A request completion sets every matching waiter's event under the
 * queue lock, giving condition-variable broadcast semantics without polling
 * or letting an unrelated waiter consume the only completion wake. */
struct aio_waiter {
	const struct aiocb *const *list;
	int count;
	int triggered;
	__plat_handle_t event;
	struct aio_waiter *next;
};

struct thread_notice {
	void (*function)(union sigval);
	union sigval value;
};

/* "The signal subsystem's recursive event mutex protects the queue" (see
 * this file's own banner above) -- every one of these is __sig_lock()/
 * __sig_unlock()'s to guard, not a second home-grown lock's. */
static struct aio_request requests[AIO_MAX] NTLIBC_GUARDED_BY(__ntlibc_sig_lock_token);
static struct aio_group groups[AIO_MAX] NTLIBC_GUARDED_BY(__ntlibc_sig_lock_token);
static unsigned long long next_sequence NTLIBC_GUARDED_BY(__ntlibc_sig_lock_token);
static __plat_handle_t worker_wake NTLIBC_GUARDED_BY(__ntlibc_sig_lock_token);
static int worker_started NTLIBC_GUARDED_BY(__ntlibc_sig_lock_token);
static int worker_synchronous NTLIBC_GUARDED_BY(__ntlibc_sig_lock_token);
static struct aio_waiter *waiters NTLIBC_GUARDED_BY(__ntlibc_sig_lock_token);

/* request required: dereferenced unconditionally (`request->cb`) in
 * the loop's own comparison, and every real call site passes a
 * pointer into the file-static `requests[]` table, never NULL.
 * `waiter->list[i]` below is a different, unrelated residual: `waiter`
 * is not a parameter of this function at all -- it is walked from the
 * file-static `waiters` linked list, the same "global's own invariant"
 * class as pthread_atfork.c's handlers[i] residuals -- and its `list`
 * field's own liveness traces back to aio_suspend()'s own `list`
 * parameter (POSIX-required, and never NULL-checked there, the
 * identical contract lio_listio()'s own already-marked `list` has;
 * aio_suspend()'s `list` itself is not marked because aio_suspend()'s
 * own body never dereferences it directly, only stores it into
 * `waiter.list` and forwards it to suspend_list_ready(), itself already
 * required nonnull(1)) -- verified sound by hand across that chain, not
 * expressible as a `nonnull` on this function's own signature. */
static void wake_waiters_locked(const struct aio_request *request)
    __attribute__((nonnull(1)))
    NTLIBC_REQUIRES(__ntlibc_sig_lock_token);
static void wake_waiters_locked(const struct aio_request *request)
{
	struct aio_waiter *waiter;
	int i;
	for (waiter = waiters; waiter; waiter = waiter->next) {
		for (i = 0; i < waiter->count; i++) {
			if (waiter->list[i] != request->cb) continue;
			waiter->triggered = 1;
			__plat_event_set(waiter->event);
			break;
		}
	}
}

/* argument required: cast to notice and dereferenced unconditionally
 * (`notice->function`); its one real call site (__plat_thread_spawn()
 * in notify() above) always passes a freshly malloc()'d, already
 * null-checked `notice`. */
static unsigned __PLAT_APC_CALL notice_thread(void *argument)
    __attribute__((nonnull(1)));
static unsigned __PLAT_APC_CALL notice_thread(void *argument)
{
	struct thread_notice *notice = argument;
	void (*function)(union sigval) = notice->function;
	union sigval value = notice->value;
	free(notice);
	function(value);
	return 0;
}

static void notify(const struct sigevent *event)
{
	if (!event || event->sigev_notify == SIGEV_NONE) return;
	if (event->sigev_notify == SIGEV_SIGNAL) {
		siginfo_t info;
		if (event->sigev_signo <= 0 || event->sigev_signo >= _NSIG) return;
		memset(&info, 0, sizeof info);
		info.si_signo = event->sigev_signo;
		info.si_code = SI_ASYNCIO;
		info.si_value = event->sigev_value;
		__sig_lock();
		(void)__raise_internal_info(info.si_signo, &info);
		__sig_unlock();
		return;
	}
	if (event->sigev_notify == SIGEV_THREAD && event->sigev_notify_function) {
		struct thread_notice *notice = malloc(sizeof *notice);
		__plat_handle_t thread;
		if (!notice) return;
		notice->function = event->sigev_notify_function;
		notice->value = event->sigev_value;
		if (__plat_thread_spawn(notice_thread, notice, 0, 0, &thread) < 0) {
			free(notice);
			return;
		}
		__plat_close(thread);
	}
}

/* Complete one member while the queue lock is held.  The caller sends the
 * copied notifications only after unlocking: a handler is allowed to call
 * aio_return(), which can immediately release and reuse this request slot. */
/* request/individual/have_individual/have_list are all required:
 * request->error, individual, *have_individual and *have_list are all
 * written unconditionally at the top of this function, before the one
 * conditional branch that may additionally touch `list`. `list` itself
 * is deliberately NOT marked -- `*list = request->group->event;` only
 * happens inside that branch (`request->group && request->group->active
 * && ...`), not on every call.
 *
 * A newer sweep's report also flags `request->cb->aio_sigevent`
 * (line below) -- the same "global table entry's own FIELD liveness,
 * not a parameter" residual class as aio_cancel()'s own comment
 * documents for `request->cb->aio_fildes`: request->cb is only ever
 * non-NULL once submit() has set it (after its own `if (!cb ...)`
 * check), and finish_locked()/perform() are only ever called on a
 * request already past that point (REQ_RUNNING, transitioning to
 * REQ_DONE) -- verified sound by hand, not force-fit to `nonnull`,
 * which has no way to describe a struct field's own conditional
 * liveness. */
static void finish_locked(struct aio_request *request, int error, ssize_t result,
	struct sigevent *individual, int *have_individual,
	struct sigevent *list, int *have_list)
    __attribute__((nonnull(1, 4, 5, 7)))
    NTLIBC_REQUIRES(__ntlibc_sig_lock_token);
static void finish_locked(struct aio_request *request, int error, ssize_t result, // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
	struct sigevent *individual, int *have_individual,
	struct sigevent *list, int *have_list)
{
	request->error = error;
	request->result = result;
	request->state = REQ_DONE;
	wake_waiters_locked(request);
	*individual = request->cb->aio_sigevent;
	*have_individual = individual->sigev_notify != SIGEV_NONE &&
		(individual->sigev_notify != SIGEV_SIGNAL || individual->sigev_signo != 0);
	*have_list = 0;
	if (request->group && request->group->active && --request->group->remaining == 0) {
		*list = request->group->event;
		*have_list = list->sigev_notify != SIGEV_NONE &&
			(list->sigev_notify != SIGEV_SIGNAL || list->sigev_signo != 0);
		request->group->active = 0;
	}
}

/* request required (dereferenced immediately, `request->cb`); error
 * required (`*error = ...;` unconditional at the very end -- this
 * function has no early-return path that skips it). `cb->aio_fildes`
 * below is the same request->cb field-liveness residual documented on
 * finish_locked() above and aio_cancel() below -- not a fact about
 * this function's own `request`/`error` parameters. */
static ssize_t perform(struct aio_request *request, int *error)
    __attribute__((nonnull(1, 2)));
static ssize_t perform(struct aio_request *request, int *error)
{
	struct aiocb *cb = request->cb;
	struct __fd *fd;
	ssize_t result;

	errno = 0;
	if (request->op == OP_SYNC) {
		result = fsync(cb->aio_fildes);
	} else {
		fd = __fd_get(cb->aio_fildes);
		if (!fd) result = -1;
		else if (fd->type == __FD_FILE) {
			if (request->op == OP_READ)
				result = pread(cb->aio_fildes, (void *)cb->aio_buf,
				               cb->aio_nbytes, cb->aio_offset);
			else
				result = pwrite(cb->aio_fildes, (const void *)cb->aio_buf,
				                cb->aio_nbytes, cb->aio_offset);
		} else if (request->op == OP_READ) {
			result = read(cb->aio_fildes, (void *)cb->aio_buf, cb->aio_nbytes);
		} else {
			result = write(cb->aio_fildes, (const void *)cb->aio_buf, cb->aio_nbytes);
		}
	}
	*error = result < 0 ? errno : 0;
	return result;
}

static struct aio_request *next_queued(void)
    NTLIBC_REQUIRES(__ntlibc_sig_lock_token);
static struct aio_request *next_queued(void)
{
	struct aio_request *best = 0;
	int i;
	for (i = 0; i < AIO_MAX; i++)
		if (requests[i].state == REQ_QUEUED &&
		    (!best || requests[i].sequence < best->sequence))
			best = &requests[i];
	return best;
}

static unsigned __PLAT_APC_CALL aio_worker(void *unused)
{
	(void)unused;
	for (;;) {
		struct aio_request *request;
		__plat_handle_t wake;
		struct sigevent individual, list;
		int have_individual, have_list, error;
		ssize_t result;

		__sig_lock();
		request = next_queued();
		if (request) request->state = REQ_RUNNING;
		wake = worker_wake;
		__sig_unlock();
		if (!request) {
			__plat_wait_one(wake, 0, 0, 0);
			continue;
		}

		result = perform(request, &error);
		__sig_lock();
		finish_locked(request, error, result, &individual, &have_individual,
		              &list, &have_list);
		__sig_unlock();
		if (have_individual) notify(&individual);
		if (have_list) notify(&list);
	}
	return 0;
}

static int start_worker(void) NTLIBC_REQUIRES(__ntlibc_sig_lock_token);
static int start_worker(void)
{
	__plat_handle_t thread, event;
	int result;
	if (worker_started) return 0;
	result = __plat_event_create(&event);
	if (result == -2) {
		worker_synchronous = 1;
		worker_started = 1;
		return 0;
	}
	if (result < 0) { errno = EAGAIN; return -1; }
	worker_wake = event;
	result = __plat_thread_spawn(aio_worker, 0, 0, 0, &thread);
	if (result < 0) {
		worker_wake = 0;
		__plat_close(event);
		if (result == -2) {
			worker_synchronous = 1;
			worker_started = 1;
			return 0;
		}
		errno = EAGAIN;
		return -1;
	}
	worker_started = 1;
	__plat_close(thread);
	return 0;
}

static int valid_event(const struct sigevent *event)
{
	if (event->sigev_notify == SIGEV_NONE) return 1;
	if (event->sigev_notify == SIGEV_SIGNAL)
		return event->sigev_signo >= 0 && event->sigev_signo < _NSIG;
	if (event->sigev_notify == SIGEV_THREAD)
		return event->sigev_notify_function != 0;
	return 0;
}

static int submit(struct aiocb *cb, int op, struct aio_group *group)
{
	struct aio_request *request = 0;
	struct sigevent individual, list;
	ssize_t result;
	int error, have_individual, have_list, i;

	if (!cb ||
	    ((op == OP_READ || op == OP_WRITE) &&
	     (cb->aio_reqprio < 0 || cb->aio_reqprio > AIO_PRIO_DELTA_MAX ||
	      cb->aio_offset < 0 || cb->aio_nbytes > (size_t)SSIZE_MAX ||
	      (cb->aio_nbytes && !cb->aio_buf))) ||
	    !valid_event(&cb->aio_sigevent)) {
		errno = EINVAL;
		return -1;
	}

	__sig_lock();
	if (cb->__nt_request) {
		__sig_unlock();
		errno = EINVAL;
		return -1;
	}
	if (start_worker() < 0) { __sig_unlock(); return -1; }
	for (i = 0; i < AIO_MAX; i++)
		if (requests[i].state == REQ_FREE) { request = &requests[i]; break; }
	if (!request) {
		__sig_unlock();
		errno = EAGAIN;
		return -1;
	}
	memset(request, 0, sizeof *request);
	request->state = REQ_QUEUED;
	request->op = op;
	request->sequence = ++next_sequence;
	request->cb = cb;
	request->group = group;
	cb->__nt_request = request;
	if (worker_synchronous) {
		request->state = REQ_RUNNING;
		__sig_unlock();
		result = perform(request, &error);
		__sig_lock();
		finish_locked(request, error, result, &individual, &have_individual,
		              &list, &have_list);
		__sig_unlock();
		if (have_individual) notify(&individual);
		if (have_list) notify(&list);
		return 0;
	}
	__plat_event_set(worker_wake);
	__sig_unlock();
	return 0;
}

int aio_read(struct aiocb *cb) { return submit(cb, OP_READ, 0); }
int aio_write(struct aiocb *cb) { return submit(cb, OP_WRITE, 0); }

int aio_fsync(int op, struct aiocb *cb)
{
	if (op != O_SYNC && op != O_DSYNC) { errno = EINVAL; return -1; }
	/* Unlike read/write initiation, aio_fsync() lists EBADF as a
	 * synchronous failure and the descriptor contributes no deferred
	 * transfer whose error could usefully be retrieved later. */
	if (!cb || !__fd_get(cb->aio_fildes)) return -1;
	return submit(cb, OP_SYNC, 0);
}

static struct aio_request *lookup(const struct aiocb *cb)
    NTLIBC_REQUIRES(__ntlibc_sig_lock_token);
static struct aio_request *lookup(const struct aiocb *cb)
{
	struct aio_request *request;
	if (!cb || !cb->__nt_request) return 0;
	request = cb->__nt_request;
	if (request < requests || request >= requests + AIO_MAX ||
	    request->state == REQ_FREE || request->cb != cb) return 0;
	return request;
}

int aio_error(const struct aiocb *cb)
{
	struct aio_request *request;
	int result;
	__sig_lock();
	request = lookup(cb);
	if (!request) result = EINVAL;
	else if (request->state == REQ_DONE) result = request->error;
	else result = EINPROGRESS;
	__sig_unlock();
	return result;
}

ssize_t aio_return(struct aiocb *cb)
{
	struct aio_request *request;
	ssize_t result;
	int error;
	__sig_lock();
	request = lookup(cb);
	if (!request || request->state != REQ_DONE) {
		__sig_unlock();
		errno = EINVAL;
		return -1;
	}
	result = request->result;
	error = request->error;
	cb->__nt_request = 0;
	memset(request, 0, sizeof *request);
	__sig_unlock();
	if (result < 0) errno = error;
	return result;
}

static int timeout_valid(const struct timespec *timeout)
{
	return !timeout || (timeout->tv_sec >= 0 && timeout->tv_nsec >= 0 &&
	                    timeout->tv_nsec < 1000000000L);
}

/* Called with the queue lock held. Invalid request identities have already
 * ceased being outstanding and therefore satisfy aio_suspend() just like a
 * request another thread completed and collected with aio_return(). */
/* list/any both required: *any = 0 is written unconditionally at
 * entry, and list is subscripted directly (`list[i]`) whenever
 * count >= 1 -- every real call site (aio_suspend()) forwards its own
 * list argument, which is itself required by POSIX. */
static int suspend_list_ready(const struct aiocb *const list[], int count,
	int *any) __attribute__((nonnull(1, 3)))
    NTLIBC_REQUIRES(__ntlibc_sig_lock_token);
static int suspend_list_ready(const struct aiocb *const list[], int count,
	int *any)
{
	int i;
	*any = 0;
	for (i = 0; i < count; i++) {
		struct aio_request *request;
		if (!list[i]) continue;
		request = lookup(list[i]);
		if (!request || request->state == REQ_DONE) return 1;
		*any = 1;
	}
	return 0;
}

static void remove_waiter_locked(struct aio_waiter *waiter)
    NTLIBC_REQUIRES(__ntlibc_sig_lock_token);
static void remove_waiter_locked(struct aio_waiter *waiter)
{
	struct aio_waiter **link = &waiters;
	while (*link && *link != waiter) link = &(*link)->next;
	if (*link) *link = waiter->next;
}

int aio_suspend(const struct aiocb *const list[], int count,
	const struct timespec *timeout)
{
	struct aio_waiter waiter;
	long long start = 0, deadline = 0;
	__plat_handle_t handles[2];
	__plat_handle_t signal_event;
	unsigned long caught = __sig_caught_count();
	int any, handle_count;
	if (count < 0 || !timeout_valid(timeout)) { errno = EINVAL; return -1; }
	if (timeout) {
		long long subsecond = (timeout->tv_nsec + 99) / 100;
		long long ticks;
		if (timeout->tv_sec > (LLONG_MAX - subsecond) / 10000000LL)
			ticks = LLONG_MAX;
		else
			ticks = (long long)timeout->tv_sec * 10000000LL + subsecond;
		start = __plat_query_system_time();
		deadline = ticks > LLONG_MAX - start ? LLONG_MAX : start + ticks;
	}

	/* Avoid requiring an event on the synchronous fallback path, where every
	 * valid request is already done before aio_suspend() can observe it. */
	__sig_lock();
	if (suspend_list_ready(list, count, &any) || !any) {
		__sig_unlock();
		return 0;
	}
	__sig_unlock();

	if (__plat_event_create(&waiter.event) < 0) { errno = EAGAIN; return -1; }
	waiter.list = list;
	waiter.count = count;
	waiter.triggered = 0;

	/* The second state check and registration share the producer's lock. A
	 * completion can therefore occur before the check or after registration,
	 * but never in a lost-wakeup gap between them. */
	__sig_lock();
	if (suspend_list_ready(list, count, &any) || !any) {
		__sig_unlock();
		__plat_close(waiter.event);
		return 0;
	}
	if (timeout && deadline <= start) {
		__sig_unlock();
		__plat_close(waiter.event);
		errno = EAGAIN;
		return -1;
	}
	waiter.next = waiters;
	waiters = &waiter;
	__sig_unlock();

	handles[0] = waiter.event;
	handle_count = 1;
	signal_event = __sig_delivery_event();
	if (signal_event) handles[handle_count++] = signal_event;
	for (;;) {
		long long now;
		int status;
		if (timeout) {
			now = __plat_query_system_time();
			if (now >= deadline) status = __PLAT_WAIT_TIMEOUT;
			else status = __plat_wait_any(handles, (unsigned)handle_count, 1,
			                              1, -(deadline - now));
		} else {
			status = __plat_wait_any(handles, (unsigned)handle_count, 1, 0, 0);
		}

		__sig_drain_pending();
		__sig_lock();
		if (waiter.triggered || suspend_list_ready(list, count, &any) || !any) {
			remove_waiter_locked(&waiter);
			__sig_unlock();
			__plat_close(waiter.event);
			return 0;
		}
		if (__sig_caught_count() != caught || status == __PLAT_WAIT_TIMEOUT ||
		    status == __PLAT_WAIT_ERROR) {
			int wait_error = status == __PLAT_WAIT_ERROR;
			remove_waiter_locked(&waiter);
			__sig_unlock();
			__plat_close(waiter.event);
			errno = status == __PLAT_WAIT_TIMEOUT ? EAGAIN : wait_error ? EINVAL : EINTR;
			return -1;
		}
		__sig_unlock();
	}
}

/* cb is deliberately NOT required: aio_cancel.html DESCRIPTION -- "If
 * the aiocbp argument is NULL, then all outstanding cancelable I/O
 * operations shall be canceled" -- and `(cb && request->cb != cb)`
 * below is exactly that real, load-bearing check, not decoration.
 * (This function's own flagged finding, `request->cb->aio_fildes`, is
 * a different, unrelated fact regardless -- request is always
 * `&requests[i]`, a file-static table entry, so it is request->cb, a
 * FIELD of that global, whose own liveness is in question, not
 * anything expressible on aio_cancel()'s own parameters; same
 * "global's own invariant, not a parameter" residual class as
 * src/env/setenv.c's is_putenv() from 242ed40.) */
int aio_cancel(int fd, struct aiocb *cb)
{
	struct sigevent notifications[AIO_MAX * 2];
	int notification_count = 0;
	int matched = 0, canceled = 0, running = 0;
	int i;
	if (!__fd_get(fd)) return -1;
	__sig_lock();
	for (i = 0; i < AIO_MAX; i++) {
		struct aio_request *request = &requests[i];
		struct sigevent individual, group;
		int have_individual, have_group;
		if (request->state == REQ_FREE || request->cb->aio_fildes != fd ||
		    (cb && request->cb != cb)) continue;
		matched = 1;
		if (request->state == REQ_RUNNING) { running = 1; continue; }
		if (request->state == REQ_DONE) continue;
		finish_locked(request, ECANCELED, -1, &individual, &have_individual,
		              &group, &have_group);
		canceled = 1;
		if (have_individual) notifications[notification_count++] = individual;
		if (have_group) notifications[notification_count++] = group;
	}
	__sig_unlock();
	for (i = 0; i < notification_count; i++) notify(&notifications[i]);
	if (running) return AIO_NOTCANCELED;
	if (canceled) return AIO_CANCELED;
	(void)matched;
	return AIO_ALLDONE;
}

static struct aio_group *group_alloc(int count, const struct sigevent *event)
{
	struct aio_group *group = 0;
	int i;
	__sig_lock();
	for (i = 0; i < AIO_MAX; i++)
		if (!groups[i].active) { group = &groups[i]; break; }
	if (group) {
		memset(group, 0, sizeof *group);
		group->active = 1;
		group->remaining = count;
		if (event) group->event = *event;
		else group->event.sigev_notify = SIGEV_NONE;
	}
	__sig_unlock();
	return group;
}

static void group_drop(struct aio_group *group)
{
	struct sigevent event;
	int send = 0;
	if (!group) return;
	__sig_lock();
	if (group->active && --group->remaining == 0) {
		event = group->event;
		send = event.sigev_notify != SIGEV_NONE &&
		       (event.sigev_notify != SIGEV_SIGNAL || event.sigev_signo != 0);
		group->active = 0;
	}
	__sig_unlock();
	if (send) notify(&event);
}

int lio_listio(int mode, struct aiocb *const list[], int count,
	struct sigevent *event)
{
	struct aio_group *group = 0;
	const struct aiocb *pending[AIO_LISTIO_MAX];
	int candidates = 0, submitted = 0, failed = 0;
	int i, remaining;
	if ((mode != LIO_WAIT && mode != LIO_NOWAIT) || count < 0 ||
	    count > AIO_LISTIO_MAX || (event && !valid_event(event))) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < count; i++)
		if (list[i] && (list[i]->aio_lio_opcode == LIO_READ ||
		                list[i]->aio_lio_opcode == LIO_WRITE)) candidates++;
	if (mode == LIO_NOWAIT && candidates) {
		group = group_alloc(candidates, event);
		if (!group) { errno = EAGAIN; return -1; }
	}
	for (i = 0; i < count; i++) {
		struct aiocb *cb = list[i];
		int op;
		if (!cb || cb->aio_lio_opcode == LIO_NOP) continue;
		if (cb->aio_lio_opcode == LIO_READ) op = OP_READ;
		else if (cb->aio_lio_opcode == LIO_WRITE) op = OP_WRITE;
		else { failed = 1; continue; }
		if (submit(cb, op, group) < 0) {
			failed = 1;
			group_drop(group);
		} else pending[submitted++] = cb;
	}
	if (mode == LIO_NOWAIT) {
		if (!candidates && event) notify(event);
		if (failed) { errno = EIO; return -1; }
		return 0;
	}
	/* LIO_WAIT waits for every operation that was successfully queued,
	 * even when another list member had an invalid opcode. */
	remaining = submitted;
	while (remaining) {
		for (i = 0; i < submitted; i++) {
			if (!pending[i] || aio_error(pending[i]) == EINPROGRESS) continue;
			pending[i] = 0;
			remaining--;
		}
		if (remaining && aio_suspend(pending, submitted, 0) < 0) return -1;
	}
	if (failed) { errno = EIO; return -1; }
	return 0;
}

void __aio_reset_after_fork(void) NTLIBC_NO_THREAD_SAFETY_ANALYSIS;
void __aio_reset_after_fork(void)
{
	int i;
	for (i = 0; i < AIO_MAX; i++)
		if (requests[i].state != REQ_FREE && requests[i].cb)
			requests[i].cb->__nt_request = 0;
	memset(requests, 0, sizeof requests);
	memset(groups, 0, sizeof groups);
	worker_wake = 0;
	worker_started = 0;
	worker_synchronous = 0;
	waiters = 0;
	next_sequence = 0;
}

// NOLINTEND(misc-include-cleaner)
