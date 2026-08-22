/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of the small, never-audited
 * headers <strings.h>, the XSI additions to <ctype.h>, <assert.h>,
 * <utime.h>, and <endian.h> (not a POSIX header at all -- see below).
 * Each block cites the page it was checked against under
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 * (or .../basedefs/<header>.html for header-level requirements).
 *
 * strcasecmp/strncasecmp and ffs already have sanity coverage in
 * test/string.c and an ordering-sign check in test/posix-string.c;
 * this file adds the clauses those did not reach: strcasecmp's
 * unsigned-char/bytes->=0x80 behavior and a full ffs() bit sweep.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <endian.h>
#include <assert.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern char **environ;

/* Internal: spawn a program as a child, return its pid (see
 * src/process/spawn.c).  fork() needs RtlCloneUserProcess, which stock
 * Wine lacks; declared locally as test/misc.c does, rather than
 * widening include/. */
int __spawn(const char *path, char *const argv[], char *const envp[]);

/* ============================== strings.h ============================== */

/* strcasecmp.html DESCRIPTION: "use the current locale to determine the
 * case of the characters"; for the POSIX locale (the only one ntlibc's
 * LC_CTYPE ever is -- src/ctype/is*.c classify by fixed ASCII ranges,
 * never consulting a locale object) "these functions shall behave as
 * if the strings had been converted to lowercase and then a byte
 * comparison performed."  ntlibc's tolower() only maps 'A'-'Z'
 * (src/ctype/tolower.c: isupper() is `(unsigned)c-'A' < 26`), so bytes
 * >= 0x80 are never touched by the case fold and fall straight through
 * to a byte comparison.  strcasecmp() casts to `const unsigned char *`
 * before comparing, so that comparison -- and the sign of the return
 * value, which RETURN VALUE ties to "greater than, equal to, or less
 * than" on the (lowercased) strings -- must treat 0x80-0xff as large
 * positive values, not as negative `char`s.  This is the one bug class
 * a `char`-typed rewrite of strcasecmp commonly introduces, so it is
 * swept deliberately rather than spot-checked. */
static void test_strcasecmp_high_bytes(void)
{
	unsigned i;
	for (i = 0x80; i <= 0xff; i++) {
		char a[2], b[2];
		a[0] = (char)i; a[1] = 0;
		b[0] = (char)0x7f; b[1] = 0;
		/* every byte >= 0x80 must compare as unsigned-char greater
		 * than 0x7f -- a signed-char bug would report these as
		 * negative, i.e. *less* than 0x7f */
		CHECK(strcasecmp(a, b) > 0);
		CHECK(strncasecmp(a, b, 1) > 0);
	}
	/* ordering among high bytes themselves must follow unsigned value,
	 * and since none of them get case-folded (no 'A'-'Z'/'a'-'z'
	 * mapping applies), the comparison is a plain unsigned-char one */
	CHECK(strcasecmp("\x80", "\xff") < 0);
	CHECK(strcasecmp("\xff", "\x80") > 0);
	CHECK(strcasecmp("\x80", "\x80") == 0);
	/* mixed: a high byte against a foldable ASCII letter is still a
	 * plain unsigned-char comparison since only the letter folds */
	CHECK(strcasecmp("\xff", "A") > 0 && strcasecmp("\xff", "a") > 0);
	CHECK(strncasecmp("\xff", "A", 1) > 0);
}

/* strcasecmp.html RETURN VALUE: sign reflects the comparison of the
 * *lowercased* strings, not the raw bytes -- exercised here with
 * mixed-case strings that would compare the other way if the fold did
 * not happen (raw "B" < "a" as unsigned char, 0x42 < 0x61) but must
 * fold to "b" > "a". */
