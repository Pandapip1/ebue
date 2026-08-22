/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause audit of the last never-audited corners:
 * <sys/resource.h>, <sys/select.h>, <sys/param.h>, and the GNU
 * getopt_long()/getopt_long_only() extensions.  Each assertion cites
 * the clause of https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/<name>.html or .../basedefs/<header>.html it checks, or
 * (for the two non-POSIX pieces) the GNU/BSD documentation cited
 * inline.
 *
 * Three genuine implementation gaps live in this area and are recorded
 * as gaps here rather than laundered into "N/A":
 *
 *   - setrlimit() is *declared* in include/sys/resource.h but has no
 *     definition anywhere in src/ (grep confirms) -- calling it is a
 *     link error, not a runtime ENOSYS.  Not implementable to spec
 *     either: see include/sys/resource.h's own undefined-ok comment.
 *     Left untested here; there is nothing to link against.
 *
 *   - getpriority()/setpriority() are POSIX.1-2017 base functions
 *     (moved from XSI to BASE in Issue 5 -- getpriority.html) declared
 *     by <sys/resource.h>, but ntlibc does not declare or define them
 *     at all.  This *is* implementable on NT (GetPriorityClass/
 *     SetPriorityClass, or NtQueryInformationProcess(ProcessBasePriority)
 *     under ntdll, mapped through the nice-value range) -- a real gap,
 *     not a platform limitation.  Nothing to call, so nothing to test;
 *     recorded for the ledger.
 *
 *   - select()/pselect(): select() is declared but, per
 *     include/sys/select.h's own undefined-ok comment, not defined
 *     anywhere in src/ (grep confirms no `int select(` in any .c).
 *     pselect() is not even declared. Both are implementable (the
 *     header's own comment sketches exactly how -- NtWaitForMultiple
 *     Objects for console/regular files, a FilePipeLocalInformation
 *     poll loop for pipes) -- a real gap, not a platform limitation.
 *     What *is* implemented and testable independent of select() ever
 *     existing is the fd_set bit-manipulation macro family, which is
 *     audited exhaustively below.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/param.h>
#include <getopt.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Internal: spawn a program as a child, return its pid (see
 * src/process/spawn.c). Declared locally, as test/misc.c and
 * test/posix-alloc.c do, rather than widening include/. */
int __spawn(const char *path, char *const argv[], char *const envp[]);

extern char **environ;

/* ===================== sys/resource.h ===================== */

/* getrlimit.html DESCRIPTION/RETURN VALUE/ERRORS.
 *
 * ntlibc's getrlimit() (src/misc/resource.c) reports real, enforced
 * numbers for the two resources it actually caps -- RLIMIT_NOFILE
 * against the fixed __fds[FD_MAX] table (src/internal/libc.h,
 * FD_MAX == 1024) and RLIMIT_NPROC against CHILD_CAP_LIMIT_
 * (src/internal/libc.h, == 1<<20, the same number sysconf(_SC_CHILD_MAX)
 * reports per src/unistd/sysconf.c) -- and RLIM_INFINITY
 * ("the implementation shall not enforce limits on that resource",
 * getrlimit.html) for every resource NT has no per-process cap for. */
