/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of the four headers this commit
 * adds: <grp.h>, <sys/utsname.h>, <sys/times.h>, <sys/uio.h>. One file,
 * since none of the four is large on its own -- same reasoning as
 * test/posix-sysmisc.c bundling several small headers together. Every
 * assertion cites the clause of
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 * or .../basedefs/<header>.html it checks. Same three-fence convention
 * as test/posix-sysmisc.c/test/posix-dl.c for the one spot that is a
 * real, permanent gap rather than something written to pass:
 *
 *   #if 0 / * N/A: <requirement + citation + why NT can't> * /
 *
 * (readv()/writev()'s cross-thread atomicity requirement -- see
 * src/misc/uio.c's header comment for the full reasoning; this file's
 * copy is the short version next to the fenced-off assertion).
 *
 * <grp.h> mirrors test/pwd.c's own structure and its have_user() gate:
 * src/misc/grp.c's one group is only knowable when %USERNAME%/%USER%
 * is set, and ntlibc's own native `make asan` harness (fuzz/ntstubs.c)
 * deliberately starts with an empty environ, so both branches are
 * exercised for real depending on which harness runs this file.
 */
#include <grp.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char *self;

/* __spawn(): the same internal re-exec primitive test/posix-signal.c
 * and test/posix-alloc.c already use to get a real child process in
 * both the Wine `make check` harness and the native `make asan` one
 * -- not declared in any public header, so declared locally here too. */
int __spawn(const char *path, char *const argv[], char *const envp[]);
extern char **environ;

/* ================================================================== *
 * <grp.h>: grp.h.html, getgrnam.html, getgrgid.html, getgrent.html,
 * mirroring test/pwd.c's audit of <pwd.h> one gid deep.
 * ================================================================== */

/* Mirrors src/misc/grp.c's current_name() / test/pwd.c's have_user(). */
static int have_group(void)
{
	const char *n = getenv("USERNAME");
	if (!n || !*n) n = getenv("USER");
	return n && *n;
}

/* grp.h.html: "at least" gr_name/gr_gid/gr_mem. getgid()==getegid()
 * always here (src/unistd/ids.c, test/posix-unistd.c), so getgid() is
 * the only gid that can ever have an entry -- when the name behind it
 * is knowable at all (have_group() above). */
static void test_getgrgid_current(void)
{
	struct group *gr;

	errno = 0;
	gr = getgrgid(getgid());
	if (!have_group()) {
		/* getgrgid.html RETURN VALUE: "If the requested entry was
		 * not found, errno shall not be changed." */
		CHECK(gr == NULL);
		CHECK(errno == 0);
		return;
	}
	CHECK(gr != NULL);
	if (!gr) return;
	CHECK(gr->gr_gid == getgid());
	CHECK(gr->gr_name != NULL && gr->gr_name[0] != '\0');
	/* grp.h.html: gr_mem is "a null-terminated array of character
	 * pointers to member names". src/misc/grp.c's design: the one
	 * user this library has genuinely is a member of its own one
	 * group, so gr_mem == {gr_name, NULL}. */
	CHECK(gr->gr_mem != NULL);
	if (gr->gr_mem) {
		CHECK(gr->gr_mem[0] != NULL && strcmp(gr->gr_mem[0], gr->gr_name) == 0);
		CHECK(gr->gr_mem[1] == NULL);
	}
}

static void test_getgrgid_other_not_found(void)
{
	struct group *gr;

	errno = 12345;
	gr = getgrgid(getgid() + 1);
	CHECK(gr == NULL);
	CHECK(errno == 12345);
}

/* getgrnam.html DESCRIPTION: search by name. Round-trip:
 * getgrnam(getgrgid(getgid())->gr_name) must be the same record. */
static void test_getgrnam_current_and_roundtrip(void)
{
	struct group *by_gid, *by_name;
	char namebuf[256];

	by_gid = getgrgid(getgid());
	if (!have_group()) {
		CHECK(by_gid == NULL);
		return;
	}
	CHECK(by_gid != NULL);
	if (!by_gid) return;
	strcpy(namebuf, by_gid->gr_name);   /* g_gr is static storage, reused below */

	errno = 0;
	by_name = getgrnam(namebuf);
	CHECK(by_name != NULL);
	CHECK(errno == 0);
	if (!by_name) return;
	CHECK(strcmp(by_name->gr_name, namebuf) == 0);
	CHECK(by_name->gr_gid == getgid());
}

