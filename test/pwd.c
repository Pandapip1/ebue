/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <pwd.h>
 * (pwd.h.html, getpwnam.html, getpwuid.html, getpwent.html) against
 * src/misc/pwd.c.  See that file's header comment for the design:
 * ntlibc has exactly one uid (getuid()/geteuid() always agree, per
 * src/unistd/ids.c and test/posix-unistd.c's own coverage of that),
 * and this file checks that getpwuid()/getpwnam() answer honestly for
 * that one user and refuse cleanly for anyone else.
 *
 * Every test below is written against `have_user`, not against "does
 * Wine set %USERNAME%": src/misc/pwd.c's whole design is "refuse
 * cleanly rather than fabricate" when the current user's name is not
 * in the environment, and this file's own native (non-Wine) `make
 * asan` run is a real instance of exactly that -- fuzz/ntstubs.c's
 * process shim deliberately starts environ empty (see its own
 * comment, "the test harness's own, arbitrary environment" -- it is
 * kept out on purpose so native tests are deterministic), so
 * %USERNAME%/%USER% are genuinely unset there.  Both branches are
 * therefore exercised for real by this one test binary depending on
 * which harness runs it, rather than one of them being dead code.
 */
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Mirrors src/misc/pwd.c's current_name(): whether this process's
 * environment lets the library know who "the current user" is at
 * all. */
static int have_user(void)
{
	const char *n = getenv("USERNAME");
	if (!n || !*n) n = getenv("USER");
	return n && *n;
}

/* pwd.h.html: "at least" pw_name/pw_uid/pw_gid/pw_dir/pw_shell, and
 * getpwuid.html DESCRIPTION: search by uid.  getuid()==geteuid()
 * always here (test/posix-unistd.c), so getuid() is the only uid that
 * can ever have an entry -- when the name behind it is knowable at
 * all (see have_user() above). */
static void test_getpwuid_current(void)
{
	struct passwd *pw;

	errno = 0;
	pw = getpwuid(getuid());
	if (!have_user()) {
		/* getpwuid.html RETURN VALUE: "If the requested entry was
		 * not found, errno shall not be changed." -- the honest
		 * answer when the name is unknowable is "not found", same
		 * as for any other uid. */
		CHECK(pw == NULL);
		CHECK(errno == 0);
		return;
	}
	CHECK(pw != NULL);
	if (!pw) return;
	CHECK(pw->pw_uid == getuid());
	CHECK(pw->pw_gid == getgid());
	CHECK(pw->pw_name != NULL && pw->pw_name[0] != '\0');
	CHECK(pw->pw_dir != NULL && pw->pw_dir[0] != '\0');
	CHECK(pw->pw_shell != NULL && pw->pw_shell[0] != '\0');
}

/* getpwuid.html RETURN VALUE: "If the requested entry was not found,
 * errno shall not be changed." ntlibc has exactly one uid, so any
 * other value is "not found" by definition, regardless of
 * have_user(). */
static void test_getpwuid_other_not_found(void)
{
	struct passwd *pw;

	errno = 12345;
	pw = getpwuid(getuid() + 1);
	CHECK(pw == NULL);
	CHECK(errno == 12345);
}

/* getpwnam.html DESCRIPTION: search by name.  Round-trip:
 * getpwnam(getpwuid(getuid())->pw_name) must be the same record --
 * both are sourced from src/misc/pwd.c's current_name(), so a mismatch
 * here would be a real bug, not just a design gap. */
static void test_getpwnam_current_and_roundtrip(void)
{
	struct passwd *by_uid, *by_name;
	char namebuf[256];

	by_uid = getpwuid(getuid());
	if (!have_user()) {
		CHECK(by_uid == NULL);
		/* nothing to round-trip without a name */
		return;
	}
	CHECK(by_uid != NULL);
	if (!by_uid) return;
	/* g_pw is static storage, reused by the next getpwnam() call --
	 * snapshot the name first. */
	strcpy(namebuf, by_uid->pw_name);

	errno = 0;
	by_name = getpwnam(namebuf);
	CHECK(by_name != NULL);
	CHECK(errno == 0);
	if (!by_name) return;
	CHECK(strcmp(by_name->pw_name, namebuf) == 0);
	CHECK(by_name->pw_uid == getuid());
	CHECK(by_name->pw_gid == getgid());
}

static void test_getpwnam_other_not_found(void)
{
	struct passwd *pw;

	errno = 12345;
	pw = getpwnam("definitely-not-a-real-ntlibc-user-xyz");
	CHECK(pw == NULL);
	CHECK(errno == 12345);
}

/* getpwuid_r/getpwnam_r (Thread-Safe Functions option): "shall return
 * zero" on success or clean not-found; error number returned directly,
 * not via errno; *result set to NULL on both error and not-found. */
static void test_getpwuid_r_success(void)
{
	struct passwd pw, *result = (struct passwd *)0x1;
	char buf[512];
	int r;

	r = getpwuid_r(getuid(), &pw, buf, sizeof buf, &result);
	CHECK(r == 0);
	if (!have_user()) {
		CHECK(result == NULL);
		return;
	}
	CHECK(result == &pw);
	CHECK(pw.pw_uid == getuid());
	CHECK(pw.pw_gid == getgid());
	CHECK(pw.pw_name != NULL && pw.pw_name[0] != '\0');
	CHECK(pw.pw_dir != NULL && pw.pw_dir[0] != '\0');
	CHECK(pw.pw_shell != NULL && pw.pw_shell[0] != '\0');
}

static void test_getpwuid_r_not_found(void)
{
	struct passwd pw, *result = (struct passwd *)0x1;
	char buf[512];
	int r;

	r = getpwuid_r(getuid() + 1, &pw, buf, sizeof buf, &result);
	CHECK(r == 0);
	CHECK(result == NULL);
}

/* ERANGE: "Insufficient storage was supplied via buffer and bufsize."
 * Only observable when there is a record to try to pack in the first
 * place -- without have_user(), getpwuid_r() reports "not found"
 * before it ever looks at bufsize, exactly like getpwuid_r_not_found
 * above. */
static void test_getpwuid_r_erange(void)
{
	struct passwd pw, *result = (struct passwd *)0x1;
	char buf[1];
	int r;

	r = getpwuid_r(getuid(), &pw, buf, sizeof buf, &result);
	if (!have_user()) {
		CHECK(r == 0);
		CHECK(result == NULL);
		return;
	}
	CHECK(r == ERANGE);
	CHECK(result == NULL);
}

static void test_getpwnam_r_success_and_not_found(void)
{
	struct passwd pw, *result;
	char buf[512];
	char namebuf[256];
	int r;

	if (!have_user()) {
		result = (struct passwd *)0x1;
		r = getpwnam_r("whoever", &pw, buf, sizeof buf, &result);
		CHECK(r == 0);
		CHECK(result == NULL);
		return;
	}

	result = (struct passwd *)0x1;
	r = getpwuid_r(getuid(), &pw, buf, sizeof buf, &result);
	CHECK(r == 0 && result == &pw);
	if (r != 0 || !result) return;
	strcpy(namebuf, pw.pw_name);

	result = (struct passwd *)0x1;
	r = getpwnam_r(namebuf, &pw, buf, sizeof buf, &result);
	CHECK(r == 0);
	CHECK(result == &pw);
	CHECK(pw.pw_uid == getuid());

	result = (struct passwd *)0x1;
	r = getpwnam_r("definitely-not-a-real-ntlibc-user-xyz", &pw, buf, sizeof buf, &result);
	CHECK(r == 0);
	CHECK(result == NULL);
}

static void test_getpwnam_r_erange(void)
{
	struct passwd pw, *result = (struct passwd *)0x1;
	char buf[1];
	char namebuf[256];
	struct passwd *cur;

	if (!have_user()) {
		result = (struct passwd *)0x1;
		CHECK(getpwnam_r("whoever", &pw, buf, sizeof buf, &result) == 0);
		CHECK(result == NULL);
		return;
	}

	cur = getpwuid(getuid());
	CHECK(cur != NULL);
	if (!cur) return;
	strcpy(namebuf, cur->pw_name);

	result = (struct passwd *)0x1;
	CHECK(getpwnam_r(namebuf, &pw, buf, sizeof buf, &result) == ERANGE);
	CHECK(result == NULL);
}

/* getpwent.html: XSI, but implementable here -- ntlibc's "database"
 * genuinely has one entry when the current user's name is knowable,
 * and none at all otherwise.  setpwent() rewinds; the first
 * getpwent() yields the one entry (or immediate end-of-file without
 * have_user()); the next call always hits end-of-file (NULL, errno
 * unchanged); setpwent() rewinds again. */
static void test_getpwent_one_entry_then_eof(void)
{
	struct passwd *pw;

	setpwent();
	errno = 0;
	pw = getpwent();
	if (have_user()) {
		CHECK(pw != NULL);
		if (pw) CHECK(pw->pw_uid == getuid());
	} else {
		CHECK(pw == NULL);
		CHECK(errno == 0);
	}

	errno = 0;
	pw = getpwent();
	CHECK(pw == NULL);
	CHECK(errno == 0);

	setpwent();
	pw = getpwent();
	CHECK((pw != NULL) == have_user());
	endpwent();
}

/* The motivating case (task brief): "~" (current user, no name after
 * the tilde) expansion, the way glob(3)/gnulib's glob.c resolves it --
 * look up the current login name via getpwnam() and use pw_dir as the
 * expansion, then confirm that directory is real and usable, not just
 * a non-NULL string.  Only meaningful when have_user() -- without a
 * name there is nothing glob.c could look up either, and it declines
 * the same way getpwnam() does here (NULL, not a fabricated guess). */
static void test_tilde_expansion_current_user(void)
{
	struct passwd *me, *pw;
	struct stat st;

	me = getpwuid(getuid());
	if (!have_user()) {
		CHECK(me == NULL);
		printf("note: %%USERNAME%%/%%USER%% unset in this harness -- '~' is correctly left unexpandable\n");
		return;
	}
	CHECK(me != NULL);
	if (!me) return;
	pw = getpwnam(me->pw_name);
	CHECK(pw != NULL);
	if (!pw) return;
	CHECK(pw->pw_dir != NULL && pw->pw_dir[0] != '\0');
	CHECK(stat(pw->pw_dir, &st) == 0);
	CHECK(S_ISDIR(st.st_mode));
}

int main(void)
{
	printf("note: have_user() = %s\n", have_user() ? "true" : "false");

	test_getpwuid_current();
	test_getpwuid_other_not_found();
	test_getpwnam_current_and_roundtrip();
	test_getpwnam_other_not_found();
	test_getpwuid_r_success();
	test_getpwuid_r_not_found();
	test_getpwuid_r_erange();
	test_getpwnam_r_success_and_not_found();
	test_getpwnam_r_erange();
	test_getpwent_one_entry_then_eof();
	test_tilde_expansion_current_user();

	if (!fails) printf("pwd: all tests passed\n");
	return fails != 0;
}