static void test_strcasecmp_folds_before_comparing(void)
{
	CHECK(strcmp("B", "a") < 0);          /* sanity: raw bytes disagree */
	CHECK(strcasecmp("B", "a") > 0);      /* folded: 'b' > 'a' */
	CHECK(strncasecmp("B", "a", 1) > 0);
	CHECK(strcasecmp("b", "A") > 0);
	CHECK(strcasecmp_l("B", "a", (locale_t)0) > 0);
	CHECK(strncasecmp_l("B", "a", 1, (locale_t)0) > 0);
}

/* ffs.html DESCRIPTION/RETURN VALUE: "find the first bit set (beginning
 * with the least significant bit) ... Bits are numbered starting at
 * one (the least significant bit) ... If i is 0, then ffs() shall
 * return 0."  Every bit position is swept, not sampled, per the task
 * brief -- ffs's whole contract is a per-bit-position table. */
static void test_ffs_every_bit(void)
{
	int bit;
	CHECK(ffs(0) == 0);
	for (bit = 0; bit < (int)sizeof(int) * CHAR_BIT - 1; bit++) {
		CHECK(ffs(1 << bit) == bit + 1);
		/* setting every bit above `bit` too must not change the
		 * answer: ffs() finds the *least* significant set bit */
		CHECK(ffs((int)(~0u << bit)) == bit + 1);
	}
	/* the sign bit (INT_MIN, bit 31 on a 32-bit int): ffs takes int
	 * but the standard's bit numbering does not exempt the sign bit */
	CHECK(ffs(INT_MIN) == (int)sizeof(int) * CHAR_BIT);
	CHECK(ffs(-1) == 1); /* all bits set: LSB wins */
}

/* strings.h.html: legacy bcmp/bcopy/bzero/index/rindex were removed
 * from POSIX.1-2017's <strings.h> (present through SUSv3, gone in
 * Issue 7 / SUSv4).  ntlibc still ships them (src/string/bcmp.c etc.),
 * gated in include/strings.h behind "not _POSIX_SOURCE and not
 * _XOPEN_SOURCE>=700", i.e. exposed as a non-standard extension, not
 * claimed as base conformance.  Confirmed present and functioning here
 * as an extension; test/string.c already sanity-covers them and the
 * ledger records the removal, so this is not re-duplicated in depth. */
static void test_legacy_extensions_present(void)
{
	char buf[4] = "abc";
	CHECK(bcmp("abc", "abc", 3) == 0);
	CHECK(bcmp("abc", "abd", 3) != 0);
	bcopy("xyz", buf, 3); CHECK(!memcmp(buf, "xyz", 3));
	bzero(buf, 3); CHECK(!memcmp(buf, "\0\0\0", 3));
	{
		/* compare against strchr/strrchr on the *same* buffer --
		 * two separate string-literal calls are not guaranteed to
		 * share storage, so comparing across two literals would
		 * not actually test index()/rindex()'s search behavior */
		char s[] = "hello";
		CHECK(index(s, 'l') == strchr(s, 'l'));
		CHECK(rindex(s, 'l') == strrchr(s, 'l'));
	}
}

/* ============================== ctype.h (XSI) ============================== */

/* isalpha.html APPLICATION USAGE / isascii.html, toascii.html
 * DESCRIPTION: unlike the other is-x/to-x classifiers, whose argument
 * "shall be representable as an unsigned char or ... EOF" (UB
 * otherwise -- test/ctype.c and test/posix-misc.c cover that family),
 * "isascii() is defined on all integer values" and toascii() "shall
 * return the value (c & 0x7f)" for any int c.  That asymmetry is
 * exercised here with values well outside unsigned-char/EOF range:
 * negative values other than EOF, and values >= 256. */
static void test_isascii_toascii_defined_for_all_ints(void)
{
	static const int vals[] = {
		0, 1, 127, 128, 255, 256, 257, 1000, 65536,
		-1, -2, -128, -129, -256, -1000, INT_MAX, INT_MIN
	};
	unsigned i;
	for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		int c = vals[i];
		/* isascii.html RETURN VALUE: non-zero iff 0 <= c <= 0177 */
		int want_ascii = (c >= 0 && c <= 0177);
		CHECK(!!isascii(c) == want_ascii);
		/* toascii.html RETURN VALUE: "the value (c & 0x7f)",
		 * exactly, for every int -- including negative c, where
		 * the bitwise-and is on the int's representation */
		CHECK(toascii(c) == (c & 0x7f));
	}
}