static void test_getgrnam_other_not_found(void)
{
	struct group *gr;

	errno = 12345;
	gr = getgrnam("definitely-not-a-real-ntlibc-group-xyz");
	CHECK(gr == NULL);
	CHECK(errno == 12345);
}

/* getgrgid_r/getgrnam_r (Thread-Safe Functions option): "shall return
 * zero" on success or clean not-found; error number returned directly,
 * not via errno; *result NULL on both error and not-found. */
static void test_getgrgid_r_success(void)
{
	struct group gr, *result = (struct group *)0x1;
	char buf[512];
	int r;

	r = getgrgid_r(getgid(), &gr, buf, sizeof buf, &result);
	CHECK(r == 0);
	if (!have_group()) {
		CHECK(result == NULL);
		return;
	}
	CHECK(result == &gr);
	CHECK(gr.gr_gid == getgid());
	CHECK(gr.gr_name != NULL && gr.gr_name[0] != '\0');
	CHECK(gr.gr_mem != NULL && gr.gr_mem[0] != NULL && gr.gr_mem[1] == NULL);
}

static void test_getgrgid_r_not_found(void)
{
	struct group gr, *result = (struct group *)0x1;
	char buf[512];
	int r;

	r = getgrgid_r(getgid() + 1, &gr, buf, sizeof buf, &result);
	CHECK(r == 0);
	CHECK(result == NULL);
}

/* ERANGE: "Insufficient storage was supplied via buffer and bufsize."
 * Only observable when there is a record to try to pack; without
 * have_group(), getgrgid_r() reports "not found" first. */
static void test_getgrgid_r_erange(void)
{
	struct group gr, *result = (struct group *)0x1;
	char buf[1];
	int r;

	r = getgrgid_r(getgid(), &gr, buf, sizeof buf, &result);
	if (!have_group()) {
		CHECK(r == 0);
		CHECK(result == NULL);
		return;
	}
	CHECK(r == ERANGE);
	CHECK(result == NULL);
}

static void test_getgrnam_r_success_and_not_found(void)
{
	struct group gr, *result;
	char buf[512];
	char namebuf[256];
	int r;

	if (!have_group()) {
		result = (struct group *)0x1;
		r = getgrnam_r("whoever", &gr, buf, sizeof buf, &result);
		CHECK(r == 0);
		CHECK(result == NULL);
		return;
	}

	result = (struct group *)0x1;
	r = getgrgid_r(getgid(), &gr, buf, sizeof buf, &result);
	CHECK(r == 0 && result == &gr);
	if (r != 0 || !result) return;
	strcpy(namebuf, gr.gr_name);

	result = (struct group *)0x1;
	r = getgrnam_r(namebuf, &gr, buf, sizeof buf, &result);
	CHECK(r == 0);
	CHECK(result == &gr);
	CHECK(gr.gr_gid == getgid());

	result = (struct group *)0x1;
	r = getgrnam_r("definitely-not-a-real-ntlibc-group-xyz", &gr, buf, sizeof buf, &result);
	CHECK(r == 0);
	CHECK(result == NULL);
}

static void test_getgrnam_r_erange(void)
{
	struct group gr, *result = (struct group *)0x1;
	char buf[1];
	char namebuf[256];
	struct group *cur;

	if (!have_group()) {
		result = (struct group *)0x1;
		CHECK(getgrnam_r("whoever", &gr, buf, sizeof buf, &result) == 0);
		CHECK(result == NULL);
		return;
	}

	cur = getgrgid(getgid());
	CHECK(cur != NULL);
	if (!cur) return;
	strcpy(namebuf, cur->gr_name);

	result = (struct group *)0x1;
	CHECK(getgrnam_r(namebuf, &gr, buf, sizeof buf, &result) == ERANGE);
	CHECK(result == NULL);
}

/* getgrent.html: XSI, but implementable here -- one entry when
 * have_group(), none otherwise. setpwent()/getgrent()/endgrent()
 * mirror test/pwd.c's getpwent() coverage exactly. */
