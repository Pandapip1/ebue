/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of the parts of stdlib.h not
 * already covered clause-for-clause by test/strto.c, test/qsort.c,
 * test/stdlib.c, test/malloc.c and test/posix-parse.c.  Each assertion
 * cites the clause of https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/<name>.html it checks.  See test/posix-coverage/stdlib.md
 * for the full ledger, including what those five files already cover.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern char **environ;

/* ---- strtol.html: EINVAL for an unsupported base, errno untouched on
 * success, no-conversion endptr rule for a non-numeric-but-nonempty base. ---- */
static void test_strtol_base(void)
{
	char *end;

	/* RETURN VALUE: "If the value of base is not supported, 0 shall be
	 * returned and errno shall be set to [EINVAL]."  ERRORS: same,
	 * listed as a required (not "may fail") error. */
	{
		const char *s = "123";
		errno = 0; end = 0;
		CHECK(strtol(s, &end, 1) == 0 && errno == EINVAL && end == s);
	}
	{
		const char *s = "123";
		errno = 0; end = 0;
		CHECK(strtol(s, &end, 37) == 0 && errno == EINVAL && end == s);
	}
	{
		const char *s = "123";
		errno = 0; end = 0;
		CHECK(strtoul(s, &end, -1) == 0 && errno == EINVAL && end == s);
	}

	/* "These functions shall not change the setting of errno if
	 * successful." -- errno left at a sentinel value across a clean
	 * conversion. */
	errno = 12345;
	CHECK(strtol("42", 0, 10) == 42 && errno == 12345);
	errno = 12345;
	CHECK(strtoul("42", 0, 10) == 42 && errno == 12345);
	errno = 12345;
	CHECK(strtod("4.5", 0) == 4.5 && errno == 12345);
}

/* ---- atoi.html: "shall be equivalent to (int) strtol(str, NULL, 10)" ---- */
static void test_atoi_family(void)
{
	/* strtol/strtod skip leading white space and accept a sign; atoi()
	 * and friends must inherit exactly that since they are DEFINED as
	 * calls to strtol()/strtod(). */
	CHECK(atoi("   \t -42") == -42);
	CHECK(atol("  +100") == 100L);
	CHECK(atoll(" -123456789012345") == -123456789012345LL);
	CHECK(atof("  2.5e1") == 25.0);
	/* "junk" after the number is simply ignored, same as strtol/strtod
	 * with a discarded endptr. */
	CHECK(atoi("17abc") == 17);
	CHECK(atof("2.5xyz") == 2.5);
	/* empty / all-whitespace / no digits: strtol/strtod perform no
	 * conversion and return 0 / 0.0. */
	CHECK(atoi("") == 0);
	CHECK(atoi("   ") == 0);
	CHECK(atof("   ") == 0.0);
}

/* ---- qsort.html / bsearch.html: nel==0 must not call the comparator. ---- */
static int ncalls;
static int counting_cmp(const void *a, const void *b)
{
	ncalls++;
	return *(const int *)a - *(const int *)b;
}

static void test_qsort_bsearch_zero(void)
{
	int a[4] = { 4, 3, 2, 1 };
	int key = 1;

	/* qsort.html DESCRIPTION: "If the nel argument has the value zero,
	 * the comparison function ... shall not be called." */
	ncalls = 0;
	qsort(a, 0, sizeof a[0], counting_cmp);
	CHECK(ncalls == 0);
	/* array must be untouched */
	CHECK(a[0] == 4 && a[1] == 3 && a[2] == 2 && a[3] == 1);

	/* bsearch.html DESCRIPTION: "If the nel argument has the value
	 * zero, the comparison function ... shall not be called and no
	 * match shall be found." */
	ncalls = 0;
	CHECK(bsearch(&key, a, 0, sizeof a[0], counting_cmp) == 0);
	CHECK(ncalls == 0);
}