/* _toascii/_tolower legacy macros (basedefs/ctype.h.html declares
 * `int _toupper(int); int _tolower(int);` with no behavioral
 * description of their own on that page; functions/_toupper.html
 * supplies it: "_toupper() ... shall be equivalent to toupper() except
 * that the application shall ensure that the argument c is a
 * lowercase letter" -- and symmetrically for _tolower()/uppercase.
 * Only the in-contract inputs are asserted; behavior outside them is
 * unspecified by the application-usage contract, not by ntlibc, so
 * nothing is asserted there. */
static void test_underscore_tolower_toupper(void)
{
	int c;
	for (c = 'a'; c <= 'z'; c++) CHECK(_toupper(c) == toupper(c));
	for (c = 'A'; c <= 'Z'; c++) CHECK(_tolower(c) == tolower(c));
}

/* ============================== assert.h ============================== */

/* assert.html DESCRIPTION: "Forcing a definition of the name NDEBUG,
 * either from the compiler command line or with the preprocessor
 * control statement #define NDEBUG ahead of the #include <assert.h>
 * statement, shall stop assertions from being compiled into the
 * program" -- and per include/assert.h, that decision is remade by
 * every #include <assert.h>, not fixed once per translation unit (the
 * header #undefs and redefines the assert() macro each time,
 * conditioned on NDEBUG's state *at that point*).  A single TU can
 * therefore have assert() active, then inactive, then active again.
 * Proved directly: the same TU re-includes <assert.h> after toggling
 * NDEBUG and checks, via a side effect inside the asserted expression,
 * whether it was evaluated at all -- an inactive assert() must not
 * evaluate its argument, since it expands to (void)0. */
static int ndebug_side_effect;

static void test_assert_active_before_ndebug(void)
{
	ndebug_side_effect = 0;
	assert((ndebug_side_effect = 1, 1));
	CHECK(ndebug_side_effect == 1); /* assert() evaluated its argument */
}

#define NDEBUG
#include <assert.h>	/* re-include: NDEBUG is now defined */

static void test_assert_inactive_under_ndebug(void)
{
	ndebug_side_effect = 0;
	assert((ndebug_side_effect = 1, 1));
	CHECK(ndebug_side_effect == 0); /* assert() compiled to (void)0 */
}

#undef NDEBUG
#include <assert.h>	/* re-include again: NDEBUG undefined once more */

static void test_assert_active_after_ndebug_undef(void)
{
	ndebug_side_effect = 0;
	assert((ndebug_side_effect = 1, 1));
	CHECK(ndebug_side_effect == 1); /* active again in the same TU */
}

/* assert.html DESCRIPTION: on failure, "information about the
 * particular call that failed ... shall include the text of the
 * argument, the name of the source file, the source file line number,
 * and the name of the enclosing function" written to stderr, and the
 * process is terminated via abort() (assert() "shall be equivalent to
 * ... abort()").  Needs an observed child: redirect the child's stderr
 * through a pipe (__assert_fail's fflush(0) guarantees the buffered
 * text is written before abort() tears the process down), let the
 * assertion fire, then have the parent read the pipe back and check
 * every element the clause promises is present. */

/* Records the physical source line of the assert() call into the very
 * file __assert_fail() is about to write to, using the *same*
 * preprocessor __LINE__ expansion point as the assert() call right
 * next to it on one line -- so "expected-line:N" and whatever line
 * number __assert_fail() actually prints come from independent
 * __LINE__ evaluations of the identical source position, with no
 * manually-counted offset to get wrong. */
#define ASSERT_MARK_THEN_FIRE(cond) do { fprintf(stderr, "expected-line:%d\n", __LINE__); assert(cond); } while (0)