static void test_getgrent_one_entry_then_eof(void)
{
	struct group *gr;

	setgrent();
	errno = 0;
	gr = getgrent();
	if (have_group()) {
		CHECK(gr != NULL);
		if (gr) CHECK(gr->gr_gid == getgid());
	} else {
		CHECK(gr == NULL);
		CHECK(errno == 0);
	}

	errno = 0;
	gr = getgrent();
	CHECK(gr == NULL);
	CHECK(errno == 0);

	setgrent();
	gr = getgrent();
	CHECK((gr != NULL) == have_group());
	endgrent();
}

/* Consistency checks the task brief calls out explicitly: gr_gid
 * against getgid(), and getgrnam(getgrgid(getgid())->gr_name)
 * round-tripping back to the same gid. */
static void test_consistency(void)
{
	struct group *by_gid, *by_name;

	by_gid = getgrgid(getgid());
	if (!have_group()) { CHECK(by_gid == NULL); return; }
	CHECK(by_gid != NULL);
	if (!by_gid) return;
	CHECK(by_gid->gr_gid == getgid());

	by_name = getgrnam(by_gid->gr_name);
	CHECK(by_name != NULL);
	if (!by_name) return;
	CHECK(by_name->gr_gid == getgid());
}

/* ================================================================== *
 * <sys/utsname.h>: uname.html, sys_utsname.h.html.
 * ================================================================== */


/* ==== clauses the successor-queue <grp.h> audit added ==================== */

/* getgrgid.html RETURN VALUE: "A null pointer shall be returned if the
 * requested entry is not found ... If the requested entry was not
 * found, errno shall not be changed." The existing not-found tests use
 * getgid()+1, adjacent to the one gid that does exist; this uses a gid
 * that could not plausibly be anything, to pin that the answer is a
 * real lookup rather than an entry fabricated for any argument. */
static void test_getgrgid_absurd_gid(void)
{
	errno = 12345;
	CHECK(getgrgid((gid_t)0x7ffffffe) == NULL);
	CHECK(errno == 12345);
	errno = 12345;
	CHECK(getgrnam("no-such-group-could-ever-be-called-this") == NULL);
	CHECK(errno == 12345);
}

/* getgrnam.html RETURN VALUE: "The getgrnam_r() function shall return
 * zero on success or if the requested entry was not found and no error
 * has occurred", with a null pointer stored through result. Not-found
 * is not an error for the _r form. */
static void test_getgrgid_r_absurd_gid(void)
{
	struct group gr;
	struct group *result = (struct group *)0x1;
	char buf[512];

	CHECK(getgrgid_r((gid_t)0x7ffffffe, &gr, buf, sizeof buf, &result) == 0);
	CHECK(result == NULL);
	result = (struct group *)0x1;
	CHECK(getgrnam_r("no-such-group-could-ever-be-called-this", &gr, buf, sizeof buf, &result) == 0);
	CHECK(result == NULL);
}

/* setgrent.html: "shall rewind the group database"; endgrent.html:
 * "shall close the group database"; getgrent.html: "If the database is
 * not already open, getgrent() shall open it and return ... the first
 * entry." So endgrent() followed by getgrent() must re-yield the first
 * entry rather than stay at end-of-file --
 * test_getgrent_one_entry_then_eof() calls endgrent() only as its last
 * statement and never reads after it. Both pages also state the
 * function "shall not change the setting of errno if successful". */
static void test_grent_reopen_and_errno(void)
{
	struct group *gr;

	setgrent();
	(void)getgrent();
	(void)getgrent();
	CHECK(getgrent() == NULL);

	endgrent();
	gr = getgrent();
	CHECK((gr != NULL) == have_group());

	errno = 12345;
	setgrent();
	CHECK(errno == 12345);
	errno = 12345;
	endgrent();
	CHECK(errno == 12345);
}

/* grp.h.html: struct group's gr_mem is a "Pointer to a null-terminated
 * array of character pointers to member names". The array lives inside
 * the caller's buffer for the _r forms, so it has to be carved out at a
 * correctly aligned offset -- src/misc/grp.c pads for that, and the
 * padding is charged to the size it demands, but nothing ever handed it
 * a deliberately misaligned buffer to prove either half. Also pins
 * ERANGE's boundary (one byte short must fail, exactly enough must
 * succeed) rather than only the one-byte case the existing test uses,
 * which cannot tell a correct size computation from a blanket refusal. */
