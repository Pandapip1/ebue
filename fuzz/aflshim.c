/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Built only for ENGINE=afl (see fuzz/Makefile).  AFL++'s runtime object,
 * afl-compiler-rt.o -- which afl-clang-fast links into every instrumented
 * binary automatically -- calls open()/read()/write()/close()/shmat()/
 * shmdt()/getenv()/fork()/waitpid()/sigaction()/signal() to talk to
 * afl-fuzz and to run its persistent-mode fast path: it maps the coverage
 * bitmap afl-fuzz allocated (shmat, keyed by the __AFL_SHM_ID environment
 * variable), reads/writes the FORKSRV_FD control pipes afl-fuzz set up
 * before execve(), and fork()s a fresh child in this very process for
 * every test case rather than paying execve() each time -- the whole
 * point of a forkserver.
 *
 * Every one of those names is also a strong symbol in ntlibc itself, and
 * in this link ntlibc's wins (ordinary "the executable's own definition
 * beats a shared library's" ELF override -- the identical mechanism, and
 * exactly the class of bug, that STATRENAME in fuzz/Makefile exists to
 * fix for stat()).  Unpatched, afl-compiler-rt.o's calls reach ntlibc's
 * versions instead of the host's, and each failure mode had to be found
 * by measurement, not anticipated up front:
 *
 *   - read()/write()/close() look the fd up in ntlibc's own fd table,
 *     which was never told about a host-inherited fd like FORKSRV_FD,
 *     and fail as though it does not exist; getenv() searches ntlibc's
 *     `environ`, which fuzz/ntstubs.c's __ntshim_init() deliberately
 *     empties for every native test; shmat()/shmdt() would attach
 *     ntlibc's simulated address space's idea of a segment, not a real
 *     SysV one.  Measured directly: afl-showmap against an unpatched
 *     binary shows the harness read its own /proc/self/cmdline and
 *     /proc/self/environ, then exited 0 without ever touching
 *     FORKSRV_FD -- afl-compiler-rt.o's own "am I running under
 *     afl-fuzz" checks came back negative, silently, because the ones
 *     they depend on (fd and env lookups) were answered by ntlibc rather
 *     than failing loudly.
 *
 *   - fork(), once the above were fixed and the faux forkserver started
 *     actually calling it, turned out to be ntlibc's -- RtlCloneUserProcess,
 *     which clones an entire simulated NT process (see
 *     [[ntlibc-fork-userspace-state]]) -- and paying that cost on every
 *     single test case is not merely wasteful, it is slow enough to look
 *     like a hang: a trivial input to fuzz_string.c took over a second
 *     and tripped afl-fuzz's dry-run timeout outright.
 *
 * A third failure, found the same way once the first two were fixed and
 * the persistent-mode fast path actually started running: the
 * long-lived forkserver child never terminates when afl-fuzz sends it
 * SIGTERM at the end of a run.  AFL++'s runtime installs its own
 * SIGTERM/SIGCHLD handlers via sigaction(), which -- unpatched -- is
 * ntlibc's, layered on this codebase's own simulated-NT signal delivery
 * (see [[wine-clone-process-hazards]] and the cross-process signal work
 * in HEAD's history).  Measured with strace: the handler ntlibc actually
 * runs on SIGTERM does a couple of close()s and returns via
 * rt_sigreturn straight back into the blocking read(FORKSRV_FD) --
 * never exiting -- so afl-fuzz's own dry run, and any real campaign's
 * shutdown, hangs until something outside the process kills it.
 *
 * __real_sigaction() below is a deliberate no-op, not a raw-syscall
 * reimplementation: it reports success without installing anything, so
 * the signals AFL++ wanted to catch (SIGTERM chief among them) keep
 * their kernel default disposition -- which for SIGTERM already IS
 * "terminate the process", exactly the behaviour that was missing. Two
 * things this gives up, both already covered elsewhere: AFL++'s own
 * crash diagnostics (a message before dying) are lost, but the crash
 * itself is unaffected and afl-fuzz already learns of it from the
 * child's wait4() status the same way it always did; and a custom
 * SIGALRM-based timeout inside this process never fires, but every
 * caller here (tools/afl-fuzz.sh, and the outer harness scripts before
 * it) already wraps its own `timeout` around the whole run for exactly
 * this reason.  Getting a raw rt_sigaction() right by hand -- kernel
 * sigset_t is 8 bytes, not glibc's 128, and a real handler needs a
 * correct SA_RESTORER trampoline to return through -- would trade a
 * measured, well-understood limitation for an unmeasured ABI risk to
 * fix a case nothing here depends on.  signal() gets the same no-op
 * treatment, for the same reason and by the same reasoning: on this
 * codebase's own terms (see [[test-cant-discriminate-vs-not-worth-adding]]),
 * a correctly-behaving no-op beats a hand-rolled kernel ABI shim with no
 * way to be sure it is right.
 *
 * A fourth failure, found only once the first three no longer masked it:
 * the persistent forkserver would fork() correctly (raw syscall, by
 * then) and report success to afl-fuzz over FORKSRV_FD -- and then
 * immediately call waitpid() on the child it had just created, which
 * -- unpatched -- is ntlibc's, consulting ntlibc's own process table.
 * That table knows nothing about a child __real_fork() created with a
 * bare syscall, so it answers ECHILD ("No child process"), and
 * afl-compiler-rt.o's own error path
 * (instrumentation/afl-compiler-rt.o.c:1307, `Error(waitpid): No child
 * process`) exit()s the forkserver on the spot -- which afl-fuzz sees,
 * from its side of FORKSRV_FD, as the connection simply going away:
 * "Unable to communicate with fork server", indistinguishable at that
 * remove from a crash, a hang, or the forkserver never having started
 * at all.  Measured with strace, not guessed from the message: the
 * fork() and the first write(199, ...) both succeed, and the very next
 * line is waitpid()'s ECHILD followed by the error print and exit_group.
 *
 * A fifth and sixth failure share one cause, in two different objects,
 * and cost the most time to find because the symptom -- afl-fuzz's dry
 * run timing out rather than erroring -- looked like a real performance
 * problem instead of a wiring one.  Once the fork()/waitpid() fix above
 * let the persistent loop actually start, the forked child that runs
 * each test case did nothing at all (not even a syscall) until afl-fuzz's
 * own watchdog SIGKILLed it a second later.  The cause: AFL++'s
 * shared-memory *testcase* transfer (as opposed to the coverage bitmap,
 * which already worked) is opt-in, gated by
 * `__afl_sharedmem_fuzzing`, a flag three different places check and
 * can clear, and every one of those places calls fcntl(FORKSRV_FD,
 * F_GETFD) as part of deciding "am I really running under afl-fuzz":
 *
 *   - fuzz/Makefile's own AFL_RTDIR loop already redirected
 *     afl-compiler-rt.o's getenv()/fork()/sigaction()/etc., but not its
 *     fcntl() -- so its own is-this-afl-fuzz check
 *     (instrumentation/afl-compiler-rt.o.c, the constructor that also
 *     handles the coverage map) saw ntlibc's fcntl() answer -1 for a
 *     host-inherited fd it does not recognise, concluded "not really
 *     under afl-fuzz", and cleared the flag before main() ever ran.
 *   - aflpp_driver.o (libAFLDriver.a) does the identical check a second
 *     time, in its own LLVMFuzzerRunDriver(), and needed the identical
 *     fcntl() redirect -- plus getenv(), whose absence made the check's
 *     `||` chain short-circuit before fcntl() was even reached, so
 *     fixing fcntl() alone here changed nothing until getenv() was
 *     fixed too.  This object ships in its own archive, not under
 *     $(AFL_SYSDIR), so it needed its own copy-and-patch step
 *     (AFL_DRIVER, extracted and objcopied from AFL_DRIVER_SYS) rather
 *     than falling out of the loop that already handles
 *     afl-compiler-rt.o.
 *
 * With the flag correctly staying set in both places, afl-compiler-rt.o's
 * own __afl_map_shm_fuzz() attaches the testcase shared-memory segment
 * (a second, smaller shmat -- confirmed by strace, distinct from the
 * coverage bitmap's) and the persistent loop's forked child has real
 * input to read instead of spinning on memory nothing ever populated.
 *
 * The fix mirrors STATRENAME throughout: local, objcopied copies of the
 * two objects that need it, redirecting exactly the undefined
 * references below to the __real_* names in this file, so only those
 * two objects -- never the harness or the library under test -- reach
 * the host's real kernel and real environment (or, for sigaction/
 * signal, a real no-op).  pipe is left alone: the forkserver evidently
 * reaches the point above using ntlibc's version of it without issue.
 * Patch what measurement shows is actually broken, not everything that
 * could plausibly be -- which is also this comment's own history in
 * miniature: kill() was reasoned to be one of the safe, untouched ones
 * right up until the SIGCONT hang above showed it was not.
 *
 * Raw syscall()s throughout, for the same reason fuzz/ntstubs.c already
 * uses them in several places (its xstatus_init(), for one): a plain
 * call to read()/write()/... from this file would hit the very same
 * ntlibc symbol this file exists to route around.  syscall() itself is
 * not one of the redirected names, so it still reaches glibc's, which is
 * the thin, ABI-stable wrapper this file wants.
 *
 * __real_getenv() reads /proc/self/environ instead of taking a pointer
 * to the process's real envp from fuzz/ntstubs.c, which is the more
 * obvious-looking design and was tried first: ntstubs.c's __ntshim_init()
 * runs as a constructor at priority 200, but AFL++'s own coverage-bitmap
 * constructor (in the object being patched here) needs __AFL_SHM_ID
 * before *that* -- constructor priorities 0-100 are reserved to the
 * implementation, AFL++'s runtime plausibly claims one of those, and no
 * priority a user constructor is allowed to request (101 and up) can run
 * before it.  Measured, not just reasoned about: with ntstubs.c handing
 * this file a saved envp pointer instead, afl-showmap still reported "No
 * instrumentation detected" and the coverage hash changed between runs
 * (ASLR noise over an unwritten map) -- i.e. __afl_area_ptr was never
 * attached to afl-fuzz's segment, because by the time our constructor
 * had run, AFL++'s had already asked ntlibc's (still-empty) `environ`
 * and given up.  /proc/self/environ has no such ordering dependency: the
 * kernel populates it from the process's real, execve-time envp before
 * *any* constructor runs, so it answers correctly no matter which one
 * of this file's own callers gets there first.
 */
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int __real_open(const char *path, int flags, ...);
long __real_read(int fd, void *buf, unsigned long n);
int __real_close(int fd);

#define ENVBUF_SIZE 65536
#define ENVPTR_MAX  1024
static char envbuf[ENVBUF_SIZE];
static char *envptr[ENVPTR_MAX];
static int env_loaded;

static void load_real_environ(void)
{
	int fd, n, total = 0, i, count = 0;
	char *p;

	if (env_loaded) return;
	env_loaded = 1;

	fd = __real_open("/proc/self/environ", O_RDONLY);
	if (fd < 0) return;
	while (total < (int)sizeof(envbuf) - 1) {
		n = (int)__real_read(fd, envbuf + total, sizeof(envbuf) - 1 - total);
		if (n <= 0) break;
		total += n;
	}
	__real_close(fd);
	envbuf[total] = 0;

	for (p = envbuf, i = 0; p < envbuf + total && count < ENVPTR_MAX - 1; p += i + 1) {
		i = (int)strlen(p);
		envptr[count++] = p;
	}
	envptr[count] = 0;
}

long __real_read(int fd, void *buf, unsigned long n)
{
	return syscall(SYS_read, fd, buf, n);
}

long __real_write(int fd, const void *buf, unsigned long n)
{
	return syscall(SYS_write, fd, buf, n);
}

int __real_close(int fd)
{
	return (int)syscall(SYS_close, fd);
}

/* The classic fork(2) syscall, not glibc's wrapper (which does its own
 * pthread_atfork/tid bookkeeping this file has no business touching) and
 * not ntlibc's (see the file comment above for why that one is wrong
 * here specifically, not just unneeded). */
int __real_fork(void)
{
	return (int)syscall(SYS_fork);
}

/* wait4(2) directly: a real child of a real fork() needs the kernel's
 * own accounting, not ntlibc's process table, which has no entry for a
 * child __real_fork() created -- see the file comment's fourth failure
 * for what asking ntlibc's waitpid() about it produces instead
 * (ECHILD, immediately, every time). */
#define SYS_wait4 61
int __real_waitpid(int pid, int *status, int options)
{
	return (int)syscall(SYS_wait4, pid, status, options, 0);
}

/* fcntl(2), for aflpp_driver.o (libAFLDriver.a) -- see this file's
 * banner for the fifth failure this one is.  Only ever called here with
 * F_GETFD/F_SETFD/F_GETFL/F_SETFL, none of which read the third argument
 * as anything but an integer, so a plain va_arg(long) covers every
 * caller without needing to know which command was passed -- the same
 * simplification musl's own fcntl() makes internally. */
#define SYS_fcntl 72
int __real_fcntl(int fd, int cmd, ...)
{
	long arg;
	va_list ap;
	va_start(ap, cmd);
	arg = va_arg(ap, long);
	va_end(ap);
	return (int)syscall(SYS_fcntl, fd, cmd, arg);
}

/* raise(SIGSTOP), from __afl_persistent_loop() -- see this file's
 * banner for the sixth failure, the one that took longest to find
 * because the symptom (a silent, syscall-free hang) looked nothing like
 * its cause.  AFL++'s persistent loop synchronises with the forkserver
 * parent by stopping itself: the parent's wait4(..., WUNTRACED) detects
 * the real kernel STOPPED state, refills the shared testcase buffer in
 * place (the same memory, inherited by fork()), and SIGCONTs the child.
 * ntlibc's raise() -- layered on this codebase's own simulated signal
 * delivery, the same machinery the sigaction()/signal() no-ops above
 * exist to route around -- does not put the process into that real
 * kernel state, so the parent's wait4() never returns and the child
 * never resumes: exactly the "closes 198/199, then nothing, for a full
 * second" hang strace showed, with no syscall to explain it because
 * ntlibc's raise() never reached one. raise(sig) is POSIX sugar for
 * kill(getpid(), sig); this spells that out directly with two raw
 * syscalls rather than also redirecting kill() and getpid() themselves,
 * since nothing else here calls either of those under names this file
 * would otherwise have to chase down. */
#define SYS_getpid 39
#define SYS_kill   62
int __real_raise(int sig)
{
	return (int)syscall(SYS_kill, syscall(SYS_getpid), sig);
}

/* kill(2), for the other half of the same mechanism: after detecting a
 * persistent-mode child's real SIGSTOP (via __real_waitpid's WUNTRACED,
 * itself only meaningful because raise() above delivers a real one),
 * afl-compiler-rt.o's forkserver loop resumes it for the next iteration
 * with kill(child_pid, SIGCONT) rather than forking again -- and that
 * call needs the same redirection raise() did, for the same reason:
 * ntlibc's kill() does not deliver a real SIGCONT to a real stopped
 * process, so the child never wakes and the loop hangs on its next
 * wait4() until afl-fuzz's own watchdog SIGKILLs everything a second
 * later.  Measured with strace: the first SIGSTOP and its WIFSTOPPED
 * detection both work (raise() already fixed), and the very next thing
 * in the trace is the parent's second wait4() never returning -- no
 * SIGCONT delivered in between, anywhere. */
int __real_kill(int pid, int sig)
{
	return (int)syscall(SYS_kill, pid, sig);
}

/* afl-compiler-rt.o only ever opens existing files (its own coverage
 * bitmap's control paths, when present), never O_CREAT, but open() is
 * variadic in the real prototype and this has to link against calls
 * built expecting that ABI. */
int __real_open(const char *path, int flags, ...)
{
	unsigned mode = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, unsigned);
		va_end(ap);
	}
	return (int)syscall(SYS_open, path, flags, mode);
}

void *__real_shmat(int shmid, const void *shmaddr, int shmflg)
{
	return (void *)syscall(SYS_shmat, shmid, shmaddr, shmflg);
}

int __real_shmdt(const void *shmaddr)
{
	return (int)syscall(SYS_shmdt, shmaddr);
}

char *__real_getenv(const char *name)
{
	size_t len;
	int i;
	if (!name) return 0;
	load_real_environ();
	len = strlen(name);
	for (i = 0; envptr[i]; i++)
		if (!strncmp(envptr[i], name, len) && envptr[i][len] == '=')
			return envptr[i] + len + 1;
	return 0;
}

/* Deliberate no-ops -- see the file comment's third section for why a
 * raw-syscall reimplementation is not worth the ABI risk here.  Report
 * success, install nothing, so SIGTERM (and anything else AFL++ wanted
 * to catch) keeps the kernel's own default disposition. */
int __real_sigaction(int signum, const void *act, void *oldact)
{
	(void)signum; (void)act; (void)oldact;
	return 0;
}

void *__real_signal(int signum, void *handler)
{
	(void)signum; (void)handler;
	return 0;
}