static void child_assert_marker(void)
{
	ASSERT_MARK_THEN_FIRE(0 == 1); /* deliberately false: "0 == 1" must appear verbatim */
}

/* Captures the child's stderr through a real pipe rather than a named
 * file: __spawn only carries real OS handles across into the child
 * (fds 0-2 by the STARTF_USESTDHANDLES contract src/process/spawn.c
 * documents, or on this project's native-ASan test harness, "only
 * descriptors 0-2, real host fds, survive the trip" per
 * fuzz/ntstubs.c's process-spawning comment -- either way, a *named
 * file* the child writes is not guaranteed visible to the parent, but
 * an inherited fd 2 is). */
static void test_assert_message_and_death(const char *self)
{
	char *argv[3];
	int pid, status;
	int p[2];
	int saved_stderr;
	char msg[512];
	ssize_t n;
	char linebuf[32];
	int expected_line;
	const char *q;

	CHECK(pipe(p) == 0);
	saved_stderr = dup(2);
	CHECK(saved_stderr >= 0);
	CHECK(dup2(p[1], 2) == 2);
	close(p[1]);

	argv[0] = (char *)self;
	argv[1] = (char *)"--posix-strings-assert-child";
	argv[2] = 0;
	pid = __spawn(self, argv, environ);

	dup2(saved_stderr, 2);	/* restore our own stderr before anything else prints */
	close(saved_stderr);

	if (pid < 0) {
		printf("note: cannot spawn \"%s\" (errno %d); assert() message/death child test skipped\n", self, errno);
		close(p[0]);
		return;
	}
	CHECK(waitpid(pid, &status, 0) == pid);
	/* abort()'s contract (abort.html) is "SIGABRT-shaped death": either
	 * killed by SIGABRT, or (SIGABRT ignored/blocked in some
	 * environment) a nonzero exit -- same relaxed check test/misc.c
	 * and test/posix-alloc.c use for the same reason. */
	CHECK(WIFSIGNALED(status) ? WTERMSIG(status) == SIGABRT
	                           : (WIFEXITED(status) && WEXITSTATUS(status) != 0));

	/* the child (and every fd 2 handle onto the pipe's write end
	 * except our own already-closed one) is gone, so read() drains
	 * whatever was written and then returns 0 (EOF) rather than
	 * blocking.  On a target where fd inheritance across __spawn does
	 * not reach a real OS-level descriptor table (this project's
	 * native ASan test harness forks the real host process directly,
	 * bypassing ntlibc's own handle layer entirely for that step --
	 * see fuzz/ntstubs.c's RtlCreateUserProcess comment), nothing
	 * arrives here even though the child ran and died correctly, as
	 * just confirmed above; skip only the message-content assertions
	 * in that case rather than failing on an environment limitation
	 * neither this test nor the library controls. */
	n = read(p[0], msg, sizeof(msg) - 1);
	close(p[0]);
	if (n <= 0) {
		printf("note: child's stderr was not observed through the inherited pipe in this environment; assert() message-format check skipped (SIGABRT death was already verified above)\n");
		return;
	}
	msg[n] = 0;

	CHECK(strstr(msg, "0 == 1") != 0);              /* text of the argument */
	CHECK(strstr(msg, "posix-strings.c") != 0);      /* source file name */
	CHECK(strstr(msg, "child_assert_marker") != 0);  /* enclosing function */
	/* "expected-line:N" was written by the same macro invocation that
	 * fires the assert() (see ASSERT_MARK_THEN_FIRE), so N is the
	 * physical source line of the assert() call, independent of the
	 * line number __assert_fail() itself reports; they must match. */
	q = strstr(msg, "expected-line:");
	CHECK(q != 0);
	expected_line = q ? atoi(q + strlen("expected-line:")) : -1;
	CHECK(expected_line > 0);
	sprintf(linebuf, "%d", expected_line);
	/* search only *after* our own "expected-line:N" note, so this
	 * finds N in __assert_fail()'s own "(file: func: line)" text, not
	 * an accidental match against the note itself */
	CHECK(expected_line > 0 && q != 0 &&
	      strstr(q + strlen("expected-line:") + strlen(linebuf), linebuf) != 0);
}