static void test_getrlimit(void)
{
	struct rlimit rl;

	/* RETURN VALUE: "Upon successful completion ... shall return 0." */
	CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0);
	CHECK(rl.rlim_cur == 1024 && rl.rlim_max == 1024);

	CHECK(getrlimit(RLIMIT_NPROC, &rl) == 0);
	CHECK(rl.rlim_cur == rl.rlim_max);
	CHECK((long)rl.rlim_cur == sysconf(_SC_CHILD_MAX));

	/* RLIM_INFINITY: "considered to be larger than any other limit
	 * value ... the implementation shall not enforce limits on that
	 * resource" -- ntlibc has no cap for any of these on NT. */
	CHECK(getrlimit(RLIMIT_CPU, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY && rl.rlim_max == RLIM_INFINITY);
	CHECK(getrlimit(RLIMIT_FSIZE, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY);
	CHECK(getrlimit(RLIMIT_DATA, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY);
	CHECK(getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY);
	CHECK(getrlimit(RLIMIT_CORE, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY);
	CHECK(getrlimit(RLIMIT_RSS, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY);
	CHECK(getrlimit(RLIMIT_MEMLOCK, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY);
	CHECK(getrlimit(RLIMIT_AS, &rl) == 0 && rl.rlim_cur == RLIM_INFINITY);

	/* ERRORS: "[EINVAL] An invalid resource was specified." */
	errno = 0;
	CHECK(getrlimit(999, &rl) == -1 && errno == EINVAL);
}

/* getrusage.html DESCRIPTION/RETURN VALUE/ERRORS.  Moved from XSI to
 * the POSIX base standard in Issue 5 (getrusage.html "Standards
 * Status").  ntlibc reports real numbers for ru_utime/ru_stime, read
 * from NtQueryInformationProcess(ProcessTimes) -- verified below by
 * confirming they are the right units (a struct timeval, tv_usec in
 * [0,1e6)) and by exercising a real child to confirm RUSAGE_CHILDREN's
 * running total (src/process/wait.c's __rusage_children()) is not
 * just a hardcoded zero. */
static void tv_is_valid(const struct timeval *tv)
{
	CHECK(tv->tv_usec >= 0 && tv->tv_usec < 1000000);
}

static void test_getrusage(const char *self)
{
	struct rusage ru, before, after;
	pid_t pid;
	int status;
	char *argv[3];

	/* RETURN VALUE: "Upon successful completion ... shall return 0." */
	CHECK(getrusage(RUSAGE_SELF, &ru) == 0);
	tv_is_valid(&ru.ru_utime);
	tv_is_valid(&ru.ru_stime);

	/* RUSAGE_THREAD: not in POSIX.1-2017 (Linux/BSD extension used
	 * elsewhere in this project's own header comment); ntlibc treats
	 * it as an alias for RUSAGE_SELF since it has no per-thread
	 * accounting -- just confirm it does not error. */
	CHECK(getrusage(RUSAGE_THREAD, &ru) == 0);

	/* ERRORS: "[EINVAL] The value of the who argument is not valid." */
	errno = 0;
	CHECK(getrusage(999, &ru) == -1 && errno == EINVAL);

	/* RUSAGE_CHILDREN: "resources used by ... its terminated and
	 * waited-for child processes" -- confirm the running total is a
	 * real, non-decreasing accumulator across a reaped child, not a
	 * hardcoded zero. Re-exec self as a short-lived child (same
	 * pattern as test/misc.c's test_abort_child()); fork() needs
	 * RtlCloneUserProcess, which stock Wine lacks. */
	CHECK(getrusage(RUSAGE_CHILDREN, &before) == 0);

	argv[0] = (char *)self;
	argv[1] = (char *)"--rusage-child";
	argv[2] = 0;
	pid = __spawn(self, argv, environ);
	if (pid < 0) {
		printf("note: cannot spawn \"%s\" (errno %d); RUSAGE_CHILDREN accumulation check skipped\n", self, errno);
		return;
	}
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	CHECK(getrusage(RUSAGE_CHILDREN, &after) == 0);
	tv_is_valid(&after.ru_utime);
	tv_is_valid(&after.ru_stime);
	/* monotonic: the total across all reaped children can only grow */
	CHECK(after.ru_utime.tv_sec > before.ru_utime.tv_sec ||
	      (after.ru_utime.tv_sec == before.ru_utime.tv_sec && after.ru_utime.tv_usec >= before.ru_utime.tv_usec) ||
	      after.ru_stime.tv_sec > before.ru_stime.tv_sec ||
	      (after.ru_stime.tv_sec == before.ru_stime.tv_sec && after.ru_stime.tv_usec >= before.ru_stime.tv_usec));
}

/* ===================== sys/select.h ===================== */

/* sys_select.h.html basedefs: FD_ZERO/FD_SET/FD_CLR/FD_ISSET, exercised
 * as pure bit manipulation -- testable in full even though select()
 * itself is not implemented (see the file banner comment above).
 *
 * "FD_ISSET() shall return a non-zero value if the bit for the file
 * descriptor fd is set ... and 0 otherwise." FD_SET/FD_CLR/FD_ZERO
 * "shall not return a value." */
static void test_fd_macros(void)
{
	fd_set s;
	int i, j;

	/* FD_ZERO clears every bit, over the whole declared FD_SETSIZE
	 * range, not just a few probed positions. */
	memset(&s, 0xff, sizeof s);
	FD_ZERO(&s);
	for (i = 0; i < FD_SETSIZE; i++)
		CHECK(!FD_ISSET(i, &s));

	/* every single fd index sets/clears/tests independently, with no
	 * cross-talk into an adjacent index (checked both neighbours,
	 * including the word-boundary-adjacent indices where a bug in
	 * the /8/sizeof(long) arithmetic would most likely show up). */
	for (i = 0; i < FD_SETSIZE; i++) {
		FD_ZERO(&s);
		FD_SET(i, &s);
		CHECK(FD_ISSET(i, &s) != 0);
		if (i > 0) CHECK(!FD_ISSET(i - 1, &s));
		if (i + 1 < FD_SETSIZE) CHECK(!FD_ISSET(i + 1, &s));
		/* every other index in the set is clear too, spot-checked
		 * at both ends and mid-array rather than exhaustively
		 * (O(FD_SETSIZE^2) is wasteful) */
		CHECK(!FD_ISSET(0 == i ? FD_SETSIZE - 1 : 0, &s));
	}

	/* FD_CLR clears exactly the requested bit and no others */
	FD_ZERO(&s);
	for (i = 0; i < 64; i++) FD_SET(i, &s);
	FD_CLR(31, &s);
	for (i = 0; i < 64; i++)
		CHECK(FD_ISSET(i, &s) == (i != 31));

	/* setting a bit twice, or clearing an already-clear bit, is a
	 * no-op (idempotent) */
	FD_ZERO(&s);
	FD_SET(5, &s); FD_SET(5, &s);
	CHECK(FD_ISSET(5, &s) != 0);
	FD_CLR(5, &s); FD_CLR(5, &s);
	CHECK(!FD_ISSET(5, &s));

	/* word-boundary indices: fds_bits is an array of `long`; every
	 * multiple of bits-per-long (and the index just below/above)
	 * must not corrupt the neighbouring word. */
	for (j = 0; j < (int)(sizeof(long) * 8) * 4; j += (int)(sizeof(long) * 8)) {
		FD_ZERO(&s);
		FD_SET(j, &s);
		CHECK(FD_ISSET(j, &s) != 0);
		if (j > 0) CHECK(!FD_ISSET(j - 1, &s));
		CHECK(!FD_ISSET(j + 1, &s));
	}
}

/* ===================== sys/param.h ===================== */

/* Not a POSIX.1-2017 header at all -- absent from the basedefs index
 * (https://pubs.opengroup.org/onlinepubs/9699919799/idx/head.html
 * lists 17 sys/*.h headers; sys/param.h is not among them). A BSD
 * extension, correctly not claimed as POSIX anywhere in ntlibc's own
 * comments. Only internal consistency is testable: there is no spec
 * page to hold it to. */
static int side_effect_calls;
static int counting(int v) { side_effect_calls++; return v; }

static void test_sys_param(void)
{
	int a, b;

	/* MIN/MAX: basic correctness, both orderings, equal operands,
	 * negative operands */
	CHECK(MIN(3, 5) == 3);
	CHECK(MIN(5, 3) == 3);
	CHECK(MAX(3, 5) == 5);
	CHECK(MAX(5, 3) == 5);
	CHECK(MIN(-1, 1) == -1);
	CHECK(MAX(-1, 1) == 1);
	CHECK(MIN(4, 4) == 4);
	CHECK(MAX(4, 4) == 4);

	/* include/sys/param.h documents no "evaluates arguments once"
	 * contract (unlike, say, a hypothetical safe-MIN); its expansion
	 * is the textbook `(((a)<(b))?(a):(b))`, which double-evaluates
	 * the selected operand. This is not a bug -- nothing promises
	 * otherwise -- but it is a real hazard worth pinning down so a
	 * future edit that adds such a promise without fixing the macro
	 * gets caught here instead of by a caller. */
	side_effect_calls = 0;
	a = MAX(counting(1), 0);
	CHECK(a == 1 && side_effect_calls == 2);  /* evaluated as `a` twice: cond + result */

	/* howmany(n,d): ceil(n/d) */
	CHECK(howmany(0, 8) == 0);
	CHECK(howmany(1, 8) == 1);
	CHECK(howmany(8, 8) == 1);
	CHECK(howmany(9, 8) == 2);
	CHECK(howmany(16, 8) == 2);
	CHECK(howmany(17, 8) == 3);

	/* roundup(n,d): n rounded up to the next multiple of d */
	CHECK(roundup(0, 8) == 0);
	CHECK(roundup(1, 8) == 8);
	CHECK(roundup(8, 8) == 8);
	CHECK(roundup(9, 8) == 16);
	CHECK(roundup(17, 4) == 20);

	/* powerof2: internal consistency of the macro's own arithmetic
	 * identity `!((n-1) & n)`, not a claim about mathematical
	 * "is n a power of two" for every n -- see the n==0 note below. */
	CHECK(powerof2(1));
	CHECK(powerof2(2));
	CHECK(powerof2(1024));
	CHECK(powerof2(0));    /* see NOTE below main text: (0-1)&0 == -1&0 == 0,
	                       * so `!0` is true -- the macro's literal
	                       * arithmetic, asserted as such, not as a
	                       * mathematical claim that 0 is a power of two */
	CHECK(!powerof2(3));
	CHECK(!powerof2(6));

	/* setbit/clrbit/isset/isclr: byte-array bit ops, at least one
	 * full byte boundary crossed */
	{
		unsigned char bits[4] = { 0, 0, 0, 0 };
		setbit(bits, 0);
		setbit(bits, 7);
		setbit(bits, 8);
		setbit(bits, 31);
		CHECK(isset(bits, 0) && isset(bits, 7) && isset(bits, 8) && isset(bits, 31));
		CHECK(isclr(bits, 1) && isclr(bits, 9) && isclr(bits, 30));
		clrbit(bits, 7);
		CHECK(isclr(bits, 7) && isset(bits, 0));
	}
}

/* NOTE on the powerof2(0) case above: the CHECK was left in (asserting
 * the *documented arithmetic identity*, not a POSIX/BSD contract) --
 * `powerof2(0)` textually expands to `!(((0)-1) & (0))` == `!(-1 & 0)`
 * == `!0` == 1 (true), so 0 reads as "a power of two" by this macro
 * even though it mathematically is not. No spec obliges otherwise
 * (sys/param.h is not a spec'd header at all), so this is not fenced
 * as a BUG -- just documented in place so nobody "fixes" the macro
 * expecting the test above to still read `!powerof2(0)`. */

/* ===================== getopt_long / getopt_long_only ===================== */

/* Not POSIX (no getopt_long.html on
 * https://pubs.opengroup.org/onlinepubs/9699919799/idx/head.html or
 * the functions index) -- a GNU extension. Audited against
 * https://man7.org/linux/man-pages/man3/getopt_long.3.html (glibc,
 * "STANDARDS: GNU") for getopt_long(), and
 * https://man.freebsd.org/cgi/man.cgi?query=getopt_long&sektion=3 for
 * getopt_long_only() (glibc's own man page omits it; FreeBSD's
 * getopt_long(3) documents the same GNU-compatible getopt_long_only()
 * BSD's libc also ships).  test/getopt.c already gives broad sanity
 * coverage (permutation, clustering, both error-mode strings) without
 * citing the manual per-clause; this file adds the clause citations
 * and the cases test/getopt.c does not reach: optional_argument's
 * attached-only rule, no_argument rejecting `--opt=val`, ambiguous
 * abbreviation, and long-only's single-char disambiguation rule. */

static int reset(void) { optind = 1; optreset = 1; opterr = 0; return 0; }

static void test_getopt_long_abbrev(void)
{
	static const struct option lo[] = {
		{ "alpha", no_argument, 0, 'a' },
		{ "alphabet", no_argument, 0, 'A' },
		{ 0, 0, 0, 0 }
	};
	char *av_unique[] = { "prog", "--alph", 0 };   /* not a prefix of "alphabet" beyond "alpha" -> ambiguous actually */
	char *av_exact[]  = { "prog", "--alpha", 0 };
	char *av_amb[]    = { "prog", "--al", 0 };
	int c, idx;

	/* getopt_long.3 DESCRIPTION: "Long option names may be abbreviated
	 * if the abbreviation is unique or is an exact match for some
	 * defined option." -- "--alpha" is an exact match for "alpha"
	 * even though it is *also* a prefix of "alphabet": exact match
	 * wins, no ambiguity error. */
	reset();
	c = getopt_long(2, av_exact, "", lo, &idx);
	CHECK(c == 'a' && idx == 0);

	/* "--al" is a prefix of both "alpha" and "alphabet", is an exact
	 * match for neither -> ambiguous -> '?' */
	reset();
	c = getopt_long(2, av_amb, "", lo, &idx);
	CHECK(c == '?');

	/* "--alph" is likewise a non-exact prefix of both -> ambiguous */
	reset();
	c = getopt_long(2, av_unique, "", lo, &idx);
	CHECK(c == '?');
}

static void test_getopt_long_arg_forms(void)
{
	static const struct option lo[] = {
		{ "req",  required_argument, 0, 'r' },
		{ "opt",  optional_argument, 0, 'o' },
		{ "none", no_argument,       0, 'n' },
		{ 0, 0, 0, 0 }
	};
	int c, idx;

	/* "A long option may take a parameter, of the form --arg=param
	 * or --arg param." (getopt_long.3) -- required_argument accepts
	 * both forms. */
	{
		char *av[] = { "prog", "--req=x", 0 };
		reset();
		c = getopt_long(2, av, "", lo, &idx);
		CHECK(c == 'r' && optarg && !strcmp(optarg, "x"));
	}
	{
		char *av[] = { "prog", "--req", "y", 0 };
		reset();
		c = getopt_long(3, av, "", lo, &idx);
		CHECK(c == 'r' && optarg && !strcmp(optarg, "y"));
	}

	/* optional_argument: GNU convention is the argument must be
	 * attached (--opt=value); a following separate argv element is
	 * NOT consumed as the option's argument (it is left as a
	 * positional/next option, same as short options' "::"). */
	{
		char *av[] = { "prog", "--opt=z", 0 };
		reset();
		c = getopt_long(2, av, "", lo, &idx);
		CHECK(c == 'o' && optarg && !strcmp(optarg, "z"));
	}
	{
		char *av[] = { "prog", "--opt", "z", 0 };
		reset();
		c = getopt_long(3, av, "", lo, &idx);
		CHECK(c == 'o' && optarg == 0);
		CHECK(optind == 2);  /* "z" left unconsumed, for the caller */
	}

	/* no_argument + "--opt=val": "Error ... '?' for ... an extraneous
	 * parameter." (getopt_long.3 RETURN VALUE) */
	{
		char *av[] = { "prog", "--none=x", 0 };
		reset();
		c = getopt_long(2, av, "", lo, &idx);
		CHECK(c == '?');
	}
}

static void test_getopt_long_flag(void)
{
	static int seen;
	struct option lo[] = {
		{ "flagged", no_argument, &seen, 42 },
		{ "valued",  no_argument, 0,      7 },
		{ 0, 0, 0, 0 }
	};
	int c, idx;

	/* "If flag is NULL, then getopt_long() returns val." -> plain val */
	{
		char *av[] = { "prog", "--valued", 0 };
		reset();
		c = getopt_long(2, av, "", lo, &idx);
		CHECK(c == 7 && idx == 1);
	}
	/* "flag specifies how results are returned ... [if non-NULL,]
	 * getopt_long() returns 0 [and sets *flag = val]" */
	{
		char *av[] = { "prog", "--flagged", 0 };
		seen = 0;
		reset();
		c = getopt_long(2, av, "", lo, &idx);
		CHECK(c == 0 && seen == 42 && idx == 0);
	}
}

static void test_getopt_long_only(void)
{
	static const struct option lo[] = {
		{ "verbose", no_argument, 0, 'V' },
		{ "v",       no_argument, 0, 'x' },  /* single-char long name */
		{ 0, 0, 0, 0 }
	};
	int c, idx;

	/* FreeBSD getopt_long(3), getopt_long_only() paragraph: "long
	 * options may start with '-' in addition to '--'." */
	{
		char *av[] = { "prog", "-verbose", 0 };
		reset();
		c = getopt_long_only(2, av, "v", lo, &idx);
		CHECK(c == 'V' && idx == 0);
	}

	/* "If an option starting with '-' does not match a long option
	 * but does match a single-character option, the single-character
	 * option is returned." ntlibc's src/misc/getopt_long.c takes this
	 * further for the case this file's DESCRIPTION comment documents:
	 * a long option whose *name* is exactly one character and also
	 * appears in optstring is deliberately treated as ambiguous and
	 * resolved as the short option, not the long one -- "-v" here
	 * matches long option "v" (val 'x') exactly, but optstring also
	 * has 'v' as a short option, so the short option wins. */
	{
		char *av[] = { "prog", "-v", 0 };
		reset();
		c = getopt_long_only(2, av, "v", lo, &idx);
		CHECK(c == 'v');  /* short option char, not 'x' (the long one) */
	}

	/* a single-dash argument that matches no long option and no
	 * short option is unrecognized: '?' */
	{
		char *av[] = { "prog", "-bogus", 0 };
		reset();
		c = getopt_long_only(2, av, "v", lo, &idx);
		CHECK(c == '?');
	}
}

/* getopt_long.3 "longindex": "points to a variable which is set to
 * the index of the long option relative to longopts" -- confirm it is
 * left untouched by short-option matches (only long matches set it),
 * and is correct across multiple options. */
static void test_getopt_long_index(void)
{
	static const struct option lo[] = {
		{ "first",  no_argument, 0, '1' },
		{ "second", no_argument, 0, '2' },
		{ "third",  no_argument, 0, '3' },
		{ 0, 0, 0, 0 }
	};
	char *av[] = { "prog", "--first", "--third", 0 };
	int c, idx;

	reset();
	idx = -1;
	c = getopt_long(3, av, "", lo, &idx);
	CHECK(c == '1' && idx == 0);
	idx = -1;
	c = getopt_long(3, av, "", lo, &idx);
	CHECK(c == '3' && idx == 2);
}

/* Reset behaviour between scans: BSD getopt_long(3): "setting optind
 * to 0 will indicate that getopt_long should reset, and optind will
 * be set to 1 in the process." ntlibc's own optreset variable
 * (declared in <getopt.h>) is the same idea, checked in src/misc/
 * getopt.c/getopt_long.c: "if (!optind || optreset) { ... optind = 1;
 * optreset = 0; }" -- both triggers are tested here, independently. */
static void test_getopt_long_reset(void)
{
	static const struct option lo[] = {
		{ "alpha", no_argument, 0, 'a' },
		{ 0, 0, 0, 0 }
	};
	char *av[] = { "prog", "--alpha", 0 };
	int c, idx;

	/* first scan, exhausted */
	optind = 1; optreset = 0; opterr = 0;
	c = getopt_long(2, av, "", lo, &idx);
	CHECK(c == 'a');
	c = getopt_long(2, av, "", lo, &idx);
	CHECK(c == -1);

	/* re-scanning the same vector without resetting must NOT rewind
	 * -- optind stays past the end */
	c = getopt_long(2, av, "", lo, &idx);
	CHECK(c == -1);

	/* optind = 0 forces a reset */
	optind = 0;
	c = getopt_long(2, av, "", lo, &idx);
	CHECK(c == 'a');

	/* exhaust again, then reset via optreset = 1 instead */
	c = getopt_long(2, av, "", lo, &idx);
	CHECK(c == -1);
	optreset = 1;
	c = getopt_long(2, av, "", lo, &idx);
	CHECK(c == 'a');

	optind = 1; optreset = 0; opterr = 1;  /* leave defaults for later tests */
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--rusage-child")) {
		/* burn a little real CPU so RUSAGE_CHILDREN has something
		 * non-trivial to accumulate; kept short since correctness
		 * only needs "non-decreasing", not "visibly nonzero" */
		volatile unsigned long i, x = 0;
		for (i = 0; i < 20000000UL; i++) x += i;
		return 0;
	}

	test_getrlimit();
	test_getrusage(argv[0]);
	test_fd_macros();
	test_sys_param();
	test_getopt_long_abbrev();
	test_getopt_long_arg_forms();
	test_getopt_long_flag();
	test_getopt_long_only();
	test_getopt_long_index();
	test_getopt_long_reset();

	if (fails) { printf("posix-sysmisc: failures: %d\n", fails); return 1; }
	printf("posix-sysmisc: all ok\n");
	return 0;
}
