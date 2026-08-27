/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <libgen.h>
#include <locale.h>
#include <assert.h>
#include <unistd.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern char **environ;
/* Internal: spawn a program as a child, return its pid (see src/process/spawn.c).
 * fork() needs RtlCloneUserProcess, which wine lacks, so this is used to
 * run abort() in a child. */
int __spawn(const char *path, char *const argv[], char *const envp[]);

/* ---- env ---- */
static int env_has(const char *prefix)
{
	char **e;
	size_t l = strlen(prefix);
	for (e = environ; e && *e; e++) if (!strncmp(*e, prefix, l)) return 1;
	return 0;
}

static void test_env(void)
{
	static char pe[] = "NTLIBC_PUTENV=pv";

	CHECK(getenv("NTLIBC_NO_SUCH_VAR_XYZ") == NULL);
	CHECK(getenv("") == NULL);
	CHECK(getenv("A=B") == NULL);

	CHECK(setenv("NTLIBC_T1", "one", 1) == 0);
	CHECK(getenv("NTLIBC_T1") && !strcmp(getenv("NTLIBC_T1"), "one"));
	CHECK(env_has("NTLIBC_T1=one"));
	CHECK(setenv("NTLIBC_T1", "two", 0) == 0);
	CHECK(getenv("NTLIBC_T1") && !strcmp(getenv("NTLIBC_T1"), "one"));
	CHECK(setenv("NTLIBC_T1", "two", 1) == 0);
	CHECK(getenv("NTLIBC_T1") && !strcmp(getenv("NTLIBC_T1"), "two"));
	CHECK(env_has("NTLIBC_T1=two"));
	CHECK(!env_has("NTLIBC_T1=one"));
	CHECK(setenv("NTLIBC_EMPTY", "", 1) == 0);
	CHECK(getenv("NTLIBC_EMPTY") && !*getenv("NTLIBC_EMPTY"));
	CHECK(unsetenv("NTLIBC_T1") == 0);
	CHECK(getenv("NTLIBC_T1") == NULL);
	CHECK(!env_has("NTLIBC_T1="));
	CHECK(unsetenv("NTLIBC_T1") == 0);  /* not present: still success */

	errno = 0;
	CHECK(setenv("A=B", "x", 1) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(setenv("", "x", 1) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(unsetenv("") == -1 && errno == EINVAL);
	errno = 0;
	CHECK(unsetenv("A=B") == -1 && errno == EINVAL);

	CHECK(putenv(pe) == 0);
	CHECK(getenv("NTLIBC_PUTENV") && !strcmp(getenv("NTLIBC_PUTENV"), "pv"));
	CHECK(env_has("NTLIBC_PUTENV=pv"));
	pe[14] = 'q';  /* putenv strings are referenced, not copied */
	CHECK(getenv("NTLIBC_PUTENV") && !strcmp(getenv("NTLIBC_PUTENV"), "qv"));
	CHECK(unsetenv("NTLIBC_PUTENV") == 0);
	CHECK(getenv("NTLIBC_PUTENV") == NULL);
	CHECK(!env_has("NTLIBC_PUTENV="));
}

/* ---- process scheduling ---- */
static void test_sched(void)
{
	struct sched_param param;
	struct timespec interval;
	int min = sched_get_priority_min(SCHED_FIFO);
	int max = sched_get_priority_max(SCHED_FIFO);

	CHECK(min >= 0 && max >= min);
	CHECK(sched_getscheduler(0) == SCHED_OTHER);
	CHECK(sched_getparam(0, &param) == 0);
	param.sched_priority = min;
	CHECK(sched_setscheduler(0, SCHED_FIFO, &param) == 0);
	CHECK(sched_getscheduler(0) == SCHED_FIFO);
	param.sched_priority = max;
	CHECK(sched_setparam(0, &param) == 0);
	param.sched_priority = -1;
	CHECK(sched_getparam(0, &param) == 0 && param.sched_priority == max);
	CHECK(sched_rr_get_interval(0, &interval) == 0);
	CHECK(interval.tv_sec == 0 && interval.tv_nsec > 0);
	errno = 0;
	CHECK(sched_get_priority_max(-1) == -1 && errno == EINVAL);
	param.sched_priority = 0;
	CHECK(sched_setscheduler(0, SCHED_OTHER, &param) == 0);
}

/* ---- whole-process memory locking ---- */
static void test_mlockall(void)
{
	CHECK(mlockall(MCL_FUTURE) == 0);
	CHECK(munlockall() == 0);
	errno = 0;
	CHECK(mlockall(0) == -1 && errno == EINVAL);
}

/* ---- semaphores ---- */
static void test_semaphore(void)
{
	sem_t sem;
	sem_t *named;
	struct timespec past;
	int value;
	CHECK(sem_init(&sem, 0, 1) == 0);
	CHECK(sem_wait(&sem) == 0);
	CHECK(sem_trywait(&sem) == -1 && errno == EAGAIN);
	CHECK(clock_gettime(CLOCK_REALTIME, &past) == 0);
	past.tv_sec--;
	CHECK(sem_timedwait(&sem, &past) == -1 && errno == ETIMEDOUT);
	CHECK(sem_post(&sem) == 0);
	CHECK(sem_getvalue(&sem, &value) == 0 && value == 1);
	CHECK(sem_destroy(&sem) == 0);
	sem_unlink("/ntlibc_misc_sem");
	named = sem_open("/ntlibc_misc_sem", O_CREAT | O_EXCL, 0600, 1);
	CHECK(named != SEM_FAILED);
	if (named != SEM_FAILED) CHECK(sem_close(named) == 0);
	CHECK(sem_unlink("/ntlibc_misc_sem") == 0);
}

/* ---- system() ---- */
static void test_system(void)
{
	int status;

	/* A command processor must be available on any real Windows/Wine
	 * host this runs on (cmd.exe always exists); see
	 * src/stdlib/system.c.  The native asan build (tools/asan-build.sh)
	 * compiles this same source against NT stubs that emulate files,
	 * pipes and processes but not a shell -- there is genuinely no
	 * ComSpec and no "cmd.exe" on PATH there, so system(NULL) reporting
	 * 0 is the *correct* answer in that environment, not a bug.  Treat
	 * that as "nothing further to exercise here" rather than a
	 * failure. */
	if (system(NULL) == 0) return;

	status = system("exit 3");
	CHECK(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 3);

	status = system("exit 0");
	CHECK(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);

	/* A command containing a space exercises append_arg's quoting of
	 * the "/c" argument -- see the design comment in system.c for why
	 * this, and not an embedded quote or trailing backslash, is the
	 * case this is expected to round-trip through cmd.exe correctly. */
	status = system("cmd /c exit 7");
	CHECK(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 7);
}

/* ---- setjmp/longjmp ---- */
static jmp_buf jb_outer, jb_inner;

static void jump_from_frames(int depth, int val)
{
	volatile char pad[64];
	pad[0] = (char)depth;
	if (depth == 0) longjmp(jb_outer, val);
	jump_from_frames(depth - 1, val + pad[0] - depth);
}

static int recurse(int n)
{
	if (n == 0) longjmp(jb_outer, 100);
	return recurse(n - 1) + 1;
}

static void test_setjmp(void)
{
	volatile int v = 1, count = 0;
	int r;

	r = setjmp(jb_outer);
	if (r == 0) {
		v = 2;
		longjmp(jb_outer, 7);
		CHECK(0);
	}
	CHECK(r == 7);
	CHECK(v == 2);

	/* longjmp(env, 0) returns 1 */
	r = setjmp(jb_outer);
	if (r == 0) longjmp(jb_outer, 0);
	CHECK(r == 1);

	/* multiple returns through the same buffer */
	r = setjmp(jb_outer);
	count++;
	if (count < 5) longjmp(jb_outer, count);
	CHECK(count == 5);
	CHECK(r == 4);

	/* nested */
	r = setjmp(jb_outer);
	if (r == 0) {
		int s = setjmp(jb_inner);
		if (s == 0) longjmp(jb_inner, 3);
		CHECK(s == 3);
		longjmp(jb_outer, 4);
	}
	CHECK(r == 4);

	/* across several stack frames */
	r = setjmp(jb_outer);
	if (r == 0) { jump_from_frames(10, 42); CHECK(0); }
	CHECK(r == 42);

	/* out of a recursion 100 deep */
	r = setjmp(jb_outer);
	if (r == 0) { (void)recurse(100); CHECK(0); }
	CHECK(r == 100);

	/* the buffer must be usable again after all this */
	r = setjmp(jb_outer);
	if (r == 0) longjmp(jb_outer, 99);
	CHECK(r == 99);
}

/* ---- signal ---- */
static volatile sig_atomic_t got_sig, sig_calls;
static void on_sig(int s) { got_sig = s; sig_calls++; }

static void test_signal(void)
{
	void (*old)(int);

	old = signal(SIGINT, on_sig);
	CHECK(old == SIG_DFL);
	old = signal(SIGINT, SIG_DFL);
	CHECK(old == on_sig);

	CHECK(signal(SIGUSR1, on_sig) == SIG_DFL);
	got_sig = 0; sig_calls = 0;
	CHECK(raise(SIGUSR1) == 0);
	CHECK(got_sig == SIGUSR1);
	CHECK(sig_calls == 1);
	CHECK(raise(SIGUSR1) == 0);   /* handler stays installed */
	CHECK(sig_calls == 2);

	CHECK(signal(SIGUSR1, SIG_IGN) == on_sig);
	CHECK(raise(SIGUSR1) == 0);
	CHECK(sig_calls == 2);
	CHECK(signal(SIGUSR1, SIG_DFL) == SIG_IGN);

	/* kill(getpid(), sig) is raise() */
	CHECK(signal(SIGUSR2, on_sig) == SIG_DFL);
	CHECK(kill(getpid(), SIGUSR2) == 0);
	CHECK(got_sig == SIGUSR2);
	CHECK(kill(getpid(), 0) == 0);

	/* blocking defers delivery */
	{
		sigset_t s, pend;
		sigemptyset(&s);
		sigaddset(&s, SIGUSR2);
		sig_calls = 0;
		CHECK(sigprocmask(SIG_BLOCK, &s, NULL) == 0);
		CHECK(raise(SIGUSR2) == 0);
		CHECK(sig_calls == 0);
		CHECK(sigpending(&pend) == 0 && sigismember(&pend, SIGUSR2) == 1);
		CHECK(sigprocmask(SIG_UNBLOCK, &s, NULL) == 0);
		CHECK(sig_calls == 1);
	}

	errno = 0;
	CHECK(signal(0, on_sig) == SIG_ERR && errno == EINVAL);
	errno = 0;
	CHECK(signal(_NSIG, on_sig) == SIG_ERR && errno == EINVAL);
	errno = 0;
	CHECK(signal(-1, on_sig) == SIG_ERR && errno == EINVAL);
	errno = 0;
	CHECK(signal(SIGKILL, on_sig) == SIG_ERR && errno == EINVAL);
	errno = 0;
	CHECK(raise(0) == -1 && errno == EINVAL);

	/* abort() in the main process would fail the test; see test_abort_child */
}

/* ---- basename/dirname ---- */
static void check_base(const char *in, const char *base, const char *dir)
{
	char buf[64], *r;
	strcpy(buf, in);
	r = basename(buf);
	if (strcmp(r, base)) { fails++; printf("FAIL %s:%d: basename(\"%s\") = \"%s\", want \"%s\"\n", __FILE__, __LINE__, in, r, base); }
	strcpy(buf, in);
	r = dirname(buf);
	if (strcmp(r, dir)) { fails++; printf("FAIL %s:%d: dirname(\"%s\") = \"%s\", want \"%s\"\n", __FILE__, __LINE__, in, r, dir); }
}

static void test_libgen(void)
{
	check_base("/a/b/c", "c", "/a/b");
	check_base("/a/b/c/", "c", "/a/b");
	check_base("/a/b//c//", "c", "/a/b");
	check_base("a", "a", ".");
	check_base("", ".", ".");
	check_base("/", "/", "/");
	check_base("//", "/", "/");
	check_base("///", "/", "/");
	check_base("a/b", "b", "a");
	check_base("/a", "a", "/");
	check_base("a/", "a", ".");
	check_base("/usr/", "usr", "/");
	CHECK(!strcmp(basename(NULL), "."));
	CHECK(!strcmp(dirname(NULL), "."));
	/* Windows flavours */
	check_base("C:\\x\\y", "y", "C:\\x");
	check_base("C:\\", "\\", "C:\\");
	check_base("C:/foo", "foo", "C:/");
}

/* ---- locale ---- */
static void test_locale(void)
{
	char *l;
	struct lconv *lc;
	l = setlocale(LC_ALL, "C");
	CHECK(l && !strcmp(l, "C"));
	l = setlocale(LC_ALL, "");
	CHECK(l != NULL);
	l = setlocale(LC_ALL, NULL);
	CHECK(l && !strcmp(l, "C"));
	l = setlocale(LC_ALL, "POSIX");
	CHECK(l && !strcmp(l, "C"));
	l = setlocale(LC_NUMERIC, "C");
	CHECK(l && !strcmp(l, "C"));
	CHECK(setlocale(LC_ALL, "xx_YY.UTF-8") == NULL);
	errno = 0;
	CHECK(setlocale(LC_ALL + 1, "C") == NULL && errno == EINVAL);
	lc = localeconv();
	CHECK(lc != NULL);
	CHECK(lc && !strcmp(lc->decimal_point, "."));
	CHECK(lc && !strcmp(lc->thousands_sep, ""));
	CHECK(lc && lc->frac_digits == 127);
}

/* ---- exit/atexit ---- */
static int order[4];
static int norder;
static void h1(void) { order[norder++] = 1; }
static void h2(void) { order[norder++] = 2; }
static void h3(void)
{
	order[norder++] = 3;
	/* main has returned: nothing else can report a failure, so do it here */
	if (norder != 3 || order[0] != 1 || order[1] != 2 || order[2] != 3) {
		printf("FAIL %s:%d: atexit order %d %d %d\n", __FILE__, __LINE__, order[0], order[1], order[2]);
		fflush(stdout);
		_Exit(1);
	}
}

/* ---- abort() in a child ---- */
static void test_abort_child(const char *self)
{
	char *argv[3];
	int pid, status;
	argv[0] = (char *)self;
	argv[1] = (char *)"--abort-child";
	argv[2] = NULL;
	pid = __spawn(self, argv, environ);
	if (pid < 0) {
		printf("note: cannot spawn \"%s\" (errno %d); abort() child test skipped\n", self, errno);
		return;
	}
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) ? WEXITSTATUS(status) != 0 : WIFSIGNALED(status));

	argv[1] = (char *)"--assert-child";
	pid = __spawn(self, argv, environ);
	CHECK(pid > 0);
	if (pid > 0) {
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFEXITED(status) ? WEXITSTATUS(status) != 0 : WIFSIGNALED(status));
	}

	argv[1] = (char *)"--exit-child";
	pid = __spawn(self, argv, environ);
	CHECK(pid > 0);
	if (pid > 0) {
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 23);
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--abort-child")) {
		signal(SIGABRT, SIG_IGN);  /* abort() must still die */
		abort();
	}
	if (argc > 1 && !strcmp(argv[1], "--assert-child")) {
		volatile int zero = 0;
		/* prints "Assertion failed: ..." to stderr: expected noise */
		assert(zero == 1);
		return 0;  /* assert did not fire: child "succeeds" -> parent fails */
	}
	if (argc > 1 && !strcmp(argv[1], "--exit-child")) {
		atexit(h1);
		exit(23);
	}

	unlink("misc-exit-flush.tmp");
	test_env();
	test_sched();
	test_mlockall();
	test_semaphore();
	test_system();
	test_setjmp();
	test_signal();
	test_libgen();
	test_locale();
	test_abort_child(argv[0]);

	/* exit flushes stdio: write without fclose and let exit() do it */
	{
		FILE *f = fopen("misc-exit-flush.tmp", "w");
		if (f) fputs("flushed by exit\n", f);
		/* left for exit() to flush; removed on the next run */
	}

	CHECK(atexit(h3) == 0);
	CHECK(atexit(h2) == 0);
	CHECK(atexit(h1) == 0);
	/* registered h3, h2, h1: they run in reverse, h1 h2 h3; h3 checks the order */

	if (!fails) printf("misc: all tests passed\n");
	return fails != 0;
}