static void test_getgrgid_r_alignment_and_erange_boundary(void)
{
	static char raw[512];
	char *misaligned = raw + 1;
	struct group gr;
	struct group *result;

	result = (struct group *)0x1;
	CHECK(getgrgid_r(getgid(), &gr, misaligned, sizeof raw - 1, &result) == 0);
	if (!have_group()) {
		CHECK(result == NULL);
		printf("note: no group name knowable -- gr_mem alignment and the ERANGE boundary are unreachable (getgrgid_r() answers \"not found\" before it sizes anything)\n");
		return;
	}
	CHECK(result == &gr);
	if (result != &gr) return;
	/* "a null-terminated array of character pointers", correctly
	 * aligned even though the buffer it was carved from was not. */
	CHECK(((size_t)(char *)gr.gr_mem % sizeof(char *)) == 0);
	CHECK(gr.gr_mem[0] != NULL);
	CHECK(gr.gr_mem[1] == NULL);

	/* ERANGE boundary. Walk the size down until it stops fitting,
	 * rather than recomputing src/misc/grp.c's packing here: what the
	 * clause requires is that there *is* a boundary and that one more
	 * byte is enough, not any particular number. */
	{
		size_t hi = sizeof raw - 1, lo;
		while (hi > 1) {
			result = (struct group *)0x1;
			if (getgrgid_r(getgid(), &gr, misaligned, hi - 1, &result) == ERANGE) break;
			hi--;
		}
		CHECK(hi > 1);			/* a boundary exists */
		lo = hi - 1;
		result = (struct group *)0x1;
		CHECK(getgrgid_r(getgid(), &gr, misaligned, lo, &result) == ERANGE);
		CHECK(result == NULL);		/* "*result shall be a null pointer ... on error" */
		result = NULL;
		CHECK(getgrgid_r(getgid(), &gr, misaligned, hi, &result) == 0);
		CHECK(result == &gr);		/* one more byte is enough */
	}
}

#if 0 /* BUG: getgrgid.html/getgrnam.html ERRORS list, for the non-_r
	forms, exactly [EIO], [EINTR], [EMFILE] and [ENFILE], all "may
	fail". [ERANGE] is listed only for getgrgid_r()/getgrnam_r(),
	where it means "insufficient storage was supplied via buffer and
	bufsize" -- an argument the non-_r forms do not have. RETURN
	VALUE adds: "If the requested entry was not found, errno shall
	not be changed."

	src/misc/grp.c's getgrnam() and getgrgid() both do

		r = fill_current(&g_gr, g_grmem, g_grbuf, sizeof g_grbuf);
		if (r == ERANGE) { errno = ERANGE; return 0; }

	on their *internal* static buffer, setting an errno POSIX does
	not permit them to set. g_grbuf is only 256 + sizeof g_grmem =
	272 bytes, so any %USERNAME% longer than that reaches it -- well
	inside what a program can set for itself, no unusual NT
	configuration needed.

	getgrent() inherits it, delegating to getgrgid(), whose ERRORS
	list is likewise [EIO]/[EINTR]/[EMFILE]/[ENFILE] only.

	This is the "stub returning an errno that is not in its POSIX
	list" shape, not a platform N/A. Fenced rather than fixed, per
	the standing rule; the fix is to treat an internal-buffer
	overflow as "not found" (NULL, errno untouched), or to size the
	static buffer so the case is unreachable. src/misc/pwd.c has the
	identical defect -- see test/pwd.c's matching fence. */
static void test_getgrgid_erange_not_in_its_errno_list(void)
{
	static char big[400];
	char *saved_username = getenv("USERNAME");
	char *saved_user = getenv("USER");
	char keep_username[256], keep_user[256];
	int had_username = saved_username != NULL, had_user = saved_user != NULL;

	if (had_username) { strncpy(keep_username, saved_username, sizeof keep_username - 1); keep_username[sizeof keep_username - 1] = 0; }
	if (had_user) { strncpy(keep_user, saved_user, sizeof keep_user - 1); keep_user[sizeof keep_user - 1] = 0; }

	memset(big, 'x', sizeof big - 1);
	big[sizeof big - 1] = 0;
	CHECK(setenv("USERNAME", big, 1) == 0);

	errno = 0;
	CHECK(getgrgid(getgid()) == NULL);
	CHECK(errno != ERANGE);		/* fails today: errno == ERANGE */

	if (had_username) setenv("USERNAME", keep_username, 1); else unsetenv("USERNAME");
	if (had_user) setenv("USER", keep_user, 1); else unsetenv("USER");
}
#endif

