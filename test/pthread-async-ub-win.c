/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX makes cancellation with PTHREAD_CANCEL_ASYNCHRONOUS undefined
 * while any function other than pthread_cancel(), pthread_setcancelstate(),
 * or pthread_setcanceltype() is executing.  ntlibc diagnoses the dangerous
 * subset in which redirecting a thread would abandon one of its internal
 * synchronization states.  These are death tests because the diagnostic
 * deliberately terminates the process instead of attempting recovery from
 * undefined behavior.
 *
 * Each sleep-family wrapper is tested separately.  Polling the internal
 * marker makes cancellation deterministic: the parent does not merely hope
 * a scheduling delay lands inside the wrapper.  The three current-thread
 * cases cover every non-redirect delivery gateway: direct self-cancel, a
 * pending request becoming enabled, and a pending request becoming async.
 * Finally, deferred cancellation inside the same marked wait and async
 * cancellation outside it prove the check does not reject defined uses.
 *
 * The -win suffix is intentional.  Under Wine, NtTerminateProcess() ends
 * each diagnostic child, but the corresponding NT process handle is not
 * signalled, so an ntlibc parent cannot reap it.  Running any child mode
 * directly under Wine still exercises the detector; native Windows runs
 * this parent-side status adjudication in the normal suite.
 */
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;
extern int __spawn(const char *, char *const *, char *const *);
extern void __pthread_cancel_unsafe_enter(const char *);
extern void __pthread_cancel_unsafe_leave(void);
extern int __pthread_cancel_unsafe_active(pthread_t);

#define SURVIVED_EXIT 70

enum wait_kind {
	WAIT_NANOSLEEP,
	WAIT_SLEEP,
	WAIT_USLEEP,
	WAIT_CLOCK_RELATIVE,
	WAIT_CLOCK_ABSOLUTE,
	WAIT_PAUSE
};

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void *unsafe_waiter(void *argument)
{
	enum wait_kind kind = (enum wait_kind)(intptr_t)argument;
	struct timespec delay = { 30, 0 };
	struct timespec deadline;

	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0) != 0)
		return (void *)(intptr_t)71;
	switch (kind) {
	case WAIT_NANOSLEEP:
		nanosleep(&delay, 0);
		break;
	case WAIT_SLEEP:
		sleep(30);
		break;
	case WAIT_USLEEP:
		usleep(30000000u);
		break;
	case WAIT_CLOCK_RELATIVE:
		clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, 0);
		break;
	case WAIT_CLOCK_ABSOLUTE:
		if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
			return (void *)(intptr_t)72;
		deadline.tv_sec += 30;
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, 0);
		break;
	case WAIT_PAUSE:
		pause();
		break;
	}
	return (void *)(intptr_t)SURVIVED_EXIT;
}

static int wait_until_unsafe(pthread_t thread)
{
	int i;
	for (i = 0; i < 1000000; i++) {
		if (__pthread_cancel_unsafe_active(thread)) return 0;
		sched_yield();
	}
	return -1;
}

static int child_remote(enum wait_kind kind)
{
	pthread_t thread;
	void *result = 0;
	if (pthread_create(&thread, 0, unsafe_waiter,
	    (void *)(intptr_t)kind) != 0) return 73;
	if (wait_until_unsafe(thread) != 0) return 74;
	if (pthread_cancel(thread) != 0) return 75;
	if (pthread_join(thread, &result) != 0) return 76;
	return result == PTHREAD_CANCELED ? SURVIVED_EXIT : 77;
}

static int child_self_cancel(void)
{
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0) != 0)
		return 71;
	__pthread_cancel_unsafe_enter("self-cancel test region");
	pthread_cancel(pthread_self());
	__pthread_cancel_unsafe_leave();
	return SURVIVED_EXIT;
}

static int child_enable_pending(void)
{
	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) != 0) return 71;
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0) != 0)
		return 72;
	if (pthread_cancel(pthread_self()) != 0) return 73;
	__pthread_cancel_unsafe_enter("cancel-enable test region");
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
	__pthread_cancel_unsafe_leave();
	return SURVIVED_EXIT;
}

static int child_make_pending_async(void)
{
	if (pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0) != 0)
		return 71;
	if (pthread_cancel(pthread_self()) != 0) return 72;
	__pthread_cancel_unsafe_enter("cancel-type test region");
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);
	__pthread_cancel_unsafe_leave();
	return SURVIVED_EXIT;
}

static void *deferred_waiter(void *unused)
{
	struct timespec delay = { 30, 0 };
	(void)unused;
	nanosleep(&delay, 0);
	return (void *)(intptr_t)SURVIVED_EXIT;
}