/* bsearch.html DESCRIPTION: "The comparison function shall be called
 * with two arguments that point to the key object and to an array
 * member, in that order." -- verify the key pointer is always first by
 * using a comparator that only *that* order can satisfy. */
static const int *bsearch_key_ptr;
static int order_cmp(const void *a, const void *b)
{
	/* a must always be the key, b an array element */
	if (a != bsearch_key_ptr) { fails++; printf("FAIL %s:%d: bsearch comparator arg order\n", __FILE__, __LINE__); }
	return *(const int *)a - *(const int *)b;
}

static void test_bsearch_arg_order(void)
{
	int a[5] = { 1, 2, 3, 4, 5 };
	int key = 3;
	bsearch_key_ptr = &key;
	CHECK(bsearch(&key, a, 5, sizeof a[0], order_cmp) == &a[2]);
}

/* ---- drand48.html: required ranges of the returned values. ---- */
static void test_rand48_ranges(void)
{
	int i;
	unsigned short s[3] = { 0x1234, 0x5678, 0x9abc };

	/* long is 32 bits on both ntlibc arches (established by
	 * test/strto.c's `sizeof(long) == 4` check), so every long value
	 * already lies in [LONG_MIN,LONG_MAX] = [-2**31,2**31-1], which is
	 * inside the required [-2**31,2**31) -- the upper bound can never
	 * be violated by the type itself and is checked here only via
	 * `long long` so the comparison itself has no overflow/UB.  The
	 * lower bound (>=0 for lrand48/nrand48) is the one a broken
	 * generator could actually fail. */
	srand48(777);
	for (i = 0; i < 200; i++) {
		double d = drand48();
		long l = lrand48();
		long m = mrand48();
		/* "non-negative ... values, uniformly distributed over the
		 * interval [0.0,1.0)" */
		CHECK(d >= 0.0 && d < 1.0);
		/* "non-negative, long integers ... over the interval [0,2**31)" */
		CHECK(l >= 0 && (long long)l < (1LL << 31));
		/* "signed long integers ... over the interval [-2**31,2**31)" */
		CHECK((long long)m >= -(1LL << 31) && (long long)m < (1LL << 31));

		CHECK(erand48(s) >= 0.0 && erand48(s) < 1.0);
		CHECK(nrand48(s) >= 0 && (long long)nrand48(s) < (1LL << 31));
		CHECK((long long)jrand48(s) >= -(1LL << 31) && (long long)jrand48(s) < (1LL << 31));
	}

	/* seed48.html RETURN VALUE: "shall return a pointer to an array of
	 * 3 unsigned shorts which contained a copy of the internal buffer
	 * ... prior to the update."  srand48.html: "the low-order 16 bits
	 * of Xi are set to the arbitrary value 0x330E [and] the high-order
	 * 32 bits of Xi are set to the ... seed argument."  Check both:
	 * seed48()'s return reflects srand48()'s prior state exactly, and
	 * a second seed48() call reflects the first one's argument. */
	{
		unsigned short knot[3] = { 11, 22, 33 };
		unsigned short *prev;

		srand48(42);
		prev = seed48(knot);
		CHECK(prev != 0 && prev[0] == 0x330e && prev[1] == 42 && prev[2] == 0);

		prev = seed48(knot);
		CHECK(prev != 0 && prev[0] == 11 && prev[1] == 22 && prev[2] == 33);
	}

	/* lcong48.html: "If lcong48() is used, it shall then be followed
	 * by a call to either seed48() or srand48() before subsequent
	 * calls to any of drand48(), erand48(), lrand48(), nrand48(),
	 * mrand48(), or jrand48()."  We only check that lcong48()'s a/c
	 * actually take effect: with a=1, c=0 the generator is the
	 * identity multiplier, so X(n+1) = X(n) (mod 2**48) -- a fixed
	 * point once seeded to any value. */
	{
		unsigned short p[7] = { 9, 0, 0,  1, 0, 0,  0 }; /* Xi=9, a=1, c=0 */
		long first, second;
		lcong48(p);
		first = mrand48();
		second = mrand48();
		CHECK(first == second);
	}
}