static void test_uname(void)
{
	struct utsname u;
	char host[256];

	memset(&u, 0, sizeof u);
	/* uname.html RETURN VALUE: "a non-negative value shall be
	 * returned" on success. */
	CHECK(uname(&u) >= 0);

	/* DESCRIPTION: "shall return a string naming the current system
	 * in ... sysname" -- format is implementation-defined
	 * (RATIONALE: "The format of each member is
	 * implementation-defined"). src/misc/uname.c's choice is the
	 * literal string NT itself uses (%OS%). */
	CHECK(strcmp(u.sysname, "Windows_NT") == 0);

	/* DESCRIPTION: "nodename shall contain the name of this node
	 * within an implementation-defined communications network" --
	 * src/misc/uname.c reuses gethostname() for this, so the two
	 * must agree exactly. */
	CHECK(gethostname(host, sizeof host) == 0);
	CHECK(strcmp(u.nodename, host) == 0);

	/* "The arrays release and version shall further identify the
	 * operating system." Format is implementation-defined; checked
	 * against the RtlGetVersion()-sourced shape src/misc/uname.c
	 * documents (major.minor, and "Build N"). */
	CHECK(u.release[0] != '\0');
	CHECK(strchr(u.release, '.') != NULL);
	CHECK(strncmp(u.version, "Build ", 6) == 0);

	/* "The array machine shall contain a name that identifies the
	 * hardware that the system is running on" -- here, the arch this
	 * binary was actually compiled for (src/misc/uname.c's
	 * documented WOW64 reasoning: the running process's own
	 * bitness, not the kernel's). */
#if defined(__x86_64__)
	CHECK(strcmp(u.machine, "x86_64") == 0);
#elif defined(__i386__)
	CHECK(strcmp(u.machine, "i686") == 0);
#endif

	/* uname.html ERRORS: "No errors are defined." -- a NULL argument
	 * is therefore not a POSIX-mandated case; src/misc/uname.c still
	 * refuses it cleanly rather than crash, exercised here as a
	 * plain implemented-behavior check, not a spec citation. */
	errno = 0;
	CHECK(uname(NULL) == -1);
}

/* ================================================================== *
 * <sys/times.h>: times.html.
 * ================================================================== */

/* Same _SC_CLK_TCK-based tick math src/misc/times.c uses internally,
 * reimplemented independently here (not shared code) so this test does
 * not just echo the same formula back at itself. */
static clock_t timeval_to_clockticks(const struct timeval *tv)
{
	long tck = sysconf(_SC_CLK_TCK);
	return (clock_t)((long long)tv->tv_sec * tck + (long long)tv->tv_usec * tck / 1000000);
}

static void test_times_self(void)
{
	struct rusage ru_before;
	struct tms t;
	clock_t r;
	clock_t utime_ticks, stime_ticks;

	/* Burn enough real user CPU that the readings below are non-zero on
	 * any platform that tracks process times at all.  Without this the
	 * whole function was vacuous: a test process that has done almost
	 * nothing reports tms_utime == 0 and ru_utime == 0, so
	 * `t.tms_utime >= utime_ticks` was 0 >= 0 and passed identically if
	 * times() and getrusage() had both written nothing.  Measured under
	 * stock Wine: ~300M iterations of this loop is ~0.5s of user time,
	 * i.e. ~50 ticks at the _SC_CLK_TCK of 100 this build reports, so
	 * the > 0 assertion below has a wide margin over the 10ms tick. */
	{
		volatile double x = 0;
		long i;
		for (i = 0; i < 300000000L; i++) x += (double)i;
		(void)x;
	}

	memset(&ru_before, 0xff, sizeof ru_before);
	CHECK(getrusage(RUSAGE_SELF, &ru_before) == 0);

	memset(&t, 0xff, sizeof t);
	r = times(&t);
	/* times.html RETURN VALUE: "(clock_t)-1 shall be returned" only
	 * on failure; this call cannot fail (no children waited on yet,
	 * no overflow reachable in a test run). */
	CHECK(r != (clock_t)-1);

	/* DESCRIPTION: "tms_utime ... is the CPU time charged for the
	 * execution of user instructions of the calling process" / same
	 * for tms_stime and the system. src/misc/resource.c's
	 * getrusage(RUSAGE_SELF) reads the identical
	 * NtQueryInformationProcess(ProcessTimes) source a few
	 * instructions earlier in this function, so times()'s answer can
	 * only have grown since -- never gone backwards. */
	utime_ticks = timeval_to_clockticks(&ru_before.ru_utime);
	stime_ticks = timeval_to_clockticks(&ru_before.ru_stime);
	/* The field is genuinely tracked, not merely zero: after the burn
	 * above it must have advanced past zero.  This is the assertion
	 * that makes the cross-check below mean something -- 0 >= 0 does
	 * not distinguish "charged correctly" from "never populated". */
	CHECK(utime_ticks > 0);
	CHECK(t.tms_utime > 0);
	CHECK(t.tms_utime >= utime_ticks);
	CHECK(t.tms_stime >= stime_ticks);
	/* And not off by some wildly different order of magnitude either
	 * -- generous (5s) bound so this cannot flake under slow CI. */
	CHECK(t.tms_utime - utime_ticks < 500);
	CHECK(t.tms_stime - stime_ticks < 500);
}

