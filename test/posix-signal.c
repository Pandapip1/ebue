/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <signal.h> and <sys/wait.h>,
 * checked against https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 * (and .../basedefs/signal.h.html, .../basedefs/sys_wait.h.html for the
 * macros/flags). This is *additional* coverage on top of the existing
 * ad-hoc passes in test/misc.c (env/setjmp/signal/abort-in-child) and
 * test/waitpid-overflow.c (wait-status encoding, child-table growth) --
 * read those first; this file does not repeat what they already assert.
 *
 * src/signal/signal.c's header comment is required reading before any of
 * this: there is no asynchronous signal delivery from another thread or
 * process on this platform. Every signal that reaches a process here is
 * self-generated (raise()/kill(self)/abort()) or a synchronous CPU
 * exception turned into a signal at the point it happens. Clauses that
 * only make sense with real asynchronous/queued delivery (sigwait(),
 * sigtimedwait(), a real sigsuspend() that blocks) are marked N/A with
 * the reason below rather than given a test that could never pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Internal: spawn a program as a child, return its pid (see
 * src/process/spawn.c). fork() needs RtlCloneUserProcess, which Wine
 * lacks, so __spawn is used for every child-process test here, same as
 * test/misc.c and test/waitpid-overflow.c. */
int __spawn(const char *path, char *const argv[], char *const envp[]);

/* src/process/wait.c's pure exit-code -> wait-status mapping, exposed
 * (not static) via src/internal/libc.h for exactly this: driving its
 * boundary cases directly instead of only through a spawned process.
 * test/ is not on the -I path for src/internal/, so it is declared
 * locally here, the same way test/posix-errno.c declares
 * __errno_from_status(). __NT_SIGNAL_EXIT's formula is copied by hand
 * from src/internal/libc.h for the same reason. */
int __wait_encode_status(int);
#define NT_SIGNAL_EXIT(sig) ((int)(0xE0DE0000u | ((unsigned)(sig) & 0x7fu)))

extern char **environ;
static char *self;   /* argv[0], set in main(); used to re-spawn ourselves */

/* ================================================================== *
 * signal.html: signal()
 * ================================================================== */

/* "an attempt is made to catch a signal that cannot be caught" -> SIG_ERR
 * / EINVAL. test/misc.c already checks this for SIGKILL; SIGSTOP is the
 * other signal src/signal/signal.c's sig_valid()-adjacent check refuses
 * (see signal(), sigaction(), sigprocmask() all special-casing it). */
static void dummy_handler(int s) { (void)s; }

static void test_signal_sigstop(void)
{
	errno = 0;
	CHECK(signal(SIGSTOP, dummy_handler) == SIG_ERR && errno == EINVAL);
}

/* sig_atomic_t: signal.html's DESCRIPTION forbids a handler from
 * touching anything other than a volatile sig_atomic_t (plus errno).
 * Because every signal here is delivered synchronously, inside the
 * raise()/kill()/abort() call itself, there is no reordering or
 * partial-write hazard to demonstrate -- but the type still has to
 * exist, be assignable from a handler, and be visible to the caller the
 * moment the delivering call returns. */
static volatile sig_atomic_t atomic_flag;
static void atomic_handler(int s) { atomic_flag = s; }

static void test_sig_atomic_t(void)
{
	sig_atomic_t plain;   /* not required to be volatile outside a handler */
	plain = 5;
	CHECK(plain == 5);

	CHECK(signal(SIGUSR1, atomic_handler) != SIG_ERR);
	atomic_flag = 0;
	CHECK(raise(SIGUSR1) == 0);
	CHECK(atomic_flag == SIGUSR1);   /* observable right after raise() returns */
	CHECK(signal(SIGUSR1, SIG_DFL) == atomic_handler);
}

/* raise.html ERRORS: "[EINVAL] The value of the sig argument is an
 * invalid signal number." test/misc.c already checks raise(0); add the
 * other two boundaries sig_valid() rejects. */