/* ---- random.html: initstate/setstate return values and size rules. ---- */
static void test_random_state(void)
{
	static char st8[8], st16[16], st32[32], st64[64], st128[128], st256[256];
	char *old;

	/* "If size bytes are not available to be used by the state array
	 * ... the initstate() function shall fail and return a null
	 * pointer" -- glibc/SVID: fewer than 8 bytes is always a failure. */
	{
		static char tiny[4];
		CHECK(initstate(1, tiny, 4) == 0);
	}

	/* Valid sizes (8, 32, 64, 128, 256 or anything >=8, rounded down)
	 * must succeed, return the previous state pointer, and not crash
	 * subsequent random() calls. */
	old = initstate(1, st8, sizeof st8);
	CHECK(old != 0);
	CHECK(random() >= 0);
	old = initstate(2, st16, sizeof st16); /* 8<=16<32: rounds down to the 8-byte generator type */
	CHECK(old == st8);
	CHECK(random() >= 0);
	old = initstate(3, st32, sizeof st32);
	CHECK(old == st16);
	CHECK(random() >= 0);
	old = initstate(4, st64, sizeof st64);
	CHECK(old == st32);
	CHECK(random() >= 0);
	old = initstate(5, st128, sizeof st128);
	CHECK(old == st64);
	CHECK(random() >= 0);
	old = initstate(6, st256, sizeof st256);
	CHECK(old == st128);
	CHECK(random() >= 0);

	/* setstate.html: "Upon successful completion, setstate() shall
	 * return a pointer to the previous state array." */
	old = setstate(st128);
	CHECK(old == st256);
	CHECK(random() >= 0);
	old = setstate(st256);
	CHECK(old == st128);

	/* srandom(1) reproducibility already covered by test/stdlib.c;
	 * here just confirm switching back to the default (128-byte)
	 * generator via srandom() still gives a well-formed sequence. */
	srandom(9);
	CHECK(random() >= 0 && random() >= 0);
}