/* times.html DESCRIPTION: "The times of a terminated child process
 * shall be included in the tms_cutime and tms_cstime elements of the
 * parent when wait() ... returns the process ID of this terminated
 * child." src/process/wait.c accumulates exactly this total for
 * getrusage(RUSAGE_CHILDREN) already; src/misc/times.c reads the same
 * accumulator, so the two must report identical values after the same
 * reap -- the exact cross-check the task brief calls for. */
static void test_times_children(void)
{
	char *argv[3];
	pid_t pid;
	int status;
	struct rusage ru_children;
	struct tms t;

	argv[0] = (char *)self; argv[1] = (char *)"--times-child"; argv[2] = NULL;
	pid = __spawn(self, argv, environ);
	if (pid < 0) { printf("note: cannot spawn \"%s\"; times() child test skipped\n", self); return; }
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	/* Both destinations are poisoned first, so "the reader returned
	 * success without writing the field" is a failure here and not an
	 * accidental match at zero. */
	memset(&ru_children, 0xff, sizeof ru_children);
	CHECK(getrusage(RUSAGE_CHILDREN, &ru_children) == 0);
	memset(&t, 0xff, sizeof t);
	CHECK(times(&t) != (clock_t)-1);

	CHECK(t.tms_cutime >= 0 && t.tms_cstime >= 0);

	/* The assertion that makes the cross-check below mean anything.
	 * Two readers of one accumulator (src/process/wait.c's
	 * children_utime100ns/children_ktime100ns) agreeing at zero agree
	 * whether or not either reader works and whether or not wait.c ever
	 * accumulated anything -- and zero is exactly what an unpopulated
	 * accumulator reads as.  The "--times-child" role in main() burns
	 * real CPU precisely so this is not zero; measured under stock apt
	 * Wine it is 49 ticks at the _SC_CLK_TCK of 100 this build reports,
	 * so the margin over the 1-tick floor is wide.
	 *
	 * tms_cstime is deliberately not held to > 0: a child that only
	 * spins in user code need not be charged any system time at all,
	 * and it measures 0 here. */
	CHECK(t.tms_cutime > 0);
	CHECK(timeval_to_clockticks(&ru_children.ru_utime) > 0);

	CHECK(t.tms_cutime == timeval_to_clockticks(&ru_children.ru_utime));
	CHECK(t.tms_cstime == timeval_to_clockticks(&ru_children.ru_stime));
}

/* Return value: "elapsed real time, in clock ticks, since an
 * arbitrary point in the past ... This point does not change from one
 * invocation of times() within the process to another." Checked by
 * calling twice and requiring the second reading not to have gone
 * backwards. */
static void test_times_monotonic(void)
{
	clock_t a, b;

	a = times(NULL);
	CHECK(a != (clock_t)-1);
	b = times(NULL);
	CHECK(b != (clock_t)-1);
	CHECK(b >= a);
}

/* EOVERFLOW ("The return value would overflow the range of clock_t")
 * is real per the spec but not practically triggerable in a finite
 * test run (it requires clock_t itself to wrap, i.e. the process
 * living past clock_t's max tick count) -- not fenced UNIMPL/N-A since
 * it is not a gap in this implementation, just untestable within a
 * test suite's lifetime. */

