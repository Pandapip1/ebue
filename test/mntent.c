/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <mntent.h>: not POSIX (glibc/BSD historical) -- see include/mntent.h's
 * own banner. Checked against the Linux man-pages mntent(3)/getmntent(3)
 * description, the same source src/misc/mntent.c's own header cites: a
 * struct mntent line is whitespace-separated "fsname dir type opts freq
 * passno", freq/passno optional and defaulting to 0, blank or '#'-led
 * lines skipped as comments.
 *
 * Every function here is pure stdio over a FILE * the caller already
 * has open (src/misc/mntent.c's own header comment), so this file
 * mostly drives them through a real temporary file via
 * setmntent()/addmntent()/endmntent() -- never MOUNTED itself, which
 * names a real Linux pseudo-file (/proc/mounts) on one platform and a
 * deliberate, clean ENOENT on the other (see include/mntent.h) -- and
 * once via fmemopen() for the parser edge cases that do not need a real
 * file at all.
 */
#define _GNU_SOURCE
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* setmntent.html-equivalent (mntent(3)): open a real file for writing,
 * addmntent() two entries, close, reopen for reading, getmntent() both
 * back in order, then confirm end-of-file. Exercises setmntent(),
 * addmntent(), endmntent() and getmntent() together, the whole
 * round-trip a real /etc/mtab writer/reader pair would make. */
static void test_roundtrip(const char *path)
{
	FILE *f;
	struct mntent m1, m2, *got;

	f = setmntent(path, "w");
	CHECK(f != NULL);
	if (!f) return;

	m1.mnt_fsname = (char *)"/dev/sda1";
	m1.mnt_dir    = (char *)"/";
	m1.mnt_type   = (char *)"ext4";
	m1.mnt_opts   = (char *)"rw,relatime";
	m1.mnt_freq   = 0;
	m1.mnt_passno = 1;
	/* mntent(3): "addmntent() returns 0 on success". */
	CHECK(addmntent(f, &m1) == 0);

	m2.mnt_fsname = (char *)"tmpfs";
	m2.mnt_dir    = (char *)"/tmp";
	m2.mnt_type   = (char *)"tmpfs";
	m2.mnt_opts   = (char *)"defaults";
	m2.mnt_freq   = 0;
	m2.mnt_passno = 0;
	CHECK(addmntent(f, &m2) == 0);

	/* mntent(3): "endmntent() always returns 1." */
	CHECK(endmntent(f) == 1);

	f = setmntent(path, "r");
	CHECK(f != NULL);
	if (!f) return;

	got = getmntent(f);
	CHECK(got != NULL);
	if (got) {
		CHECK(strcmp(got->mnt_fsname, "/dev/sda1") == 0);
		CHECK(strcmp(got->mnt_dir, "/") == 0);
		CHECK(strcmp(got->mnt_type, "ext4") == 0);
		CHECK(strcmp(got->mnt_opts, "rw,relatime") == 0);
		CHECK(got->mnt_freq == 0);
		CHECK(got->mnt_passno == 1);
	}

	got = getmntent(f);
	CHECK(got != NULL);
	if (got) {
		CHECK(strcmp(got->mnt_fsname, "tmpfs") == 0);
		CHECK(strcmp(got->mnt_dir, "/tmp") == 0);
		CHECK(got->mnt_freq == 0 && got->mnt_passno == 0);
	}

	/* No third entry: getmntent() at real end-of-file returns NULL. */
	errno = 0;
	CHECK(getmntent(f) == NULL);

	CHECK(endmntent(f) == 1);
}

/* getmntent_r() directly, over an in-memory stream (no real file
 * needed for pure parsing) -- comment/blank-line skipping, the
 * freq/passno-omitted default-to-0 case, and the too-few-fields
 * rejection, all cited from src/misc/mntent.c's own header comment
 * (the closest thing to a spec this historical interface has). */
static void test_getmntent_r_parsing(void)
{
	static const char data[] =
		"# a comment line, skipped\n"
		"\n"
		"   # indented comment, also skipped\n"
		"/dev/sdb1 /mnt ext3 ro\n"
		"proc /proc proc defaults 0 0\n";
	FILE *f;
	struct mntent mnt, *r;
	char buf[256];

	f = fmemopen((void *)data, sizeof data - 1, "r");
	CHECK(f != NULL);
	if (!f) return;

	/* First real line: only the four mandatory fields present --
	 * freq/passno must default to 0, not be left uninitialised. */
	r = getmntent_r(f, &mnt, buf, sizeof buf);
	CHECK(r == &mnt);
	if (r) {
		CHECK(strcmp(mnt.mnt_fsname, "/dev/sdb1") == 0);
		CHECK(strcmp(mnt.mnt_dir, "/mnt") == 0);
		CHECK(strcmp(mnt.mnt_type, "ext3") == 0);
		CHECK(strcmp(mnt.mnt_opts, "ro") == 0);
		CHECK(mnt.mnt_freq == 0);
		CHECK(mnt.mnt_passno == 0);
	}

	r = getmntent_r(f, &mnt, buf, sizeof buf);
	CHECK(r == &mnt);
	if (r) {
		CHECK(strcmp(mnt.mnt_fsname, "proc") == 0);
		CHECK(mnt.mnt_freq == 0 && mnt.mnt_passno == 0);
	}

	CHECK(getmntent_r(f, &mnt, buf, sizeof buf) == NULL);
	fclose(f);

	/* Fewer than the four mandatory fields: rejected outright, not
	 * padded with empty strings. */
	{
		static const char shortline[] = "/dev/sdc1 /mnt2\n";
		f = fmemopen((void *)shortline, sizeof shortline - 1, "r");
		CHECK(f != NULL);
		if (f) {
			CHECK(getmntent_r(f, &mnt, buf, sizeof buf) == NULL);
			fclose(f);
		}
	}

	/* NULL/degenerate arguments: documented to return NULL rather than
	 * crash (src/misc/mntent.c's own guard, `if (!f || !mnt || !buf ||
	 * buflen <= 0) return 0;`). */
	CHECK(getmntent_r(NULL, &mnt, buf, sizeof buf) == NULL);
	f = fmemopen((void *)data, sizeof data - 1, "r");
	if (f) {
		CHECK(getmntent_r(f, &mnt, buf, 0) == NULL);
		fclose(f);
	}
}

/* hasmntopt.html-equivalent (mntent(3)): "hasmntopt() scans the
 * mnt_opts field ... for a substring that matches opt", returning a
 * pointer into mnt_opts on a match (not merely nonzero) and NULL on
 * none.  Comma-delimited, and a prefix of one option must not match a
 * different, longer option that merely starts with the same letters
 * (e.g. "ro" must not match inside "rootcontext"). */
static void test_hasmntopt(void)
{
	struct mntent m;
	char *r;

	m.mnt_opts = (char *)"ro,noauto,defaults";

	r = hasmntopt(&m, "noauto");
	CHECK(r != NULL);
	if (r) CHECK(strncmp(r, "noauto", 6) == 0);

	r = hasmntopt(&m, "ro");
	CHECK(r == m.mnt_opts); /* the first option, at the very start */

	CHECK(hasmntopt(&m, "rw") == NULL);
	CHECK(hasmntopt(&m, "auto") == NULL); /* substring of "noauto", not an option itself */

	/* An empty options field: "" is a legitimate (if degenerate) match
	 * for opt=="" per src/misc/mntent.c's own header comment ("True for
	 * "" too"), and no real option ever matches against it. */
	m.mnt_opts = (char *)"";
	CHECK(hasmntopt(&m, "") != NULL);
	CHECK(hasmntopt(&m, "ro") == NULL);
}

/* mntent(3): "If the mount table is not found NULL is returned." --
 * checked against a path that certainly does not exist, mirroring
 * every other "not found" probe in this tree (test/pwd.c,
 * test/posix-grp.c) rather than assuming any particular errno beyond
 * what fopen() itself already documents. */
static void test_setmntent_missing_file(void)
{
	FILE *f = setmntent("definitely-not-a-real-mtab-xyz", "r");
	CHECK(f == NULL);
}

int main(void)
{
	char tmpl[] = "mntenttest-XXXXXX";
	int fd = mkstemp(tmpl);

	CHECK(fd >= 0);
	if (fd >= 0) close(fd); /* setmntent() reopens by path */

	test_roundtrip(tmpl);
	test_getmntent_r_parsing();
	test_hasmntopt();
	test_setmntent_missing_file();

	if (fd >= 0) unlink(tmpl);

	if (!fails) printf("mntent: all tests passed\n");
	return fails != 0;
}