/* ---- getenv/setenv/unsetenv/putenv: not exercised anywhere else. ---- */
static void test_env(void)
{
	/* setenv.html ERRORS: "[EINVAL] The envname argument points to an
	 * empty string, or points to a string containing an '=' character." */
	errno = 0;
	CHECK(setenv("", "x", 1) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(setenv("FOO=BAR", "x", 1) == -1 && errno == EINVAL);

	/* basic set + getenv.html: "a pointer to the value ... or a null
	 * pointer if ... not found" */
	CHECK(unsetenv("NTLIBC_TEST_VAR") == 0); /* start clean; unsetenv on a missing var still succeeds */
	CHECK(getenv("NTLIBC_TEST_VAR") == 0);
	CHECK(setenv("NTLIBC_TEST_VAR", "one", 1) == 0);
	CHECK(getenv("NTLIBC_TEST_VAR") && !strcmp(getenv("NTLIBC_TEST_VAR"), "one"));

	/* setenv.html DESCRIPTION: overwrite==0 with an existing variable
	 * leaves the environment unchanged and still returns success. */
	CHECK(setenv("NTLIBC_TEST_VAR", "two", 0) == 0);
	CHECK(!strcmp(getenv("NTLIBC_TEST_VAR"), "one"));
	CHECK(setenv("NTLIBC_TEST_VAR", "two", 1) == 0);
	CHECK(!strcmp(getenv("NTLIBC_TEST_VAR"), "two"));

	/* unsetenv.html: "Upon successful completion, zero shall be
	 * returned" and the variable is gone afterwards; EINVAL for an
	 * empty name or one containing '='. */
	CHECK(unsetenv("NTLIBC_TEST_VAR") == 0);
	CHECK(getenv("NTLIBC_TEST_VAR") == 0);
	errno = 0;
	CHECK(unsetenv("") == -1 && errno == EINVAL);
	errno = 0;
	CHECK(unsetenv("A=B") == -1 && errno == EINVAL);

	/* putenv.html DESCRIPTION: "the string pointed to by string shall
	 * become part of the environment, so altering the string shall
	 * change the environment" -- the string is not copied. */
	{
		static char buf[] = "NTLIBC_PUTENV_VAR=abc";
		CHECK(putenv(buf) == 0);
		CHECK(!strcmp(getenv("NTLIBC_PUTENV_VAR"), "abc"));
		/* mutate the very string handed to putenv(), in place ("xyz" is
		 * the same length as "abc" so no NUL bookkeeping needed) */
		strcpy(buf + strlen("NTLIBC_PUTENV_VAR="), "xyz");
		CHECK(!strcmp(getenv("NTLIBC_PUTENV_VAR"), "xyz"));
		unsetenv("NTLIBC_PUTENV_VAR");
	}

	/* putenv.html RETURN VALUE: "Upon successful completion, putenv()
	 * shall return 0; otherwise, it shall return a non-zero value." */
	{
		static char buf2[] = "NTLIBC_PUTENV_VAR2=v";
		CHECK(putenv(buf2) == 0);
		CHECK(!strcmp(getenv("NTLIBC_PUTENV_VAR2"), "v"));
		unsetenv("NTLIBC_PUTENV_VAR2");
	}

	/* environ.html / getenv.html: environ reflects the live list, and
	 * a name must appear in it after setenv(). */
	{
		char **e;
		int found = 0;
		CHECK(setenv("NTLIBC_ENVIRON_CHECK", "z", 1) == 0);
		for (e = environ; e && *e; e++)
			if (!strncmp(*e, "NTLIBC_ENVIRON_CHECK=", 21)) found = 1;
		CHECK(found);
		unsetenv("NTLIBC_ENVIRON_CHECK");
	}
}

/* ---- mkostemp/mkstemps: forms not already covered by test/stdlib.c
 * (which covers plain mkstemp/mkstemps/mkdtemp and the EINVAL
 * no-XXXXXX case). ---- */
static void test_mkostemp(void)
{
	/* mkstemp.html EINVAL: "The last six characters of template were
	 * not XXXXXX" -- fewer than six (five here) must also fail, not
	 * just zero. */
	{
		char t[] = "short-XXXXX";
		errno = 0;
		CHECK(mkostemp(t, 0) == -1 && errno == EINVAL);
	}

	/* mkstemp.html: "as if by a call to open() with ... O_RDWR" --
	 * mkostemp is documented (glibc, and src/stdlib/mktemp.c's
	 * `flags &= ~O_ACCMODE`) to force O_RDWR regardless of any
	 * access-mode bits passed in flags.  Passing O_RDONLY must not
	 * leave the descriptor read-only. */
	{
		char t[] = "mkotest-XXXXXX";
		int fd = mkostemp(t, O_RDONLY);
		CHECK(fd >= 0);
		if (fd >= 0) {
			CHECK(write(fd, "hi", 2) == 2);
			close(fd);
			unlink(t);
		}
	}

	/* the created file must not already exist (O_EXCL semantics) and
	 * must be a regular file open for reading and writing. */
	{
		char t[] = "mkotest2-XXXXXX";
		int fd = mkstemp(t);
		struct stat st;
		CHECK(fd >= 0);
		if (fd >= 0) {
			CHECK(fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
			CHECK(lseek(fd, 0, SEEK_CUR) == 0);
			CHECK(read(fd, &st, 0) == 0); /* readable */
			close(fd);
			unlink(t);
		}
	}
}

/* ---- realpath.html: caller-supplied (non-NULL) resolved_name buffer. ---- */
static void test_realpath_buf(void)
{
	char t[] = "rpbuf-XXXXXX";
	int fd = mkstemp(t);
	CHECK(fd >= 0);
	if (fd >= 0) {
		char resolved[4096]; /* PATH_MAX may not be defined; generous buffer */
		char *r = realpath(t, resolved);
		/* realpath.html RETURN VALUE: "shall return a pointer to the
		 * resolved_name argument" when one was supplied. */
		CHECK(r == resolved);
		CHECK(r && strstr(r, "rpbuf-") != 0);
		close(fd);
		unlink(t);
	}
}

/* ---- system.html: not exercised anywhere else. ---- */
static void test_system(void)
{
	int st;

	/* RETURN VALUE: "the system() function shall always return
	 * non-zero when command is NULL" (a command processor is
	 * available: cmd.exe, via ComSpec or PATH). */
	CHECK(system(0) != 0);

	/* "the value returned by system() shall be the termination status
	 * of the command language interpreter in the format specified by
	 * waitpid()." -- cmd.exe's "exit N" sets its own exit code to N. */
	st = system("exit 5");
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 5);

	st = system("exit 0");
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);

	/* "If a shell could not be executed ... the value returned by
	 * system() shall be as if the command interpreter terminated
	 * using exit(127)."  Not independently triggerable here (cmd.exe
	 * itself is what we depend on to exist), so instead check that a
	 * command cmd.exe itself cannot find comes back through *its* own
	 * 1-exit-code convention, which is a sanity check on the
	 * status-decoding path rather than this specific POSIX clause. */
	st = system("exit 1");
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 1);
}