static int control_deferred_inside_unsafe(void)
{
	pthread_t thread;
	void *result = 0;
	if (pthread_create(&thread, 0, deferred_waiter, 0) != 0) return -1;
	if (wait_until_unsafe(thread) != 0) return -1;
	if (pthread_cancel(thread) != 0) return -1;
	if (pthread_join(thread, &result) != 0) return -1;
	return result == PTHREAD_CANCELED ? 0 : -1;
}

static void *safe_async_waiter(void *unused)
{
	volatile unsigned long value = 0;
	(void)unused;
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0) != 0)
		return (void *)(intptr_t)71;
	for (;;) value++;
}

static int control_async_outside_unsafe(void)
{
	pthread_t thread;
	void *result = 0;
	int i;
	if (pthread_create(&thread, 0, safe_async_waiter, 0) != 0) return -1;
	for (i = 0; i < 1000; i++) sched_yield();
	if (pthread_cancel(thread) != 0) return -1;
	if (pthread_join(thread, &result) != 0) return -1;
	return result == PTHREAD_CANCELED ? 0 : -1;
}

static int run_child(const char *self, const char *mode, int *status,
	char *output, size_t capacity)
{
	char *argv[3];
	ssize_t count;
	size_t used = 0;
	int pipefd[2], saved, pid;
	if (capacity == 0 || pipe(pipefd) != 0) return -1;
	saved = dup(STDERR_FILENO);
	if (saved < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
	if (dup2(pipefd[1], STDERR_FILENO) < 0) {
		close(saved); close(pipefd[0]); close(pipefd[1]); return -1;
	}
	close(pipefd[1]);
	argv[0] = (char *)self;
	argv[1] = (char *)mode;
	argv[2] = 0;
	pid = __spawn(self, argv, environ);
	dup2(saved, STDERR_FILENO);
	close(saved);
	if (pid < 0) { close(pipefd[0]); return -1; }
	if (waitpid(pid, status, 0) != pid) { close(pipefd[0]); return -1; }
	while (used + 1 < capacity &&
	    (count = read(pipefd[0], output + used, capacity - used - 1)) > 0)
		used += (size_t)count;
	close(pipefd[0]);
	output[used] = 0;
	return 0;
}

static void expect_ub(const char *self, const char *mode, const char *region)
{
	char output[256] = { 0 };
	int status = 0;
	CHECK(run_child(self, mode, &status, output, sizeof output) == 0);
	CHECK(WIFSIGNALED(status));
	if (WIFSIGNALED(status)) CHECK(WTERMSIG(status) == SIGABRT);
	CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == SURVIVED_EXIT));
	CHECK(strstr(output, "ntlibc: undefined behavior: asynchronous cancellation during ") != 0);
	CHECK(strstr(output, region) != 0);
}

int main(int argc, char **argv)
{
	static const char *const remote_modes[] = {
		"--nanosleep", "--sleep", "--usleep", "--clock-relative",
		"--clock-absolute", "--pause"
	};
	static const char *const remote_regions[] = {
		"nanosleep()", "sleep()", "usleep()", "clock_nanosleep()",
		"clock_nanosleep()", "pause()"
	};
	int i;

	if (argc > 1) {
		for (i = 0; i < (int)(sizeof remote_modes / sizeof remote_modes[0]);
		    i++)
			if (!strcmp(argv[1], remote_modes[i])) return child_remote(i);
		if (!strcmp(argv[1], "--self-cancel")) return child_self_cancel();
		if (!strcmp(argv[1], "--enable-pending")) return child_enable_pending();
		if (!strcmp(argv[1], "--make-pending-async"))
			return child_make_pending_async();
		if (!strcmp(argv[1], "--control-deferred"))
			return control_deferred_inside_unsafe() != 0;
		if (!strcmp(argv[1], "--control-async"))
			return control_async_outside_unsafe() != 0;
		return 78;
	}

	for (i = 0; i < (int)(sizeof remote_modes / sizeof remote_modes[0]); i++)
		expect_ub(argv[0], remote_modes[i], remote_regions[i]);
	expect_ub(argv[0], "--self-cancel", "self-cancel test region");
	expect_ub(argv[0], "--enable-pending", "cancel-enable test region");
	expect_ub(argv[0], "--make-pending-async", "cancel-type test region");
	CHECK(control_deferred_inside_unsafe() == 0);
	CHECK(control_async_outside_unsafe() == 0);

	if (fails) printf("pthread-async-ub: %d failure(s)\n", fails);
	else printf("pthread-async-ub: ok\n");
	return fails != 0;
}