/* ============================== utime.h ============================== */

/* utime.html DESCRIPTION/RETURN VALUE: full happy-path round trip via
 * stat() is already exercised in test/unistd.c (utimensat/utime/
 * futimens/futimesat, UTIME_NOW/UTIME_OMIT, ENOENT).  Not duplicated
 * here; this adds the one clause that pass did not check: "Upon
 * successful completion, 0 shall be returned" (return value, not just
 * the side effect on stat()), and the times==NULL "current time"
 * path's return value likewise. */
#define UTIME_FILE "posix-strings-utime.tmp"

static void test_utime_return_value(void)
{
	int fd;
	struct utimbuf ub;

	unlink(UTIME_FILE);
	fd = open(UTIME_FILE, O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0);
	if (fd >= 0) close(fd);

	ub.actime = 1000000000;
	ub.modtime = 1100000000;
	CHECK(utime(UTIME_FILE, &ub) == 0);   /* RETURN VALUE: 0 on success */

	CHECK(utime(UTIME_FILE, 0) == 0);     /* times==NULL: current time, still 0 */

	/* ERRORS [ENOENT]: "A component of path does not name an existing
	 * file" -- -1 and errno set, matching RETURN VALUE's contract for
	 * the failure path */
	errno = 0;
	CHECK(utime("posix-strings-utime-does-not-exist.tmp", &ub) == -1);
	CHECK(errno == ENOENT);

	unlink(UTIME_FILE);
}

/* ============================== endian.h ============================== */

/* endian.h is not part of POSIX.1-2017: no functions/endian.h.html or
 * basedefs/endian.h.html page exists (verified: the basedefs URL
 * 404s), and it is absent from the base-definitions header index. It
 * is a glibc/BSD extension ntlibc ships for source compatibility;
 * gated behind _GNU_SOURCE/_BSD_SOURCE in include/endian.h. Recorded
 * as an extension, tested only for internal self-consistency per the
 * task brief: __BYTE_ORDER names a real endianness, and the
 * byte-swap helpers actually swap. */
static void test_endian_internal_consistency(void)
{
	CHECK(__BYTE_ORDER == __LITTLE_ENDIAN || __BYTE_ORDER == __BIG_ENDIAN);
	CHECK(BYTE_ORDER == __BYTE_ORDER);
	CHECK(htobe16(0x0102) == be16toh(0x0102)); /* both directions of the same swap */
	CHECK(htobe16(0x0102) == 0x0201);
	CHECK(htobe32(0x01020304) == 0x04030201u);
	CHECK((uint64_t)htobe64(0x0102030405060708ULL) == 0x0807060504030201ULL);
	/* le helpers are identity on this (little-endian-only) target */
	CHECK(htole16(0x0102) == 0x0102);
	CHECK(htole32(0x01020304) == 0x01020304u);
	CHECK(le16toh(0x0102) == 0x0102);
	/* htobe/be16toh must be inverses of each other */
	CHECK(be16toh(htobe16(0xabcd)) == 0xabcd);
	CHECK(be32toh(htobe32(0xdeadbeefu)) == 0xdeadbeefu);
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--posix-strings-assert-child")) {
		child_assert_marker();
		return 0; /* unreachable if assert() fired as required */
	}

	test_strcasecmp_high_bytes();
	test_strcasecmp_folds_before_comparing();
	test_ffs_every_bit();
	test_legacy_extensions_present();
	test_isascii_toascii_defined_for_all_ints();
	test_underscore_tolower_toupper();
	test_assert_active_before_ndebug();
	test_assert_inactive_under_ndebug();
	test_assert_active_after_ndebug_undef();
	test_assert_message_and_death(argv[0]);
	test_utime_return_value();
	test_endian_internal_consistency();

	if (!fails) printf("posix-strings: all tests passed\n");
	return fails != 0;
}