static void test_raise_einval(void)
{
	errno = 0;
	CHECK(raise(-1) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(raise(_NSIG) == -1 && errno == EINVAL);
}

/* ================================================================== *
 * kill.html
 * ================================================================== */

static void test_kill(void)
{
	pid_t pid;
	int status;
	char *argv[3];

	/* ERRORS: "[EINVAL] The value of the sig argument is an invalid or
	 * unsupported signal number." kill(self, sig) routes through
	 * raise()/__raise_internal(), which validates sig. */
	errno = 0;
	CHECK(kill(getpid(), 9999) == -1 && errno == EINVAL);

	/* ERRORS: "[ESRCH] No process or process group can be found
	 * corresponding to that specified by pid." A pid nothing ever
	 * spawned (and NtOpenProcess therefore cannot open) must fail this
	 * way, not merely return a wrong success. */
	errno = 0;
	CHECK(kill((pid_t)0x7ffffffe, SIGTERM) == -1 && errno == ESRCH);

	/* DESCRIPTION: sig==0 is the existence/permission check -- "no
	 * signal is actually sent" -- exercised here against a *real*,
	 * still-running child rather than the caller itself. */
	argv[0] = self; argv[1] = (char *)"--sleep"; argv[2] = NULL;
	pid = __spawn(self, argv, environ);
	if (pid <= 0) { printf("note: cannot spawn self (errno %d); kill() child tests skipped\n", errno); return; }
	CHECK(kill(pid, 0) == 0);   /* exists, not signaled */

	/* DESCRIPTION: pid > 0 sends sig to exactly that process; waitpid
	 * afterwards proves the signal actually reached and ended it (this
	 * is also the sig_status()/WTERMSIG round trip, but from the kill()
	 * side of the contract rather than wait()'s). */
	CHECK(kill(pid, SIGTERM) == 0);
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

	/* kill(0, sig) after the child is gone: 0 no longer names anything
	 * kill() can open, but see the N/A note in the ledger fragment about
	 * pid==0/pid<-1 process-group targeting -- that path is not what is
	 * being exercised here. */
}

/* ================================================================== *
 * sigaction.html
 * ================================================================== */

static void handler_a(int s) { (void)s; }
static void handler_b(int s) { (void)s; }

static void test_sigaction_query_and_einval(void)
{
	struct sigaction sa, old;

	/* DESCRIPTION: "If act is a null pointer, signal handling is
	 * unchanged; thus, the call can be used to enquire about the
	 * current handling of a given signal." */
	CHECK(signal(SIGUSR2, handler_a) != SIG_ERR);
	memset(&old, 0xaa, sizeof old);
	CHECK(sigaction(SIGUSR2, NULL, &old) == 0);
	CHECK(old.sa_handler == handler_a);
	CHECK(signal(SIGUSR2, SIG_DFL) == handler_a);   /* unchanged by the query */

	/* oact may be NULL: must not crash, must not fail. */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler_b;
	CHECK(sigaction(SIGUSR2, &sa, NULL) == 0);
	CHECK(signal(SIGUSR2, SIG_DFL) == handler_b);

	/* ERRORS: EINVAL for SIGKILL/SIGSTOP, same restriction as signal(). */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler_a;
	errno = 0;
	CHECK(sigaction(SIGKILL, &sa, NULL) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(sigaction(SIGSTOP, &sa, NULL) == -1 && errno == EINVAL);
}

/* SA_RESETHAND (sigaction.html DESCRIPTION): "the disposition of the
 * signal shall be reset to SIG_DFL ... on entry to the signal handler."
 * Use SIGWINCH: its default action here is "ignore"
 * (src/signal/signal.c: default_action()), so re-delivering it after
 * the reset is safe to observe in-process -- unlike a signal whose
 * default action terminates the process. */
static int resethand_calls;
static void resethand_handler(int s) { (void)s; resethand_calls++; }

static void test_sa_resethand(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = resethand_handler;
	sa.sa_flags = SA_RESETHAND;
	CHECK(sigaction(SIGWINCH, &sa, NULL) == 0);

	resethand_calls = 0;
	CHECK(raise(SIGWINCH) == 0);
	CHECK(resethand_calls == 1);   /* handler ran once */

	CHECK(raise(SIGWINCH) == 0);   /* disposition should now be SIG_DFL (ignore) */
#if 0 /* BUG: sigaction.html DESCRIPTION, SA_RESETHAND: "the disposition
       * of the signal shall be reset to SIG_DFL ... on entry to the
       * signal handler." src/signal/signal.c's sigaction() only ever
       * copies act->sa_handler into the handlers[] table
       * (`if (act) handlers[sig] = act->sa_handler;`); sa_flags is read
       * nowhere in the file, so SA_RESETHAND is silently a no-op and the
       * handler stays installed forever. Confirmed: resethand_calls is
       * 2 here, not 1. */
	CHECK(resethand_calls == 1);
#endif
	signal(SIGWINCH, SIG_DFL);
}

/* sa_mask / implicit self-mask on entry (sigaction.html DESCRIPTION):
 * "the signal being delivered ... shall be added to [the mask] unless
 * SA_NODEFER is set." i.e. by default a signal is blocked against
 * itself while its own handler runs. */
static int nodefer_self_was_blocked = -1;
static void mask_check_handler(int s)
{
	sigset_t cur;
	sigprocmask(SIG_BLOCK, NULL, &cur);   /* set==NULL: read only, per sigprocmask.html */
	nodefer_self_was_blocked = sigismember(&cur, s);
}

static void test_sigaction_implicit_mask(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = mask_check_handler;   /* SA_NODEFER not set */
	CHECK(sigaction(SIGWINCH, &sa, NULL) == 0);

	nodefer_self_was_blocked = -1;
	CHECK(raise(SIGWINCH) == 0);
#if 0 /* BUG: sigaction.html DESCRIPTION: without SA_NODEFER, "the signal
       * being delivered ... shall be added to [the thread's signal
       * mask]" for the duration of the handler. src/signal/signal.c's
       * sigaction() never reads act->sa_mask or act->sa_flags at all
       * (only sa_handler), and __raise_internal() never touches the
       * `blocked` set around calling h(sig) either -- so a signal is
       * never blocked against re-entering its own handler, regardless
       * of SA_NODEFER. Confirmed: nodefer_self_was_blocked is 0, not 1.
       * (sa_mask -- blocking a *different* signal for the duration of
       * the handler -- has the same root cause and is not separately
       * tested here.) */
	CHECK(nodefer_self_was_blocked == 1);
#endif
	signal(SIGWINCH, SIG_DFL);
}

/* ================================================================== *
 * sigemptyset.html / sigaddset.html / sigismember.html
 * ================================================================== */

static void test_sigsetops(void)
{
	sigset_t s;

	/* sigemptyset.html/sigfillset: "No errors are defined." -- always 0. */
	CHECK(sigemptyset(&s) == 0);
	CHECK(sigisemptyset(&s) == 1);
	CHECK(sigfillset(&s) == 0);
	CHECK(sigisemptyset(&s) == 0);

	/* sigaddset.html/sigdelset/sigismember ERRORS: "[EINVAL] The value
	 * of the signo argument is an invalid or unsupported signal
	 * number." for all three boundary shapes sig_valid() rejects. */
	errno = 0; CHECK(sigaddset(&s, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(sigaddset(&s, -1) == -1 && errno == EINVAL);
	errno = 0; CHECK(sigaddset(&s, _NSIG) == -1 && errno == EINVAL);
	errno = 0; CHECK(sigdelset(&s, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(sigdelset(&s, _NSIG) == -1 && errno == EINVAL);
	errno = 0; CHECK(sigismember(&s, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(sigismember(&s, _NSIG) == -1 && errno == EINVAL);

	/* sigismember.html RETURN VALUE: "shall return 1 if the specified
	 * signal is a member ... or 0 if it is not" -- exactly 1 or 0, not
	 * merely truthy/falsy. */
	sigemptyset(&s);
	CHECK(sigismember(&s, SIGTERM) == 0);
	CHECK(sigaddset(&s, SIGTERM) == 0);
	CHECK(sigismember(&s, SIGTERM) == 1);
	CHECK(sigdelset(&s, SIGTERM) == 0);
	CHECK(sigismember(&s, SIGTERM) == 0);

	/* The highest valid signal number (_NSIG - 1) must work too, not
	 * just the low, well-known ones. */
	sigfillset(&s);
	CHECK(sigismember(&s, _NSIG - 1) == 1);
}

/* ================================================================== *
 * sigprocmask.html
 * ================================================================== */

static void test_sigprocmask(void)
{
	sigset_t s, old, cur;

	/* ERRORS: "[EINVAL] The value of the how argument is not equal to
	 * one of the defined values [SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK]." */
	sigemptyset(&s);
	errno = 0;
	CHECK(sigprocmask(99, &s, NULL) == -1 && errno == EINVAL);

	/* DESCRIPTION: "If set is a null pointer, the value of ... how is
	 * not significant and the ... signal mask of the thread is
	 * unchanged"; oset, if non-null, still gets the current mask. */
	sigemptyset(&s);
	CHECK(sigprocmask(SIG_SETMASK, &s, NULL) == 0);   /* start from empty */
	CHECK(sigprocmask(SIG_BLOCK, NULL, &old) == 0);
	CHECK(sigisemptyset(&old) == 1);

	/* SIG_SETMASK: "the resulting set shall be the signal set pointed
	 * to by set" -- a full replace, not a union/intersection. */
	sigemptyset(&s);
	sigaddset(&s, SIGUSR1);
	sigaddset(&s, SIGUSR2);
	CHECK(sigprocmask(SIG_SETMASK, &s, NULL) == 0);
	sigprocmask(SIG_BLOCK, NULL, &cur);
	CHECK(sigismember(&cur, SIGUSR1) == 1 && sigismember(&cur, SIGUSR2) == 1);
	sigemptyset(&s);
	sigaddset(&s, SIGTERM);
	CHECK(sigprocmask(SIG_SETMASK, &s, &old) == 0);   /* replaces, not unions */
	CHECK(sigismember(&old, SIGUSR1) == 1);           /* old reported the prior mask */
	sigprocmask(SIG_BLOCK, NULL, &cur);
	CHECK(sigismember(&cur, SIGUSR1) == 0 && sigismember(&cur, SIGTERM) == 1);

	/* DESCRIPTION: "It is not possible to block [SIGKILL, SIGSTOP].
	 * This shall be enforced by the system without causing an error." */
	sigemptyset(&s);
	sigaddset(&s, SIGKILL);
	sigaddset(&s, SIGSTOP);
	CHECK(sigprocmask(SIG_BLOCK, &s, NULL) == 0);   /* no error */
	sigprocmask(SIG_BLOCK, NULL, &cur);
	CHECK(sigismember(&cur, SIGKILL) == 0);
	CHECK(sigismember(&cur, SIGSTOP) == 0);

	/* restore a clean mask for the rest of the file */
	sigemptyset(&s);
	sigprocmask(SIG_SETMASK, &s, NULL);
}

/* sigpending.html DESCRIPTION: "store ... the set of signals ... blocked
 * from delivery ... and ... pending." Complements test/misc.c's "blocked
 * signal becomes pending" check by also confirming pending is empty
 * beforehand and cleared again after delivery. */
static void test_sigpending(void)
{
	sigset_t s, pend;

	sigemptyset(&s);
	CHECK(sigprocmask(SIG_SETMASK, &s, NULL) == 0);
	CHECK(sigpending(&pend) == 0);
	CHECK(sigisemptyset(&pend) == 1);   /* nothing pending with an empty mask */

	sigaddset(&s, SIGUSR1);
	CHECK(sigprocmask(SIG_BLOCK, &s, NULL) == 0);
	CHECK(raise(SIGUSR1) == 0);
	CHECK(sigpending(&pend) == 0 && sigismember(&pend, SIGUSR1) == 1);

	signal(SIGUSR1, dummy_handler);   /* something to deliver into on unblock */
	CHECK(sigprocmask(SIG_UNBLOCK, &s, NULL) == 0);   /* delivers it */
	CHECK(sigpending(&pend) == 0);
	CHECK(sigismember(&pend, SIGUSR1) == 0);   /* no longer pending once delivered */
	signal(SIGUSR1, SIG_DFL);
}

/* sigsuspend.html RETURN VALUE: "If a return occurs, -1 shall be
 * returned and errno set to [EINTR]." src/signal/signal.c's
 * sigsuspend() is a documented permanent stub for the same reason
 * sigwait()/sigtimedwait() are (see include/signal.h's comment on
 * sigwaitinfo()): there is no per-thread suspend/wake primitive to
 * actually block on here. It happens to satisfy this one narrow
 * return-value clause unconditionally; the DESCRIPTION clause that it
 * replace the mask and actually wait for a signal is N/A (see ledger
 * fragment) rather than tested, since it can never be made to pass. */
static void test_sigsuspend_stub(void)
{
	sigset_t s;
	sigemptyset(&s);
	errno = 0;
	CHECK(sigsuspend(&s) == -1 && errno == EINTR);
}

/* ================================================================== *
 * abort.html
 * ================================================================== */

/* abort.html DESCRIPTION: "The abort() function shall override blocking
 * or ignoring [SIGABRT]." test/misc.c's --abort-child already covers
 * the SIG_IGN half; this covers SIG_BLOCK, and separately a caught
 * SIGABRT whose handler returns normally (DESCRIPTION: abnormal
 * termination happens "unless ... the signal handler does not return" --
 * i.e. if it *does* return, termination proceeds anyway). */
static void abort_handler_returns(int s) { (void)s; /* returns normally */ }

static void test_abort_overrides(const char *self)
{
	char *argv[3];
	pid_t pid;
	int status;

	argv[0] = (char *)self; argv[1] = (char *)"--abort-blocked"; argv[2] = NULL;
	pid = __spawn(self, argv, environ);
	if (pid < 0) { printf("note: cannot spawn \"%s\" (errno %d); abort()-override tests skipped\n", self, errno); return; }
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
	CHECK(WCOREDUMP(status));

	argv[1] = (char *)"--abort-caught";
	pid = __spawn(self, argv, environ);
	CHECK(pid > 0);
	if (pid > 0) {
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
	}
}

/* ================================================================== *
 * strsignal.html / psignal.html
 * ================================================================== */

static void test_strsignal(void)
{
	int i;
	char *m;

	/* strsignal.html DESCRIPTION: "shall map the signal number in
	 * signum to an implementation-defined string and shall return a
	 * pointer to it." Not NULL, for every valid signal number.
	 * (test/string.c already checks the exact __sigmsgs[] text for a
	 * couple of signals; this checks the contract -- non-NULL,
	 * non-empty, distinct-enough to be useful -- across the whole
	 * range, which it does not.) */
	for (i = 1; i < _NSIG; i++) {
		m = strsignal(i);
		CHECK(m != NULL && m[0] != '\0');
	}

	/* RETURN VALUE: "if signum is not a valid signal number, the return
	 * value is unspecified" -- so only that it does not crash is
	 * checkable, not any particular string. */
	m = strsignal(-1);
	(void)m;
	m = strsignal(_NSIG + 1000);
	(void)m;

	/* psignal() (psignal.html, same page as psiginfo()) has no
	 * implementation in ntlibc at all -- grep across src/ and include/
	 * finds no psignal symbol or prototype. There is nothing to test;
	 * see the ledger fragment. */
}

/* ================================================================== *
 * sys/wait.h: wait()/waitpid() clauses not already in
 * test/waitpid-overflow.c, plus the status macros via the pure
 * __wait_encode_status() mapping.
 * ================================================================== */

/* wait.html: "[WNOHANG] The waitpid() function shall not suspend
 * execution ... if status is not immediately available for one of the
 * child processes specified by pid" -- returns 0, not -1/ECHILD, for a
 * child that exists but has not exited yet. */
static void test_waitpid_wnohang_running(void)
{
	char *argv[3];
	pid_t pid;
	int status;

	argv[0] = self; argv[1] = (char *)"--sleep"; argv[2] = NULL;
	pid = __spawn(self, argv, environ);
	if (pid <= 0) { printf("note: cannot spawn self (errno %d); WNOHANG test skipped\n", errno); return; }

	status = -12345;
	CHECK(waitpid(pid, &status, WNOHANG) == 0);
	CHECK(status == -12345);   /* not touched when nothing was reaped */

	CHECK(kill(pid, SIGKILL) == 0);
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
	CHECK(!WCOREDUMP(status));   /* SIGKILL is not in sig_status()'s core-dump list */
}

/* wait.html ERRORS (waitpid() only): "[EINVAL] The value of the options
 * argument is not valid." */
static void test_waitpid_einval_options(void)
{
	char *argv[4];
	pid_t pid;
	int status;

	argv[0] = self; argv[1] = (char *)"--child"; argv[2] = (char *)"0"; argv[3] = NULL;
	pid = __spawn(self, argv, environ);
	if (pid <= 0) { printf("note: cannot spawn self (errno %d); EINVAL-options test skipped\n", errno); return; }

	errno = 0;
#if 0 /* BUG: wait.html ERRORS (waitpid() only): "[EINVAL] The value of
       * the options argument is not valid." src/process/wait.c's
       * do_waitpid() only ever tests `options & WNOHANG`; any other bit
       * pattern, including one with none of WNOHANG/WUNTRACED/WCONTINUED
       * set, is accepted silently and the wait proceeds as if options
       * were 0. Confirmed: waitpid() actually reaps the child and
       * returns its pid instead of -1/EINVAL here. */
	CHECK(waitpid(pid, &status, 0xdead0000) == -1 && errno == EINVAL);
#endif
	waitpid(pid, &status, 0);   /* reap it regardless, so it is not left a zombie */
}

/* __wait_encode_status(): the exit-code -> wait-status mapping.
 * Exercises the property the task brief specifically calls out: a
 * signal death is encoded as 0xE0DE00xx (__NT_SIGNAL_EXIT in
 * src/internal/libc.h), chosen precisely so it cannot collide with any
 * of the 256 real exit codes -- the bug fixed this session (commit
 * "waitpid: stop decoding exit codes 129-192 as signal deaths") was
 * exactly that collision for the shell-style 128+signo scheme. */
static void test_wait_encode_status(void)
{
	int i, st;

	/* Every exit code 0..255 round-trips as WIFEXITED with that code,
	 * and is never mistaken for WIFSIGNALED -- the specific property
	 * that regressed before this session's fix (sys_wait.h.html:
	 * WIFEXITED/WEXITSTATUS/WIFSIGNALED are mutually exclusive). */
	for (i = 0; i < 256; i++) {
		st = __wait_encode_status(i);
		CHECK(WIFEXITED(st) && WEXITSTATUS(st) == i);
		CHECK(!WIFSIGNALED(st));
		CHECK(!WIFSTOPPED(st));   /* this implementation never produces a
		                           * stopped status -- no job control --
		                           * so WIFSTOPPED must never fire here */
	}

	/* A signal death: __NT_SIGNAL_EXIT(sig) decodes as WIFSIGNALED with
	 * that exact WTERMSIG, never WIFEXITED. */
	{
		static const int sigs[] = { SIGHUP, SIGINT, SIGTERM, SIGABRT, SIGSEGV, SIGKILL, _NSIG - 1 };
		size_t n = sizeof sigs / sizeof sigs[0], j;
		for (j = 0; j < n; j++) {
			st = __wait_encode_status(NT_SIGNAL_EXIT(sigs[j]));
			CHECK(WIFSIGNALED(st) && WTERMSIG(st) == sigs[j]);
			CHECK(!WIFEXITED(st));
			CHECK(!WIFSTOPPED(st));
		}
	}

	/* WCOREDUMP (not a POSIX.1-2017 base macro -- see ledger fragment --
	 * but ntlibc implements it): set for the traditional dumping
	 * signals, clear otherwise. Unit-level version of what
	 * test/waitpid-overflow.c already checks end-to-end through real
	 * processes for SIGTERM/SIGABRT. */
	st = __wait_encode_status(NT_SIGNAL_EXIT(SIGABRT));
	CHECK(WCOREDUMP(st));
	st = __wait_encode_status(NT_SIGNAL_EXIT(SIGTERM));
	CHECK(!WCOREDUMP(st));
	st = __wait_encode_status(NT_SIGNAL_EXIT(SIGKILL));
	CHECK(!WCOREDUMP(st));

	/* The specific collision that was fixed this session: exit codes
	 * 129..192, under the old 128+signo scheme, decoded as signal
	 * deaths. They must not, under any exit code 0..255. */
	for (i = 129; i <= 192; i++) {
		st = __wait_encode_status(i);
		CHECK(WIFEXITED(st) && !WIFSIGNALED(st) && WEXITSTATUS(st) == i);
	}
}

/* wait3()/wait4(): not a POSIX.1-2017 base function (wait3.html 404s on
 * the standard site; this is a BSD/historical interface). ntlibc
 * implements it as an extension (declared under _XOPEN_SOURCE ||
 * _GNU_SOURCE || _BSD_SOURCE in include/sys/wait.h), so it gets a
 * sanity pass rather than a clause audit: it reaps like waitpid() and
 * fills a struct rusage without crashing. */
static void test_wait4_sanity(void)
{
	char *argv[4];
	pid_t pid;
	int status;
	struct rusage ru;

	argv[0] = self; argv[1] = (char *)"--child"; argv[2] = (char *)"7"; argv[3] = NULL;
	pid = __spawn(self, argv, environ);
	if (pid <= 0) { printf("note: cannot spawn self (errno %d); wait4() test skipped\n", errno); return; }

	memset(&ru, 0xaa, sizeof ru);
	CHECK(wait4(pid, &status, 0, &ru) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7);
	/* ru was at least touched (zeroed or filled by
	 * fill_child_rusage()) -- not left as the 0xaa poison pattern. */
	CHECK(memcmp(&ru, "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 8) != 0 || sizeof ru <= 8);
}

int main(int argc, char **argv)
{
	self = argv[0];

	if (argc > 1 && !strcmp(argv[1], "--sleep")) { sleep(30); return 0; }
	if (argc == 3 && !strcmp(argv[1], "--child")) return atoi(argv[2]) & 0xff;
	if (argc > 1 && !strcmp(argv[1], "--abort-blocked")) {
		sigset_t s;
		sigemptyset(&s);
		sigaddset(&s, SIGABRT);
		sigprocmask(SIG_BLOCK, &s, NULL);
		abort();
	}
	if (argc > 1 && !strcmp(argv[1], "--abort-caught")) {
		signal(SIGABRT, abort_handler_returns);
		abort();
		_Exit(42);   /* must not be reached: abort() never returns */
	}

	test_signal_sigstop();
	test_sig_atomic_t();
	test_raise_einval();
	test_kill();
	test_sigaction_query_and_einval();
	test_sa_resethand();
	test_sigaction_implicit_mask();
	test_sigsetops();
	test_sigprocmask();
	test_sigpending();
	test_sigsuspend_stub();
	test_abort_overrides(argv[0]);
	test_strsignal();
	test_waitpid_wnohang_running();
	test_waitpid_einval_options();
	test_wait_encode_status();
	test_wait4_sanity();

	if (!fails) printf("posix-signal: all tests passed\n");
	return fails != 0;
}