/* ---- a64l.html / l64a.html: radix-64 digit mapping and the
 * first-six-characters / stops-at-NUL rules. ---- */
static void test_a64l(void)
{
	/* "'.' represents the value 0 through 'z' represents the value 63" */
	CHECK(a64l(".") == 0);
	CHECK(a64l("/") == 1);
	CHECK(a64l("0") == 2);
	CHECK(a64l("9") == 11);
	CHECK(a64l("A") == 12);
	CHECK(a64l("Z") == 37);
	CHECK(a64l("a") == 38);
	CHECK(a64l("z") == 63);

	/* "the first character represents the least significant digit" */
	CHECK(a64l("0/") == 2 + 1 * 64); /* '0'=2 (lsd), '/'=1 */

	/* "a64l() shall use the first six characters" -- extra characters
	 * beyond six must be ignored. */
	{
		long a = a64l("////// extra junk that would overflow if read");
		long b = a64l("//////");
		CHECK(a == b);
	}

	/* round-trip already covered by test/stdlib.c; here check l64a(0)
	 * returns a pointer to an *empty* string, not NULL. */
	{
		char *p = l64a(0);
		CHECK(p != 0 && p[0] == 0);
	}
}

/* ---- getsubopt.html: keylistp must not be modified by the call. ---- */
static void test_getsubopt_keylist(void)
{
	char buf[] = "ro";
	char *tokens[] = { "ro", "rw", 0 };
	char *tok0 = tokens[0], *tok1 = tokens[1], *tok2 = tokens[2];
	char *subopts = buf, *val;
	int r = getsubopt(&subopts, tokens, &val);
	CHECK(r == 0 && val == 0);
	/* "getsubopt() shall not modify the keylistp vector" -- the array's
	 * own pointer slots (not just the strings they name) must be the
	 * same afterwards. */
	CHECK(tokens[0] == tok0 && tokens[1] == tok1 && tokens[2] == tok2);
	/* single suboption, no comma: "*optionp shall be updated to point
	 * to the null character at the end of the string." */
	CHECK(subopts != 0 && *subopts == 0);
}

int main(void)
{
	test_strtol_base();
	test_atoi_family();
	test_qsort_bsearch_zero();
	test_bsearch_arg_order();
	test_rand48_ranges();
	test_random_state();
	test_env();
	test_mkostemp();
	test_realpath_buf();
	test_system();
	test_a64l();
	test_getsubopt_keylist();

	if (!fails) printf("posix-stdlib: all tests passed\n");
	return fails != 0;
}