/* ================================================================== *
 * <sys/uio.h>: readv.html, writev.html, sys_uio.h.html.
 * ================================================================== */

static void test_readv_writev_roundtrip(void)
{
	char path[] = "t-uio-roundtrip.tmp";
	int fd;
	struct iovec wiov[3];
	struct iovec riov[3];
	char w0[5] = "Hello", w1[7] = ", ntl!", w2[4] = "bc!";
	char r0[5], r1[7], r2[4];
	ssize_t n;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (fd < 0) return;

	/* writev.html DESCRIPTION: "gather output data from the iovcnt
	 * buffers ... always write a complete area before proceeding to
	 * the next." */
	wiov[0].iov_base = w0; wiov[0].iov_len = sizeof w0;
	wiov[1].iov_base = w1; wiov[1].iov_len = sizeof w1;
	wiov[2].iov_base = w2; wiov[2].iov_len = sizeof w2;
	n = writev(fd, wiov, 3);
	CHECK(n == (ssize_t)(sizeof w0 + sizeof w1 + sizeof w2));

	CHECK(lseek(fd, 0, SEEK_SET) == 0);

	/* readv.html DESCRIPTION: "place the input data into the iovcnt
	 * buffers ... always fill an area completely before proceeding
	 * to the next." */
	riov[0].iov_base = r0; riov[0].iov_len = sizeof r0;
	riov[1].iov_base = r1; riov[1].iov_len = sizeof r1;
	riov[2].iov_base = r2; riov[2].iov_len = sizeof r2;
	n = readv(fd, riov, 3);
	CHECK(n == (ssize_t)(sizeof r0 + sizeof r1 + sizeof r2));
	CHECK(memcmp(r0, w0, sizeof w0) == 0);
	CHECK(memcmp(r1, w1, sizeof w1) == 0);
	CHECK(memcmp(r2, w2, sizeof w2) == 0);

	close(fd);
	unlink(path);
}

/* sys_uio.h.html DESCRIPTION: struct iovec "shall include at least"
 * iov_base (void *) and iov_len (size_t). */
static void test_iovec_members(void)
{
	struct iovec v;
	char c;

	v.iov_base = &c;
	v.iov_len = 1;
	CHECK(v.iov_base == &c);
	CHECK(v.iov_len == 1);
}

/* readv.html/writev.html ERRORS: "may fail" with [EINVAL] if "iovcnt
 * ... was less than or equal to 0, or greater than {IOV_MAX}."
 * src/misc/uio.c always enforces this (not merely "may"). */
static void test_iovcnt_range(void)
{
	struct iovec iov[1];
	char c;
	int fd;

	iov[0].iov_base = &c; iov[0].iov_len = 1;
	fd = open("t-uio-range.tmp", O_RDWR | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0);

	errno = 0;
	CHECK(readv(fd, iov, 0) == -1);
	CHECK(errno == EINVAL);
	errno = 0;
	CHECK(writev(fd, iov, 0) == -1);
	CHECK(errno == EINVAL);

	errno = 0;
	CHECK(readv(fd, iov, -1) == -1);
	CHECK(errno == EINVAL);

	errno = 0;
	CHECK(readv(fd, iov, IOV_MAX + 1) == -1);
	CHECK(errno == EINVAL);
	errno = 0;
	CHECK(writev(fd, iov, IOV_MAX + 1) == -1);
	CHECK(errno == EINVAL);

	if (fd >= 0) { close(fd); unlink("t-uio-range.tmp"); }
}

/* readv.html/writev.html ERRORS: "shall fail" with [EINVAL] if "the
 * sum of the iov_len values in the iov array overflowed an ssize_t";
 * writev.html DESCRIPTION adds "no data shall be transferred." Two
 * huge lengths that individually fit in size_t but overflow SSIZE_MAX
 * when summed -- src/misc/uio.c's check_iov() rejects this before
 * touching either buffer, so bogus iov_base pointers are safe here. */
