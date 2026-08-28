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
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libc.h"

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

struct thread_notice {
	void (*function)(union sigval);
	union sigval value;
};

static struct aio_request requests[AIO_MAX];
static struct aio_group groups[AIO_MAX];
static unsigned long long next_sequence;
static HANDLE worker_wake;
static int worker_started;
static int worker_synchronous;

static ULONG NTAPI notice_thread(PVOID argument)
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
		HANDLE thread;
		if (!notice) return;
		notice->function = event->sigev_notify_function;
		notice->value = event->sigev_value;
		if (!NT_SUCCESS(NtCreateThreadEx(&thread, THREAD_ALL_ACCESS, 0,
		    NtCurrentProcess(), (PVOID)notice_thread, notice, 0, 0, 0, 0, 0))) {
			free(notice);
			return;
		}
		NtClose(thread);
	}
}

/* Complete one member while the queue lock is held.  The caller sends the
 * copied notifications only after unlocking: a handler is allowed to call
 * aio_return(), which can immediately release and reuse this request slot. */
static void finish_locked(struct aio_request *request, int error, ssize_t result,
	struct sigevent *individual, int *have_individual,
	struct sigevent *list, int *have_list)
{
	request->error = error;
	request->result = result;
	request->state = REQ_DONE;
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
{
	struct aio_request *best = 0;
	int i;
	for (i = 0; i < AIO_MAX; i++)
		if (requests[i].state == REQ_QUEUED &&
		    (!best || requests[i].sequence < best->sequence))
			best = &requests[i];
	return best;
}

static ULONG NTAPI aio_worker(PVOID unused)
{
	(void)unused;
	for (;;) {
		struct aio_request *request;
		struct sigevent individual, list;
		int have_individual, have_list, error;
		ssize_t result;

		__sig_lock();
		request = next_queued();
		if (request) request->state = REQ_RUNNING;
		__sig_unlock();
		if (!request) {
			NtWaitForSingleObject(worker_wake, FALSE, 0);
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

static int start_worker(void)
{
	OBJECT_ATTRIBUTES attributes;
	HANDLE thread, event;
	NTSTATUS status;
	if (worker_started) return 0;
	InitializeObjectAttributes(&attributes, 0, 0, 0, 0);
	status = NtCreateEvent(&event, EVENT_ALL_ACCESS, &attributes,
	                       SynchronizationEvent, FALSE);
	if (status == STATUS_NOT_IMPLEMENTED) {
		worker_synchronous = 1;
		worker_started = 1;
		return 0;
	}
	if (!NT_SUCCESS(status)) { errno = EAGAIN; return -1; }
	worker_wake = event;
	status = NtCreateThreadEx(&thread, THREAD_ALL_ACCESS, 0, NtCurrentProcess(),
	                          (PVOID)aio_worker, 0, 0, 0, 0, 0, 0);
	if (!NT_SUCCESS(status)) {
		worker_wake = 0;
		NtClose(event);
		if (status == STATUS_NOT_IMPLEMENTED) {
			worker_synchronous = 1;
			worker_started = 1;
			return 0;
		}
		errno = EAGAIN;
		return -1;
	}
	worker_started = 1;
	NtClose(thread);
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
	LONG previous;
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
	NtSetEvent(worker_wake, &previous);
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

int aio_suspend(const struct aiocb *const list[], int count,
	const struct timespec *timeout)
{
	long long left = 0;
	unsigned long caught = __sig_caught_count();
	int i, any;
	if (count < 0 || !timeout_valid(timeout)) { errno = EINVAL; return -1; }
	if (timeout)
		left = (long long)timeout->tv_sec * 10000000LL +
		       (timeout->tv_nsec + 99) / 100;
	for (;;) {
		any = 0;
		__sig_lock();
		for (i = 0; i < count; i++) {
			struct aio_request *request;
			if (!list[i]) continue;
			request = lookup(list[i]);
			if (!request || request->state == REQ_DONE) {
				__sig_unlock();
				return 0;
			}
			any = 1;
		}
		__sig_unlock();
		if (!any) return 0;
		if (timeout && left <= 0) { errno = EAGAIN; return -1; }
		{
			LARGE_INTEGER delay = -10000; /* 1 ms, bounded for EINTR polling */
			if (timeout && left < 10000) delay = -left;
			NtDelayExecution(TRUE, &delay);
			if (timeout) left -= -delay;
		}
		if (__sig_caught_count() != caught) { errno = EINTR; return -1; }
	}
}

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
	int candidates = 0, submitted = 0, failed = 0;
	int i;
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
		} else submitted++;
	}
	if (mode == LIO_NOWAIT) {
		if (!candidates && event) notify(event);
		if (failed) { errno = EIO; return -1; }
		return 0;
	}
	/* LIO_WAIT waits for every operation that was successfully queued,
	 * even when another list member had an invalid opcode. */
	while (submitted) {
		submitted = 0;
		for (i = 0; i < count; i++)
			if (list[i] && lookup(list[i]) && aio_error(list[i]) == EINPROGRESS)
				submitted++;
		if (submitted) {
			LARGE_INTEGER delay = -10000;
			NtDelayExecution(TRUE, &delay);
		}
	}
	if (failed) { errno = EIO; return -1; }
	return 0;
}

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
	next_sequence = 0;
}