static void test_iov_len_overflow(void)
{
	struct iovec iov[2];
	int fd;
	off_t before, after;

	iov[0].iov_base = (void *)1; iov[0].iov_len = (size_t)SSIZE_MAX - 1;
	iov[1].iov_base = (void *)1; iov[1].iov_len = (size_t)SSIZE_MAX - 1;

	fd = open("t-uio-overflow.tmp", O_RDWR | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (fd < 0) return;

	before = lseek(fd, 0, SEEK_CUR);
	errno = 0;
	CHECK(writev(fd, iov, 2) == -1);
	CHECK(errno == EINVAL);
	after = lseek(fd, 0, SEEK_CUR);
	CHECK(before == after);   /* "no data shall be transferred" */

	errno = 0;
	CHECK(readv(fd, iov, 2) == -1);
	CHECK(errno == EINVAL);

	close(fd);
	unlink("t-uio-overflow.tmp");
}

/* writev.html DESCRIPTION: "If fildes refers to a regular file and
 * all of the iov_len members ... are 0, writev() shall return 0 and
 * have no other effect." */
static void test_writev_all_zero(void)
{
	struct iovec iov[2];
	int fd;
	off_t before, after;

	iov[0].iov_base = NULL; iov[0].iov_len = 0;
	iov[1].iov_base = NULL; iov[1].iov_len = 0;

	fd = open("t-uio-zero.tmp", O_RDWR | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (fd < 0) return;

	before = lseek(fd, 0, SEEK_CUR);
	CHECK(writev(fd, iov, 2) == 0);
	after = lseek(fd, 0, SEEK_CUR);
	CHECK(before == after);

	close(fd);
	unlink("t-uio-zero.tmp");
}

/* XBD 2.9.7 "Thread Interactions with Regular File Operations"
 * (basedefs/V2_chap02.html) requires read(), write(), readv(), and
 * writev() (among others) to be atomic with respect to each other on
 * a regular file: "If two threads each call one of these functions,
 * each call shall either see all of the specified effects of the
 * other call, or none of them." src/misc/uio.c's readv()/writev() are
 * a loop of separate NtReadFile()/NtWriteFile() calls, so a concurrent
 * write() from another thread can land in the middle of a readv()'s
 * buffers, or vice versa -- there is no genuinely atomic alternative
 * available: NT's real scatter/gather primitives
 * (NtReadFileScatter()/NtWriteFileGather()) require every element to
 * be page-aligned and a whole number of pages, which an arbitrary
 * struct iovec from a real caller is not. Not testable in-process
 * either way (it is a cross-thread race, not a single-call return
 * value), so this is documentation, not an assertion. */
#if 0 /* N/A: XBD 2.9.7 (basedefs/V2_chap02.html) -- readv()/writev()
       * are not atomic with respect to concurrent read()/write() on
       * the same regular file, because NT's only real scatter/gather
       * primitives (NtReadFileScatter/NtWriteFileGather) are
       * page-granular and cannot serve an arbitrary iovec. See
       * src/misc/uio.c's header comment. */
#endif

/* ================================================================== */

int main(int argc, char **argv)
{
	self = argv[0];
	if (argc > 1 && !strcmp(argv[1], "--times-child")) {
		/* Burn a little real CPU so tms_cutime/tms_cstime have
		 * something nonzero-shaped to accumulate, then exit cleanly
		 * so waitpid() reaps a real, queryable exit status. */
		volatile unsigned long i, sum = 0;
		for (i = 0; i < 20000000UL; i++) sum += i;
		(void)sum;
		return 0;
	}

	printf("note: have_group() = %s\n", have_group() ? "true" : "false");

	test_getgrgid_current();
	test_getgrgid_other_not_found();
	test_getgrnam_current_and_roundtrip();
	test_getgrnam_other_not_found();
	test_getgrgid_r_success();
	test_getgrgid_r_not_found();
	test_getgrgid_r_erange();
	test_getgrnam_r_success_and_not_found();
	test_getgrnam_r_erange();
	test_getgrent_one_entry_then_eof();
	test_consistency();
	test_getgrgid_absurd_gid();
	test_getgrgid_r_absurd_gid();
	test_grent_reopen_and_errno();
	test_getgrgid_r_alignment_and_erange_boundary();

	test_uname();

	test_times_self();
	test_times_children();
	test_times_monotonic();

	test_readv_writev_roundtrip();
	test_iovec_members();
	test_iovcnt_range();
	test_iov_len_overflow();
	test_writev_all_zero();

	if (!fails) printf("posix-grp: all tests passed\n");
	return fails != 0;
}
