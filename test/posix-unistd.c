/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <unistd.h>/<fcntl.h>/<sys/stat.h>
 * requirements not already exercised by test/unistd.c's ~330-assertion
 * sanity pass or test/posix-io.c's errno pass.  Each block cites the page
 * it was checked against under
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 * Genuine spec violations are fenced with #if 0 / BUG: rather than
 * weakened -- see test/posix-coverage/unistd.md for the roundup.
 *
 * Everything happens in a fresh mkdtemp directory, removed at the end.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <utime.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* open.html DESCRIPTION: "a bitwise AND is performed on the file-mode
 * bits and the corresponding bits in the complement of the process' file
 * mode creation mask" -- i.e. mode is ANDed with ~umask before it takes
 * effect.  Fixed: src/fcntl/open.c's __open_handle() now ANDs mode with
 * ~__umask_get() (src/stat/chmod.c) before deciding
 * FILE_ATTRIBUTE_READONLY -- the only mode bit NTFS gives any meaning
 * to here, so it is the only bit umask can be observed to affect. */
static void test_open_umask_bug(void)
{
	mode_t old;
	int fd;
	struct stat st;

	old = umask(0222);		/* clear all write bits */
	fd = open("um.txt", O_CREAT | O_WRONLY | O_TRUNC, 0666);
	CHECK(fd >= 0);
	close(fd);
	CHECK(stat("um.txt", &st) == 0);
	CHECK(!(st.st_mode & 0222));
	umask(old);
	/* The file just created is read-only; Wine's server refuses to
	 * unlink a read-only file (real NT does not -- see test/unistd.c's
	 * "a file created read-only via the mode argument" case for the
	 * same quirk), so clear the attribute first when that happens.
	 * Plain chmod() back from read-only used to fail the same way
	 * here (Wine denies a bare FILE_WRITE_ATTRIBUTES open once
	 * FILE_ATTRIBUTE_READONLY is set); src/stat/chmod.c's fchmodat()
	 * now retries with a read-attributes-only handle when that
	 * happens (see test_chmod_owner_can_always_chmod_readonly()
	 * above), so the chmod() branch below is dead on a fixed build
	 * and kept only as a defensive fallback / regression net. */
	if (unlink("um.txt") == -1) {
		CHECK(errno == EACCES);
		if (chmod("um.txt", 0644) == -1) {
			CHECK(errno == EACCES);
			fd = open("um.txt", O_RDONLY);
			CHECK(fd >= 0);
			CHECK(fchmod(fd, 0644) == 0);
			CHECK(close(fd) == 0);
		}
		CHECK(unlink("um.txt") == 0);
	}
}

/* chmod.html DESCRIPTION: "The application shall ensure that the
 * effective user ID of the process matches the owner of the file or
 * the process has appropriate privileges" in order to change the
 * file's permission bits -- nothing there conditions permission to
 * chmod() on the file's *own* current mode.  A file with no
 * owner-write bit (0444) must still be chmod()-able by its owner.
 * BUG (fixed): src/fcntl/open.c sets FILE_ATTRIBUTE_READONLY for a
 * no-write-bits mode; src/stat/chmod.c's fchmodat() used to open every
 * target with FILE_WRITE_ATTRIBUTES unconditionally, and Wine's
 * server denies that open outright once FILE_ATTRIBUTE_READONLY is
 * already set (real NT does not -- see test_open_umask_bug() above),
 * so chmod() on a 0444 file was permanently EACCES.  This is what
 * broke GNU tar extraction downstream: tar creates most files 0444,
 * then chmod()s/utimensat()s them. */
static void test_chmod_owner_can_always_chmod_readonly(void)
{
	struct stat st;
	int fd = open("t-chmod-ro.txt", O_CREAT | O_WRONLY | O_TRUNC, 0444);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(stat("t-chmod-ro.txt", &st) == 0 && (st.st_mode & 0777) == 0444);

	errno = 0;
	CHECK(chmod("t-chmod-ro.txt", 0644) == 0);	/* would fail: EACCES before the fix */
	CHECK(stat("t-chmod-ro.txt", &st) == 0 && (st.st_mode & 0777) == 0644);

	/* round trip back down to 0444 and confirm it sticks */
	CHECK(chmod("t-chmod-ro.txt", 0444) == 0);
	CHECK(stat("t-chmod-ro.txt", &st) == 0 && (st.st_mode & 0777) == 0444);

	/* unlink() of a read-only file is Wine's other unfixed
	 * WA-open-denied quirk (test_open_umask_bug()'s comment above);
	 * clear the attribute first so cleanup does not itself fail. */
	CHECK(chmod("t-chmod-ro.txt", 0644) == 0);
	unlink("t-chmod-ro.txt");
}

/* utime.html DESCRIPTION: with a non-null times argument "a process
 * must have write permission for the file, or be the owner of the
 * file, or be a process with appropriate privileges"; with a null
 * times argument, "the effective user ID of the process shall match
 * the owner of the file, or the process has write permission to the
 * file or has appropriate privileges".  Neither clause conditions
 * permission to utime()/utimensat() on the file's own *current* mode
 * bits.  Same BUG (fixed) as chmod above: src/stat/utimensat.c's
 * utimensat() opened with FILE_WRITE_ATTRIBUTES unconditionally, so a
 * 0444 file's own read-only attribute made every utimensat()/utime()
 * call on it EACCES under Wine -- the other half of what broke tar. */
static void test_utimensat_owner_can_touch_readonly(void)
{
	struct stat st;
	struct timespec ts[2];
	int fd = open("t-utime-ro.txt", O_CREAT | O_WRONLY | O_TRUNC, 0444);
	CHECK(fd >= 0 && close(fd) == 0);

	ts[0].tv_sec = ts[1].tv_sec = 1000000;
	ts[0].tv_nsec = ts[1].tv_nsec = 0;
	errno = 0;
	CHECK(utimensat(AT_FDCWD, "t-utime-ro.txt", ts, 0) == 0);	/* would fail: EACCES before the fix */
	CHECK(stat("t-utime-ro.txt", &st) == 0);
	CHECK(st.st_mtim.tv_sec == 1000000);

	/* utime.html: only the times change -- mode must survive
	 * untouched.  BUG (fixed): FILE_BASIC_INFORMATION's
	 * FileAttributes==0 is documented as "leave unchanged", but
	 * Wine's NtSetInformationFile was observed clearing
	 * FILE_ATTRIBUTE_READONLY on every timestamp-only call, silently
	 * turning the file writable even though the call "succeeded". */
	CHECK((st.st_mode & 0777) == 0444);	/* would fail: mode clobbered to 0644 before the fix */

	CHECK(utime("t-utime-ro.txt", 0) == 0);
	CHECK(stat("t-utime-ro.txt", &st) == 0 && (st.st_mode & 0777) == 0444);

	/* see test_chmod_owner_can_always_chmod_readonly()'s cleanup comment */
	CHECK(chmod("t-utime-ro.txt", 0644) == 0);
	unlink("t-utime-ro.txt");
}

/* Combined round trip + failure-path check: FILE_ATTRIBUTE_READONLY
 * must be left exactly as the last successful chmod()/utimensat()
 * call set it -- neither call may corrupt it, on the success path or
 * on an unrelated induced-failure path (chmod()/utimensat() of a
 * nonexistent name, which must fail before ever touching the real
 * file's handle). */
static void test_chmod_utimensat_attr_state_after_failure(void)
{
	struct stat st;
	struct timespec ts[2];
	int fd = open("t-attr-state.txt", O_CREAT | O_WRONLY | O_TRUNC, 0444);
	CHECK(fd >= 0 && close(fd) == 0);

	CHECK(chmod("t-attr-state.txt", 0444) == 0);	/* no-op set; still exercises the WA-denied-open path */
	CHECK(stat("t-attr-state.txt", &st) == 0 && (st.st_mode & 0777) == 0444);

	ts[0].tv_sec = ts[1].tv_sec = 2000000;
	ts[0].tv_nsec = ts[1].tv_nsec = 0;
	CHECK(utimensat(AT_FDCWD, "t-attr-state.txt", ts, 0) == 0);
	CHECK(stat("t-attr-state.txt", &st) == 0 && (st.st_mode & 0777) == 0444);

	/* induced failure: a nonexistent path must fail before opening
	 * the real file at all, and must not leave it half-changed. */
	errno = 0;
	CHECK(chmod("t-attr-state-nonexistent.txt", 0644) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(utimensat(AT_FDCWD, "t-attr-state-nonexistent.txt", ts, 0) == -1 && errno == ENOENT);
	CHECK(stat("t-attr-state.txt", &st) == 0 && (st.st_mode & 0777) == 0444);	/* untouched */

	/* see test_chmod_owner_can_always_chmod_readonly()'s cleanup comment */
	CHECK(chmod("t-attr-state.txt", 0644) == 0);
	unlink("t-attr-state.txt");
}

/* dup.html DESCRIPTION (dup2()): "If fildes is equal to fildes2, ...
 * dup2() shall return fildes2 without closing it" and "If fildes is
 * equal to fildes2, the FD_CLOEXEC flag associated with fildes2 shall
 * not be changed."  test/unistd.c already checks the return value; this
 * checks the FD_CLOEXEC-preserved half. */
static void test_dup2_self_preserves_cloexec(void)
{
	int fd = open("t-d2self.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(dup2(fd, fd) == fd);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);	/* unchanged, not cleared */
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(close(fd) == 0);
	unlink("t-d2self.txt");
}

/* fcntl.html F_DUPFD DESCRIPTION: "the lowest numbered available file
 * descriptor greater than or equal to the third argument".  Free up a
 * slot inside the requested range and check it -- not some higher
 * number -- is what comes back. */
static void test_fcntl_dupfd_lowest(void)
{
	int fd, a, b, c;

	fd = open("t-fd-low.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);
	a = fcntl(fd, F_DUPFD, 70);
	b = fcntl(fd, F_DUPFD, 70);
	CHECK(a >= 70 && b > a);
	close(a);	/* free the lowest of the two */
	c = fcntl(fd, F_DUPFD, 70);
	CHECK(c == a);		/* the freed slot, not a new high one */
	close(b);
	close(c);
	CHECK(close(fd) == 0);
	unlink("t-fd-low.txt");
}

/* fcntl.html F_SETFL DESCRIPTION: "Bits corresponding to the file access
 * mode and the file creation flags ... that are set in arg shall be
 * ignored."  Try to flip O_ACCMODE via F_SETFL and confirm it has no
 * effect (the descriptor is still open for the mode it was opened
 * with). */
static void test_fcntl_setfl_ignores_accmode(void)
{
	int fd = open("t-setfl.txt", O_CREAT | O_RDONLY | O_TRUNC, 0644);
	CHECK(fd >= 0);
	CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
	CHECK(fcntl(fd, F_SETFL, O_WRONLY | O_APPEND) == 0);
	CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY);	/* unchanged */
	CHECK(fcntl(fd, F_GETFL) & O_APPEND);			/* status flag did stick */
	CHECK(close(fd) == 0);
	unlink("t-setfl.txt");
}

/* fcntl.html F_GETLK DESCRIPTION: "if no lock would prevent this lock
 * from being created, ... update the l_type field with the value
 * F_UNLCK".  Advisory locks are unimplemented (src/fcntl/fcntl.c always
 * reports success); confirm F_GETLK reports "no conflicting lock" and
 * F_SETLK/F_SETLKW report success, the way a filesystem without locking
 * support would. */
static void test_fcntl_locks_are_noops(void)
{
	int fd = open("t-lock.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	struct flock fl;
	CHECK(fd >= 0);
	memset(&fl, 0, sizeof fl);
	fl.l_type = F_WRLCK; fl.l_whence = SEEK_SET; fl.l_start = 0; fl.l_len = 0;
	CHECK(fcntl(fd, F_GETLK, &fl) == 0);
	CHECK(fl.l_type == F_UNLCK);
	CHECK(fcntl(fd, F_SETLK, &fl) == 0);
	CHECK(fcntl(fd, F_SETLKW, &fl) == 0);
	CHECK(close(fd) == 0);
	unlink("t-lock.txt");
}

/* access.html ERRORS ENOTDIR: "A component of path names an existing
 * file that is neither a directory nor a symbolic link to a directory,
 * and it is not the last component of path, or ... path ends with one
 * or more trailing slashes and the last component of path is not a
 * directory." */
static void test_access_trailing_slash_enotdir(void)
{
	int fd = open("t-notdir.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	errno = 0;
	/* Fixed: src/internal/path.c's __ntpath()/__ntpath_at() now note
	 * whether a trailing slash was stripped and, if so, re-check the
	 * resolved object's type with a handle-less NtQueryAttributesFile
	 * before returning -- ENOTDIR if it exists and is not a directory,
	 * left alone otherwise (a name that does not exist yet is left to
	 * whatever real operation the caller goes on to do). */
	CHECK(access("t-notdir.txt/", F_OK) == -1 && errno == ENOTDIR);
	unlink("t-notdir.txt");
}

/* rename.html DESCRIPTION: "If the old argument and the new argument
 * resolve to either the same existing directory entry or different
 * directory entries for the same existing file, rename() shall return
 * successfully and perform no other action." */
static void test_rename_same_file_noop(void)
{
	int fd = open("t-rsame.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	struct stat before, after;
	CHECK(fd >= 0 && write(fd, "same", 4) == 4 && close(fd) == 0);
	CHECK(stat("t-rsame.txt", &before) == 0);
	CHECK(rename("t-rsame.txt", "t-rsame.txt") == 0);
	CHECK(stat("t-rsame.txt", &after) == 0);
	CHECK(before.st_ino == after.st_ino && before.st_size == after.st_size);
	unlink("t-rsame.txt");
}

/* rename.html ERRORS EISDIR: "The new argument points to a directory
 * and the old argument points to a file that is not a directory." */
static void test_rename_new_dir_old_file_eisdir(void)
{
	int fd = open("t-rfile.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(mkdir("t-rdir", 0755) == 0);
	errno = 0;
	/* Fixed: src/stdio/misc.c's renameat() now disambiguates NT's
	 * STATUS_ACCESS_DENIED by querying old and new's types -- new an
	 * existing directory and old not one maps to EISDIR here, the same
	 * way open.c already special-cases STATUS_FILE_IS_A_DIRECTORY. */
	CHECK(rename("t-rfile.txt", "t-rdir") == -1 && errno == EISDIR);
	CHECK(rmdir("t-rdir") == 0);
	unlink("t-rfile.txt");
}

/* rename.html DESCRIPTION/ERRORS ENOTEMPTY: "the new argument ... names
 * an existing directory ... required to be an empty directory";
 * ERRORS "[EEXIST] or [ENOTEMPTY] ... The link named by new is a
 * directory that is not an empty directory." */
static void test_rename_onto_nonempty_dir(void)
{
	int fd;
	CHECK(mkdir("t-rsrc", 0755) == 0);
	CHECK(mkdir("t-rdst", 0755) == 0);
	fd = open("t-rdst/x.txt", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	errno = 0;
	/* Fixed: same disambiguation as the EISDIR case above -- old and
	 * new both directories maps to ENOTEMPTY instead of EACCES. */
	CHECK(rename("t-rsrc", "t-rdst") == -1 && (errno == ENOTEMPTY || errno == EEXIST));
	unlink("t-rdst/x.txt");
	CHECK(rmdir("t-rdst") == 0);
	CHECK(rmdir("t-rsrc") == 0);
}

/* creat.html: "shall behave as if it is implemented as:
 * return open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);" -- a second creat()
 * on an already-populated file truncates it to zero, exactly like the
 * equivalent open(). */
static void test_creat_truncates_existing(void)
{
	int fd = creat("t-creat.txt", 0644);
	struct stat st;
	CHECK(fd >= 0 && write(fd, "0123456789", 10) == 10 && close(fd) == 0);
	CHECK(stat("t-creat.txt", &st) == 0 && st.st_size == 10);
	fd = creat("t-creat.txt", 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(stat("t-creat.txt", &st) == 0 && st.st_size == 0);
	unlink("t-creat.txt");
}

/* unlink.html DESCRIPTION: "If one or more processes have the file open
 * when the last link is removed, the link shall be removed before
 * unlink() returns, but the removal of the file contents shall be
 * postponed until all references to the file are closed."  Needs
 * Windows 10 1709+'s POSIX delete semantics (src/unistd/unlink.c's
 * FILE_DISPOSITION_POSIX_SEMANTICS); an older target would fall back to
 * delete-on-close, which happens to give the same observable result
 * here since the fd stays open throughout. */
static void test_unlink_open_file_stays_usable(void)
{
	int fd = open("t-ghost.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	char buf[8];
	struct stat st;
	CHECK(fd >= 0 && write(fd, "ghost", 5) == 5);
	CHECK(unlink("t-ghost.txt") == 0);
	CHECK(stat("t-ghost.txt", &st) == -1 && errno == ENOENT);	/* name gone */
	CHECK(fstat(fd, &st) == 0 && st.st_size == 5);			/* fd still good */
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(read(fd, buf, 5) == 5 && !memcmp(buf, "ghost", 5));
	CHECK(close(fd) == 0);
}

/* lseek.html ERRORS EINVAL: "the resulting file offset would be
 * negative for a regular file" -- already covered for SEEK_SET in
 * test/unistd.c; here via SEEK_CUR, whose overflow-to-negative path is
 * a different code shape. */
static void test_lseek_seek_cur_negative(void)
{
	int fd = open("t-lsc.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, "abc", 3) == 3);
	CHECK(lseek(fd, 1, SEEK_SET) == 1);
	errno = 0;
	CHECK(lseek(fd, -5, SEEK_CUR) == -1 && errno == EINVAL);
	CHECK(lseek(fd, 0, SEEK_CUR) == 1);	/* unchanged after the failed seek */
	CHECK(close(fd) == 0);
	unlink("t-lsc.txt");
}

/* getcwd.html ERRORS ERANGE: "The size argument is greater than 0, but
 * is smaller than the length of the pathname +1." Exercise the boundary
 * exactly (size == length, one short of length+1) rather than a small
 * fixed buffer, which test/unistd.c already covers. */
static void test_getcwd_off_by_one(void)
{
	char cwd[4096], boundary[4096];
	size_t len;
	CHECK(getcwd(cwd, sizeof cwd) == cwd);
	len = strlen(cwd);
	errno = 0;
	CHECK(getcwd(boundary, len) == 0 && errno == ERANGE);		/* one short */
	CHECK(getcwd(boundary, len + 1) == boundary);			/* exact fit */
	CHECK(!strcmp(boundary, cwd));
}

/* stat.html DESCRIPTION: st_dev/st_ino together identify a file
 * uniquely; test/unistd.c already checks this for two paths to the same
 * file.  Here: two distinct files must NOT collide. */
static void test_stat_ino_distinct(void)
{
	struct stat a, b;
	int fd = open("t-i1.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	fd = open("t-i2.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(stat("t-i1.txt", &a) == 0 && stat("t-i2.txt", &b) == 0);
	CHECK(!(a.st_ino == b.st_ino && a.st_dev == b.st_dev));
	unlink("t-i1.txt");
	unlink("t-i2.txt");
}

/* stat.html DESCRIPTION, again: the same st_dev/st_ino uniqueness
 * requirement, but for the src/stat/stat.c early-return path that used
 * to skip both fields entirely for pipes/consoles/char devices (leaving
 * them zeroed), which made the universal same-file idiom
 * (a.st_dev==b.st_dev && a.st_ino==b.st_ino) wrongly report every pair
 * of pipes -- and every pair of console handles -- as one file.  Two
 * distinct pipe2() calls must not collide, and S_ISFIFO must still hold
 * (mode bits were already correct; only the identity fields were the
 * bug, so this also guards against a regression there). */
static void test_stat_pipe_ino_distinct(void)
{
	int p1[2], p2[2];
	struct stat a, b;
	CHECK(pipe(p1) == 0);
	CHECK(pipe(p2) == 0);
	CHECK(fstat(p1[0], &a) == 0 && S_ISFIFO(a.st_mode));
	CHECK(fstat(p2[0], &b) == 0 && S_ISFIFO(b.st_mode));
	CHECK(!(a.st_ino == b.st_ino && a.st_dev == b.st_dev));
	CHECK(close(p1[0]) == 0 && close(p1[1]) == 0);
	CHECK(close(p2[0]) == 0 && close(p2[1]) == 0);
}

/* stat.html DESCRIPTION, again: a pipe and a console are different
 * files and must not collide either, even though both used to report
 * st_dev==0 && st_ino==0 before this fix.  /dev/tty (src/internal/path.c
 * maps it straight to "CON") gets an independent, always-openable
 * console handle regardless of what's on fd 0/1/2, so this works the
 * same whether or not the test runner attached a console to us there --
 * but NtCreateFile on CON can still fail outright with no console
 * subsystem present at all (some CI/service contexts), so, like
 * test_ttyname_r_erange above, detect and skip rather than asserting
 * either shape. */
static void test_stat_pipe_vs_console_distinct(void)
{
	int p[2];
	int cfd = open("/dev/tty", O_RDWR);
	struct stat a, b;
	if (cfd < 0) {
		printf("note: /dev/tty not openable here, pipe-vs-console skipped\n");
		return;
	}
	CHECK(pipe(p) == 0);
	CHECK(fstat(p[0], &a) == 0 && S_ISFIFO(a.st_mode));
	CHECK(fstat(cfd, &b) == 0 && S_ISCHR(b.st_mode));
	CHECK(!(a.st_ino == b.st_ino && a.st_dev == b.st_dev));
	CHECK(close(p[0]) == 0 && close(p[1]) == 0);
	CHECK(close(cfd) == 0);
}

/* stat.html: st_mtime tracks data writes; st_atime is not required to
 * track reads on every implementation (POSIX permits an atime update to
 * be deferred/omitted), so only mtime-after-write is asserted here. */
static void test_stat_mtime_after_write(void)
{
	int fd = open("t-mt.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	struct stat before, after;
	CHECK(fd >= 0);
	CHECK(fstat(fd, &before) == 0);
	CHECK(write(fd, "x", 1) == 1);
	CHECK(fstat(fd, &after) == 0);
	CHECK(after.st_mtime >= before.st_mtime);
	CHECK(close(fd) == 0);
	unlink("t-mt.txt");
}

/* sysconf.html: _SC_CHILD_MAX "the value returned shall not be more
 * restrictive than the corresponding value described for {CHILD_MAX}."
 * src/internal/libc.h documents this as a large synthetic ceiling (NT
 * process tables are not fixed-size), so just check it is a definite,
 * large, positive number rather than -1/unlimited. */
static void test_sysconf_child_max(void)
{
	long n = sysconf(_SC_CHILD_MAX);
	CHECK(n > 0);
	CHECK(n >= 25);		/* _POSIX_CHILD_MAX floor */
}

/* pipe.html: "The pipe() function shall create a pipe... two file
 * descriptors, fildes[0] for reading and fildes[1] for writing" -- and
 * PIPE_BUF-sized writes are documented elsewhere as atomic; here just
 * the two-independent-fds-not-aliasing part that test/unistd.c's pipe
 * block doesn't check: closing one end does not affect the other's
 * validity for fcntl(). */
static void test_pipe_ends_independent(void)
{
	int p[2];
	CHECK(pipe(p) == 0);
	CHECK(close(p[1]) == 0);
	CHECK(fcntl(p[0], F_GETFD) == 0);	/* still a live descriptor */
	CHECK(close(p[0]) == 0);
}

/* ttyname_r.html ERRORS ERANGE: "The buffer supplied is too small."
 * ntlibc's only tty type is CON (src/unistd/ttyname.c), so this can only
 * be reached from an fd that isatty() accepts; whether stdin is such a
 * descriptor depends on how the test runner launched us (Wine's runner
 * and `make asan`'s native run both may or may not attach a console), so
 * detect and skip rather than asserting either shape. */
static void test_ttyname_r_erange(void)
{
	char tiny[2];
	int r;
	if (!isatty(0)) {
		printf("note: fd 0 is not a tty here, ttyname_r ERANGE path not reachable\n");
		return;
	}
	r = ttyname_r(0, tiny, sizeof tiny);
	CHECK(r == ERANGE);
}

/* stat.html DESCRIPTION cites sys_stat.h.html's st_size field: "For
 * regular files, the file size in bytes ... For other file types, the
 * use of this field is unspecified" -- a directory falls under "other
 * file types", so POSIX leaves st_size for one entirely up to the
 * implementation.  src/stat/stat.c's mode_from_attrs()/__fstat_handle()
 * document ntlibc's choice: "st_size = S_ISDIR(...) ? 0 : ...".  Since
 * any value is spec-legal, this is a real, always-passing assertion of
 * ntlibc's own documented design, not a fence -- confirm it holds
 * regardless of what the directory contains. */
static volatile int got_sig;
static void mark_got_sig(int s) { (void)s; got_sig = 1; }

static void test_stat_dir_size_is_zero(void)
{
	struct stat st;
	int fd;

	CHECK(mkdir("t-dsz", 0755) == 0);
	CHECK(stat("t-dsz", &st) == 0);
	CHECK(S_ISDIR(st.st_mode));
	CHECK(st.st_size == 0);

	fd = open("t-dsz/child.txt", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0 && write(fd, "abcdefgh", 8) == 8 && close(fd) == 0);
	CHECK(stat("t-dsz", &st) == 0);
	CHECK(st.st_size == 0);	/* still 0 -- not derived from directory contents */

	unlink("t-dsz/child.txt");
	CHECK(rmdir("t-dsz") == 0);
}

/* ---------------------------------------------------------------------
 * chmod()'s permission bits: three clauses, all UNIMPL, none N/A.
 *
 * The three fences below were N/A, each naming its own variation on
 * "NTFS has no attribute to store this in".  That is true of the
 * ATTRIBUTE WORD and false about the platform, and the difference is
 * the whole point: NT's permission model is not the attribute word, it
 * is the security descriptor, and NT's is strictly MORE expressive than
 * POSIX's nine mode bits, not less.
 *
 *   - There is an execute right.  FILE_EXECUTE (0x0020) and
 *     FILE_GENERIC_EXECUTE are defined in this tree's OWN header,
 *     src/internal/nt.h:376 and :395.  NT checks it when a section is
 *     created for image execution, which is the same gate POSIX's
 *     S_IXUSR is.
 *   - There is per-identity granularity.  A DACL holds one ACE per SID,
 *     so "group may write but other may not" has somewhere to live;
 *     "one aggregate FILE_ATTRIBUTE_READONLY bit" is a property of the
 *     mapping ntlibc chose, not of NTFS.
 *   - There is a deny.  RtlAddAccessDeniedAce builds exactly the DENY
 *     ACE the read-bits fence says would be needed.
 *   - And the calls exist: NtQuerySecurityObject and NtSetSecurityObject
 *     are real ntdll syscalls, exported and implemented even by Wine
 *     (dlls/ntdll/ntdll.spec lines 344 and 419), as are
 *     RtlSetDaclSecurityDescriptor and RtlAddAccessDeniedAce (lines
 *     1024 and 496).
 *
 * None of them is declared in src/internal/nt.h.  That is the real
 * blocker, and it is a choice: ntlibc does no ACL work anywhere, so
 * chmod() has nothing but FILE_ATTRIBUTE_READONLY to write and stat()
 * has nothing but the attribute word to read back.  Two of the three
 * fences below already half-admitted this -- one said DENY ACEs are
 * "which ntlibc's chmod() does not attempt", the other said observing
 * it needs "real NT DACL storage, which no code in this tree has".  A
 * fence that names the alternative and says we did not do it is
 * describing UNIMPL, whatever tag it wears.
 *
 * Retagged, not reopened: the decision to stay out of ACL editing may
 * well be right (it is a large amount of surface, and a POSIX-mode ->
 * DACL mapping has real design questions about which SIDs stand in for
 * "group" and "other").  What changes is only that these say so.
 *
 * test/POSIX-COVERAGE.md's "re-audit against real Windows rather than
 * Wine" note applies with force here: Wine's DACL emulation over a Unix
 * filesystem is not NT's, so anyone implementing this must measure on
 * real Windows and not on Wine.
 * ------------------------------------------------------------------ */

/* sys_stat.h.html: S_IRUSR/S_IRGRP/S_IROTH (0444) "read permission" for
 * owner/group/other. src/stat/chmod.c's chmod_handle() comment: "chmod
 * can only express one thing on NTFS: whether the file is read-only" --
 * it tests "mode & 0222" and flips FILE_ATTRIBUTE_READONLY, the only
 * readability-adjacent bit NTFS exposes without ACL surgery ntlibc does
 * not do.  There is no NTFS attribute for "unreadable but not
 * read-only", so the read bits src/stat/stat.c's mode_from_attrs()
 * synthesizes (0444, always set) cannot be cleared by chmod() at all --
 * confirmed live below rather than asserted from the source comment. */
#if 0 /* UNIMPL: chmod.html DESCRIPTION "set the file permission bits
       * ... to the value contained in mode" -- but chmod(path, 0) here
       * leaves S_IRUSR|S_IRGRP|S_IROTH set anyway.  Was N/A on "there
       * is no 'deny read' attribute a chmod-only implementation can
       * flip".  True of the attribute word; but the DENY ACE the same
       * sentence goes on to name is exactly what NT provides for this
       * (RtlAddAccessDeniedAce), and the sentence's own conclusion --
       * "which ntlibc's chmod() does not attempt" -- is a choice, not a
       * platform limit.  See the banner above this group. */
static void test_chmod_cannot_clear_read_bits(void)
{
	struct stat st;
	int fd = open("t-crb.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(chmod("t-crb.txt", 0000) == 0);
	CHECK(stat("t-crb.txt", &st) == 0);
	CHECK((st.st_mode & 0444) == 0);	/* would fail: NTFS keeps the file readable */
	unlink("t-crb.txt");
}
#endif

/* sys_stat.h.html: S_IXUSR/S_IXGRP/S_IXOTH (0111) "execute/search
 * permission".  src/stat/stat.c's has_exe_suffix()/mode_from_attrs()
 * derive these purely from the filename's extension (.exe/.com/.bat/
 * .cmd/.sh); chmod_handle() never touches them.  NTFS has no execute
 * permission bit at all (any file can be "run" via CreateProcess if its
 * *contents* look like a PE/script; NT does not gate that on a
 * permission bit the way exec() gates on S_IXUSR), so ntlibc's naming
 * heuristic is the only signal available and chmod cannot move it. */
#if 0 /* UNIMPL: chmod.html says mode's 0111 bits become the new
       * S_IX{USR,GRP,OTH} bits; here chmod(0000) on a .exe leaves them
       * set and chmod(0777) on a .txt cannot set them.  Was N/A on "NT
       * has no execute-permission attribute to write".  NT has no
       * execute ATTRIBUTE, which is what that sentence is really about,
       * but it does have an execute RIGHT -- FILE_EXECUTE, defined in
       * this tree's own src/internal/nt.h:376 -- carried in the
       * security descriptor and checked when a section is created for
       * image execution.  ntlibc writes no security descriptors, which
       * is why the filename heuristic is all st_mode has.  See the
       * banner above this group. */
static void test_chmod_cannot_move_exec_bits(void)
{
	struct stat st;
	int fd = open("t-cxb.exe", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(stat("t-cxb.exe", &st) == 0);
	CHECK((st.st_mode & 0111) != 0);	/* name-derived, exec bits already on */
	CHECK(chmod("t-cxb.exe", 0000) == 0);
	CHECK(stat("t-cxb.exe", &st) == 0);
	CHECK((st.st_mode & 0111) == 0);	/* would fail: still name-derived, still on */
	unlink("t-cxb.exe");

	fd = open("t-cxb.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(chmod("t-cxb.txt", 0777) == 0);
	CHECK(stat("t-cxb.txt", &st) == 0);
	CHECK((st.st_mode & 0111) != 0);	/* would fail: .txt never gets exec bits */
	unlink("t-cxb.txt");
}
#endif

/* sys_stat.h.html: S_IWGRP (020) and S_IWOTH (002) are documented as
 * independent bits from S_IWUSR (0200).  Verified live (not from
 * memory): starting from a read-only file, fchmod(fd, 0020) -- group
 * write ONLY, no owner/other write bit -- clears FILE_ATTRIBUTE_READONLY
 * exactly as fchmod(fd, 0222) would, and the file comes back as mode
 * 0644 (owner-write shape), never 0640.  (fchmod on an already-open
 * O_RDONLY fd, not chmod-by-path, sidesteps the separate Wine quirk
 * documented in test_open_umask_bug where the server's
 * FileWriteAttributes-as-Unix-O_WRONLY mapping refuses a chmod back
 * from read-only by path -- see that comment for the citation.) */
#if 0 /* UNIMPL: chmod.html says the individual mode bits requested
       * become the new mode; here S_IWGRP/S_IWOTH alone have the
       * identical, all-or-nothing effect as S_IWUSR|S_IWGRP|S_IWOTH
       * together.  The proximate mechanism is right -- chmod_handle()
       * in src/stat/chmod.c tests "mode & 0222" as one aggregate
       * boolean against a single FILE_ATTRIBUTE_READONLY bit -- but the
       * conclusion drawn from it was not: "there is no NTFS concept of
       * group- or other-write distinct from owner-write to store the
       * difference in" is false.  A DACL holds one ACE per SID, which
       * is per-identity granularity POSIX's three classes do not even
       * need all of.  The aggregate is a property of the mapping ntlibc
       * chose, not of NTFS.  See the banner above this group. */
static void test_chmod_group_other_write_aliases_owner(void)
{
	struct stat st;
	int fd = open("t-gow.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(chmod("t-gow.txt", 0000) == 0);
	fd = open("t-gow.txt", O_RDONLY);
	CHECK(fd >= 0);
	CHECK(fchmod(fd, 0020) == 0);		/* group-write only */
	CHECK(close(fd) == 0);
	CHECK(stat("t-gow.txt", &st) == 0);
	CHECK((st.st_mode & 0777) == 0640);	/* would fail: NTFS gives 0644, not 0640 */
	unlink("t-gow.txt");
}
#endif

/* kill.html DESCRIPTION: "If pid is -1 ... sig shall be sent to all
 * processes ... for which the process has permission to send that
 * signal" -- and the sending process always has permission to signal
 * itself (kill(getpid(), sig) is unconditionally valid per the same
 * page), so at minimum kill(-1, sig) must reach the caller.  Verified
 * live: src/signal/signal.c's kill() checks "pid == getpid() || pid ==
 * 0" first, then falls straight into "if (pid < 0) { errno = ESRCH;
 * return -1; }" -- pid == -1 never reaches the self-check, so this
 * fails with ESRCH unconditionally, on both Wine and real NT (the
 * branch never touches NT at all). */
static void test_kill_neg1_reaches_self(void)
{
	got_sig = 0;
	CHECK(signal(SIGUSR1, mark_got_sig) != SIG_ERR);
	errno = 0;
	CHECK(kill(-1, SIGUSR1) == 0);
	CHECK(got_sig == 1);
	CHECK(signal(SIGUSR1, SIG_DFL) != SIG_ERR);
}

/* kill.html DESCRIPTION: "If pid is 0 ... sig shall be sent to all
 * processes ... whose process group ID is equal to the process group
 * ID of the sender" -- src/unistd/ids.c's getpgrp()/getpgid() always
 * report group 1 (every process is its own group of one on this
 * platform, per the ledger's kill pid==0/pid<-1 N/A note), so "all
 * processes in the sender's group" reduces exactly to "the sender", and
 * src/signal/signal.c's kill() implements precisely that: "pid ==
 * getpid() || pid == 0" both route to raise().  This is the
 * group-of-one model actually working, not something Wine-hostile, so
 * it is a real, live, unfenced assertion. */
static void test_kill_zero_is_own_group_of_one(void)
{
	got_sig = 0;
	CHECK(signal(SIGUSR2, mark_got_sig) != SIG_ERR);
	errno = 0;
	CHECK(kill(0, SIGUSR2) == 0);
	CHECK(got_sig == 1);
	CHECK(signal(SIGUSR2, SIG_DFL) != SIG_ERR);
}

/* kill.html ERRORS: "[EPERM] The process does not have permission to
 * send the signal to any receiving process."
 *
 * Asserted in test/posix-kill-perm-win.c, not here.  Two things used to
 * sit at this spot and both were wrong, in opposite directions:
 *
 *   - an N/A fence, test_kill_eperm_uid_mismatch(), claiming the clause
 *     was "structurally impossible here" because src/unistd/ids.c has
 *     exactly one uid (1000) forever.  kill()'s EPERM has nothing to do
 *     with uids: src/signal/signal.c maps NtOpenProcess's
 *     STATUS_ACCESS_DENIED straight to EPERM, and that is NT's own
 *     access check on the target process object.  The fence was
 *     refuted by the test fifteen lines below it, in this same file.
 *
 *   - test_kill_eperm_protected_process(), which asserted the real
 *     thing but skipped itself with a printed note whenever kill(4, 0)
 *     answered ESRCH.  That is the correct shape for Wine, where pid 4
 *     does not exist -- but it is also indistinguishable from real NT
 *     having stopped denying pid 4, which the test would then pass
 *     silently.  A conditional assertion cannot be the only assertion.
 *
 * The replacement is unconditional, on the leg where the condition
 * genuinely holds: *-win.c tests are built everywhere and run only on
 * the real-Windows CI leg.  It also carries the positive controls this
 * one lacked (a live self-kill, and a nonexistent pid that must answer
 * ESRCH rather than EPERM), so a build where every kill() failed cannot
 * pass it.  See that file's header for the measured NtOpenProcess
 * status table and for why no token-elevation check belongs in it.
 *
 * The uid-mismatch half of the clause is a different mechanism and is
 * still unasserted: it needs a process owned by another user, observed
 * from an unelevated token.  It is not folded in here. */

/* access.html DESCRIPTION: access() "shall check ... using the real
 * user ID in place of the effective user ID and the real group ID in
 * place of the effective group ID"; faccessat()'s AT_EACCESS flag
 * "enables checking access using the effective user and group IDs
 * instead". src/unistd/access.c's faccessat() ignores flags entirely
 * ("(void)flags;"), which is only spec-compliant because
 * src/unistd/ids.c makes real and effective always identical (a single
 * fixed uid 1000, no setuid); with two distinct identities available,
 * ignoring AT_EACCESS would be a real bug.  This is a real, live
 * assertion of that structural fact, not a fence. */
static void test_access_real_effective_uid_identical(void)
{
	int fd = open("t-reu.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(getuid() == geteuid() && getgid() == getegid());
	CHECK(access("t-reu.txt", R_OK) == 0);
	CHECK(faccessat(AT_FDCWD, "t-reu.txt", R_OK, 0) ==
	      faccessat(AT_FDCWD, "t-reu.txt", R_OK, AT_EACCESS));
	unlink("t-reu.txt");
}

/* ---------------------------------------------------------------------
 * The four names test/POSIX-GAP-ACCOUNTING.md flags as claimed by a
 * ledger row while no test in the tree ever calls them: utimes,
 * fpathconf, readlink, unlinkat.  Each block cites the page it was
 * checked against.
 * ------------------------------------------------------------------ */

/* utimes.html (XSI; the page is shared with futimens/utimensat).
 * DESCRIPTION: "If the times argument is a null pointer, both the access
 * and modification timestamps shall be set to the greatest value
 * supported by the file system that is not greater than the current
 * time."  RETURN VALUE: "Upon successful completion, these functions
 * shall return 0.  Otherwise, these functions shall return -1 and set
 * errno".  ERRORS [ENOENT]: "A component of path does not name an
 * existing file or path is an empty string."
 *
 * The tv_usec -> tv_nsec scaling is the part worth pinning: utimes()
 * takes microseconds where utimensat() takes nanoseconds, and NTFS
 * stores 100ns ticks, so a microsecond value is exactly representable
 * and a missing *1000 would be visible.  Filesystem behaviour, so Wine
 * is only weak evidence here; the real-Windows CI leg is the authority. */
static void test_utimes(void)
{
	struct timeval tv[2];
	struct stat st;
	time_t before, after;
	int fd = open("ut.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);

	tv[0].tv_sec = 1000000000; tv[0].tv_usec = 123456;
	tv[1].tv_sec = 1000000001; tv[1].tv_usec = 654321;
	CHECK(utimes("ut.txt", tv) == 0);
	CHECK(stat("ut.txt", &st) == 0);
	CHECK(st.st_atim.tv_sec == 1000000000);
	CHECK(st.st_mtim.tv_sec == 1000000001);
	/* microseconds scaled to nanoseconds, not stored raw */
	CHECK(st.st_atim.tv_nsec == 123456000L);
	CHECK(st.st_mtim.tv_nsec == 654321000L);

	/* null times: "shall be set to ... not greater than the current
	 * time" -- so within the window the call was made in, and not the
	 * stale 2001 stamps just written. */
	before = time(0);
	CHECK(utimes("ut.txt", 0) == 0);
	after = time(0);
	CHECK(stat("ut.txt", &st) == 0);
	CHECK(st.st_mtim.tv_sec >= before && st.st_mtim.tv_sec <= after + 1);
	CHECK(st.st_atim.tv_sec >= before && st.st_atim.tv_sec <= after + 1);

	errno = 0;
	CHECK(utimes("nope-utimes", tv) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(utimes("", tv) == -1 && errno == ENOENT);
	CHECK(unlink("ut.txt") == 0);
}

/* fpathconf.html.  DESCRIPTION: fpathconf() "shall determine the current
 * value of a configurable limit or option (variable) that is associated
 * with a file or directory", the fildes form taking an open descriptor.
 * RETURN VALUE: "Otherwise, the value of the requested variable shall be
 * returned ... without changing errno"; -1 with EINVAL "if the value of
 * name is invalid"; and "the value returned shall not be more
 * restrictive than the corresponding value described to the application
 * when it was compiled with the implementation's <limits.h>".
 *
 * Pure C-library arithmetic over compile-time constants: Wine is a sound
 * oracle for all of it.
 *
 * N/A here, deliberately: the optional [EBADF] ("The fildes argument is
 * not a valid file descriptor").  src/unistd/sysconf.c's fpathconf()
 * ignores fildes entirely and forwards to pathconf(), so a closed
 * descriptor still answers -- which POSIX permits, because that error is
 * listed under "may fail", not "shall fail".  Asserting either way would
 * be asserting a choice the spec leaves open. */
static void test_fpathconf(void)
{
	static const int names[] = {
		_PC_LINK_MAX, _PC_MAX_CANON, _PC_MAX_INPUT, _PC_NAME_MAX,
		_PC_PATH_MAX, _PC_PIPE_BUF, _PC_CHOWN_RESTRICTED,
		_PC_NO_TRUNC, _PC_VDISABLE
	};
	size_t i;
	int fd = open("fpc.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);

	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		long a, b;
		errno = 0;
		a = fpathconf(fd, names[i]);
		/* -1 is only legal for a variable with no limit, and none of
		 * these nine is unlimited here; either way errno must not
		 * have been touched. */
		CHECK(errno == 0);
		b = pathconf("fpc.txt", names[i]);
		CHECK(a == b);
		CHECK(a >= 0);
	}

	/* "shall not be more restrictive than" the <limits.h> minimums */
	CHECK(fpathconf(fd, _PC_NAME_MAX) >= _POSIX_NAME_MAX);
	CHECK(fpathconf(fd, _PC_PATH_MAX) >= _POSIX_PATH_MAX);
	CHECK(fpathconf(fd, _PC_PIPE_BUF) >= _POSIX_PIPE_BUF);
	CHECK(fpathconf(fd, _PC_LINK_MAX) >= _POSIX_LINK_MAX);
	CHECK(fpathconf(fd, _PC_MAX_CANON) >= _POSIX_MAX_CANON);
	CHECK(fpathconf(fd, _PC_MAX_INPUT) >= _POSIX_MAX_INPUT);

	/* [EINVAL] "The value of the name argument is invalid." */
	errno = 0;
	CHECK(fpathconf(fd, -1) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(fpathconf(fd, 12345) == -1 && errno == EINVAL);

	CHECK(close(fd) == 0);
	CHECK(unlink("fpc.txt") == 0);
}

/* readlink.html.  RETURN VALUE: "Upon successful completion, these
 * functions shall return the count of bytes placed in the buffer.
 * Otherwise, these functions shall return a value of -1, leave the
 * buffer unchanged, and set errno".  DESCRIPTION: "If the buf argument
 * is not large enough to contain the link content, the first bufsize
 * bytes shall be placed in buf" -- and APPLICATION USAGE warns the
 * result is not null-terminated, so the byte past the returned count
 * must be untouched.  ERRORS [EINVAL]: "The path argument names a file
 * that is not a symbolic link."  [ENOENT]: "A component of path does not
 * name an existing file or path is an empty string."
 *
 * No symbolic link can be created in this environment, so the success
 * half is conditional on symlink() working; the error half is not, and
 * runs everywhere.  The blocker differs by leg -- under stock Wine below
 * 10.19 it is an unimplemented FSCTL_SET_REPARSE_POINT, not
 * SeCreateSymbolicLinkPrivilege; see test/posix-unreferenced.c's test_fchmodat_eloop() fence, which is the
 * canonical account and is not duplicated here.
 * Filesystem behaviour throughout -- real-Windows CI is the authority. */
static void test_readlink(void)
{
	char buf[64];
	ssize_t n;
	int fd = open("rl-target.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, "hello", 5) == 5 && close(fd) == 0);

	/* [EINVAL] on a regular file, and the buffer is left unchanged. */
	memset(buf, '@', sizeof buf);
	errno = 0;
	CHECK(readlink("rl-target.txt", buf, sizeof buf) == -1 && errno == EINVAL);
	CHECK(buf[0] == '@');

	/* [ENOENT] for a missing name and for the empty string. */
	errno = 0;
	CHECK(readlink("nope-readlink", buf, sizeof buf) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(readlink("", buf, sizeof buf) == -1 && errno == ENOENT);

	/* readlinkat(AT_FDCWD, ...) "shall be identical to a call to
	 * readlink()". */
	errno = 0;
	CHECK(readlinkat(AT_FDCWD, "rl-target.txt", buf, sizeof buf) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(readlinkat(AT_FDCWD, "nope-readlink", buf, sizeof buf) == -1 && errno == ENOENT);

	if (symlink("rl-target.txt", "rl.lnk") == 0) {
		memset(buf, '@', sizeof buf);
		n = readlink("rl.lnk", buf, sizeof buf);
		CHECK(n == (ssize_t)strlen("rl-target.txt"));
		if (n > 0) {
			CHECK(!memcmp(buf, "rl-target.txt", (size_t)n));
			/* not null-terminated: nothing written past n */
			CHECK(buf[n] == '@');
		}

		/* truncation: "the first bufsize bytes shall be placed in
		 * buf", and the return is that count, not the full length. */
		memset(buf, '@', sizeof buf);
		n = readlink("rl.lnk", buf, 4);
		CHECK(n == 4);
		CHECK(!memcmp(buf, "rl-t", 4));
		CHECK(buf[4] == '@');

		/* readlinkat relative to a directory descriptor */
		{
			int dfd = open(".", O_RDONLY | O_DIRECTORY);
			CHECK(dfd >= 0);
			if (dfd >= 0) {
				memset(buf, '@', sizeof buf);
				n = readlinkat(dfd, "rl.lnk", buf, sizeof buf);
				CHECK(n == (ssize_t)strlen("rl-target.txt"));
				if (n > 0) CHECK(!memcmp(buf, "rl-target.txt", (size_t)n));
				CHECK(close(dfd) == 0);
			}
		}
		CHECK(unlink("rl.lnk") == 0);
	} else {
		printf("note: symlink() not supported here (errno %d), readlink success path skipped\n", errno);
	}
	CHECK(unlink("rl-target.txt") == 0);
}

/* unlink.html (the unlinkat half).  DESCRIPTION: AT_REMOVEDIR means
 * "Remove the directory entry specified by fd and path as a directory,
 * not a normal file", and AT_FDCWD makes the call "identical to a call
 * to unlink() or rmdir() respectively".  RETURN VALUE: 0 on success,
 * -1 with errno otherwise.  ERRORS [ENOENT], [ENOTDIR] ("The flag
 * parameter has the AT_REMOVEDIR bit set and path does not name a
 * directory"), [EEXIST] or [ENOTEMPTY] ("... names a directory that is
 * not an empty directory"), [EBADF] ("path does not specify an absolute
 * path and the fd argument is neither AT_FDCWD nor a valid file
 * descriptor").  Filesystem behaviour: real-Windows CI is the authority. */
static void test_unlinkat(void)
{
	int fd = open("ua.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	struct stat st;
	CHECK(fd >= 0 && close(fd) == 0);

	/* AT_FDCWD without AT_REMOVEDIR == unlink() */
	CHECK(unlinkat(AT_FDCWD, "ua.txt", 0) == 0);
	CHECK(stat("ua.txt", &st) == -1);

	/* AT_FDCWD with AT_REMOVEDIR == rmdir() */
	CHECK(mkdir("uadir", 0755) == 0);
	CHECK(unlinkat(AT_FDCWD, "uadir", AT_REMOVEDIR) == 0);
	CHECK(stat("uadir", &st) == -1);

	/* [ENOTDIR] AT_REMOVEDIR on something that is not a directory */
	fd = open("ua2.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	errno = 0;
	CHECK(unlinkat(AT_FDCWD, "ua2.txt", AT_REMOVEDIR) == -1 && errno == ENOTDIR);
	CHECK(stat("ua2.txt", &st) == 0);	/* and it survived */

	/* [ENOTEMPTY] or [EEXIST] on a non-empty directory */
	CHECK(mkdir("uafull", 0755) == 0);
	fd = open("uafull/inner.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	errno = 0;
	CHECK(unlinkat(AT_FDCWD, "uafull", AT_REMOVEDIR) == -1 &&
	      (errno == ENOTEMPTY || errno == EEXIST));

	/* Without AT_REMOVEDIR, a directory is not removable.  POSIX lists
	 * [EPERM] for this; ntlibc reports [EISDIR], as Linux does.  Both
	 * are refusals that leave the directory in place, which is the part
	 * the spec's DESCRIPTION actually requires ("The path argument shall
	 * not name a directory unless ... the implementation supports using
	 * unlink() on directories"); the exact errno is left unasserted
	 * rather than pinned to a value POSIX does not list. */
	errno = 0;
	CHECK(unlinkat(AT_FDCWD, "uafull", 0) == -1);
	CHECK(stat("uafull", &st) == 0 && S_ISDIR(st.st_mode));

	/* relative to a real directory descriptor */
	{
		int dfd = open("uafull", O_RDONLY | O_DIRECTORY);
		CHECK(dfd >= 0);
		if (dfd >= 0) {
			CHECK(unlinkat(dfd, "inner.txt", 0) == 0);
			CHECK(stat("uafull/inner.txt", &st) == -1);
			errno = 0;
			CHECK(unlinkat(dfd, "inner.txt", 0) == -1 && errno == ENOENT);
			CHECK(close(dfd) == 0);
		}
	}
	CHECK(unlinkat(AT_FDCWD, "uafull", AT_REMOVEDIR) == 0);

	/* [ENOENT] */
	errno = 0;
	CHECK(unlinkat(AT_FDCWD, "nope-unlinkat", 0) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(unlinkat(AT_FDCWD, "", 0) == -1 && errno == ENOENT);

	/* [EBADF] relative path against a descriptor that is not open */
	errno = 0;
	CHECK(unlinkat(4096, "ua2.txt", 0) == -1 && errno == EBADF);
	CHECK(stat("ua2.txt", &st) == 0);

	/* [EINVAL] "(unlinkat() only) The value of the flag argument is not
	 * valid."  AT_REMOVEDIR is the only flag unlinkat() defines, so any
	 * other bit is invalid: src/unistd/unlink.c rejects
	 * flags & ~AT_REMOVEDIR rather than masking it off, because masking
	 * turns a caller's wrong AT_* constant into a deletion.  The file
	 * must still be there afterwards. */
	errno = 0;
	CHECK(unlinkat(AT_FDCWD, "ua2.txt", AT_SYMLINK_NOFOLLOW) == -1 && errno == EINVAL);
	CHECK(stat("ua2.txt", &st) == 0);

	CHECK(unlinkat(AT_FDCWD, "ua2.txt", 0) == 0);
}

/* mkdir.html (the mkdirat half).  DESCRIPTION: mkdirat() "shall be
 * equivalent to the mkdir() function except in the case where path
 * specifies a relative path", which is then resolved against fd; with
 * AT_FDCWD "the behavior shall be identical to a call to mkdir()".
 * RETURN VALUE: 0 on success, else -1 with errno.  ERRORS [EEXIST] "The
 * named file exists", [ENOENT] "A component of the path prefix ... does
 * not name an existing directory or path is an empty string", [ENOTDIR]
 * for a non-directory prefix component and (mkdirat only) for an fd that
 * is "a file descriptor associated with a non-directory file", [EBADF]
 * "The path argument does not specify an absolute path and the fd
 * argument is neither AT_FDCWD nor a valid file descriptor".
 *
 * The mode argument is deliberately not asserted: this ledger already
 * records directory mode bits as N/A (implementation-defined on NTFS,
 * and src/stat/mkdir.c ignores mode by design), so the clause about
 * initialising permission bits from mode has nothing observable to check
 * against here.  Filesystem behaviour, so Wine is weak evidence and the
 * real-Windows CI leg is the authority. */
static void test_mkdirat(void)
{
	struct stat st;
	int dfd;

	/* AT_FDCWD is identical to mkdir() */
	CHECK(mkdirat(AT_FDCWD, "mda", 0755) == 0);
	CHECK(stat("mda", &st) == 0 && S_ISDIR(st.st_mode));

	/* [EEXIST] the named file exists -- as a directory ... */
	errno = 0;
	CHECK(mkdirat(AT_FDCWD, "mda", 0755) == -1 && errno == EEXIST);
	/* ... and as a plain file */
	{
		int fd = open("mdf.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
		CHECK(fd >= 0 && close(fd) == 0);
		errno = 0;
		CHECK(mkdirat(AT_FDCWD, "mdf.txt", 0755) == -1 && errno == EEXIST);
	}

	/* [ENOENT] missing path prefix, and the empty string */
	errno = 0;
	CHECK(mkdirat(AT_FDCWD, "mda-nope/child", 0755) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(mkdirat(AT_FDCWD, "", 0755) == -1 && errno == ENOENT);

	/* [ENOTDIR] a prefix component that is an existing regular file */
	errno = 0;
	CHECK(mkdirat(AT_FDCWD, "mdf.txt/child", 0755) == -1 && errno == ENOTDIR);

	/* [EBADF] relative path against a descriptor that is not open */
	errno = 0;
	CHECK(mkdirat(4096, "mda-bad", 0755) == -1 && errno == EBADF);
	CHECK(stat("mda-bad", &st) == -1);

	/* [ENOTDIR] fd is open on a non-directory */
	{
		int ffd = open("mdf.txt", O_RDONLY);
		CHECK(ffd >= 0);
		if (ffd >= 0) {
			errno = 0;
			CHECK(mkdirat(ffd, "mda-nd", 0755) == -1 && errno == ENOTDIR);
			CHECK(close(ffd) == 0);
		}
	}

	/* the relative-to-a-real-dirfd case that is mkdirat()'s whole point */
	dfd = open("mda", O_RDONLY | O_DIRECTORY);
	CHECK(dfd >= 0);
	if (dfd >= 0) {
		CHECK(mkdirat(dfd, "inner", 0755) == 0);
		CHECK(stat("mda/inner", &st) == 0 && S_ISDIR(st.st_mode));
		errno = 0;
		CHECK(mkdirat(dfd, "inner", 0755) == -1 && errno == EEXIST);
		/* an absolute path ignores fd entirely */
		CHECK(close(dfd) == 0);
		CHECK(rmdir("mda/inner") == 0);
	}

	CHECK(rmdir("mda") == 0);
	CHECK(unlink("mdf.txt") == 0);
}

/* mkfifo.html and mknod.html.  Both are permanent stubs here -- see
 * test/POSIX-GAP-ACCOUNTING.md's "permanent degenerate stubs" table,
 * which records mkfifo/mkfifoat as ENOSYS (NT named pipes exist and are
 * pure NTDLL, but nobody has mapped FIFO semantics onto them) and
 * mknod/mknodat as EPERM.
 *
 * What is asserted is the one clause a stub can still honour, and which
 * both pages state in identical words: "If -1 is returned, no FIFO shall
 * be created" / "If -1 is returned, the new file shall not be created."
 * A stub that left debris behind would be worse than a stub.
 *
 * N/A, with the reason: every other clause on both pages (mode ANDed
 * with the file creation mask, the resulting file type, [EEXIST],
 * [ENOTDIR], [EACCES], [EROFS], [ENOSPC], and the dirfd resolution the
 * *at forms exist for) presupposes that the call can succeed at least
 * once.  It cannot here, on any input, so there is nothing to observe.
 *
 * mknod()'s EPERM is POSIX's own answer for this situation -- "[EPERM]
 * The invoking process does not have appropriate privileges and the file
 * type is not FIFO-special" -- so that one is asserted exactly.
 * mkfifo()'s ENOSYS is *not* in mkfifo.html's ERRORS list, and no errno
 * that page does list would be truthful either; that deviation is
 * already carried as a known permanent stub in the gap-accounting file
 * rather than re-opened as a new bug here, so the assertion below pins
 * the -1 and the absence of debris and leaves the errno value to that
 * record.  Pure library behaviour: Wine is a sound oracle. */
static void test_mkfifo_mknod_stubs(void)
{
	struct stat st;

	errno = 0;
	CHECK(mkfifo("mff", 0666) == -1);
	CHECK(errno != 0);
	CHECK(stat("mff", &st) == -1);		/* "no FIFO shall be created" */

	errno = 0;
	CHECK(mkfifoat(AT_FDCWD, "mffa", 0666) == -1);
	CHECK(errno != 0);
	CHECK(stat("mffa", &st) == -1);

	/* mknod: "[EPERM] The invoking process does not have appropriate
	 * privileges and the file type is not FIFO-special." */
	errno = 0;
	CHECK(mknod("mnd", S_IFCHR | 0666, 0) == -1 && errno == EPERM);
	CHECK(stat("mnd", &st) == -1);		/* "the new file shall not be created" */

	errno = 0;
	CHECK(mknodat(AT_FDCWD, "mnda", S_IFCHR | 0666, 0) == -1 && errno == EPERM);
	CHECK(stat("mnda", &st) == -1);

	/* The only portable use of mknod() is S_IFIFO with dev 0; it is
	 * refused the same way, and still leaves nothing behind. */
	errno = 0;
	CHECK(mknod("mndf", S_IFIFO | 0666, 0) == -1);
	CHECK(errno != 0);
	CHECK(stat("mndf", &st) == -1);
}

/* ---------------------------------------------------------------------
 * The rest of test/POSIX-GAP-ACCOUNTING.md's never-asserted <unistd.h>
 * list.  The exec family (execl/execle/execlp/fexecve) is in
 * test/exec.c, which already has the spawn-a-role harness they need.
 * ------------------------------------------------------------------ */

/* confstr.html.  DESCRIPTION: "If len is not 0 ... if the string to be
 * returned is longer than len bytes, including the terminating null,
 * then the string shall be truncated"; "If len is 0 and buf is a null
 * pointer, then confstr() shall still return the integer value as
 * defined below, but shall not return a string."  RETURN VALUE: for a
 * valid name with a value, "the size of buffer that would be needed to
 * hold the entire configuration-defined value including the terminating
 * null"; for an invalid name, "confstr() shall return 0 and set errno to
 * indicate the error".  ERRORS: "[EINVAL] The value of the name argument
 * is invalid."  Pure library string handling: Wine is a sound oracle. */
static void test_confstr(void)
{
	char buf[64];
	size_t n;

	/* _CS_PATH is the only name include/unistd.h defines. */
	n = confstr(_CS_PATH, buf, sizeof buf);
	CHECK(n > 1);
	CHECK(n == strlen(buf) + 1);

	/* "len is 0 and buf is a null pointer": the size is still returned
	 * and nothing is written. */
	CHECK(confstr(_CS_PATH, NULL, 0) == n);

	/* truncation to len-1 bytes plus the null, with the full size still
	 * returned so the caller can size a second buffer. */
	memset(buf, '@', sizeof buf);
	CHECK(confstr(_CS_PATH, buf, 4) == n);
	CHECK(strlen(buf) == 3);
	CHECK(buf[3] == 0);

	/* len 1: nothing but the terminator fits */
	memset(buf, '@', sizeof buf);
	CHECK(confstr(_CS_PATH, buf, 1) == n);
	CHECK(buf[0] == 0);

#if 0	/* BUG: confstr() reports success for an invalid name.
	 * confstr.html RETURN VALUE: "If the value of the name argument is
	 * invalid, confstr() shall return 0 and set errno to indicate the
	 * error", and ERRORS lists "[EINVAL] The value of the name argument
	 * is invalid" as its only, shall-fail, entry.
	 *
	 * Mechanism: src/unistd/sysconf.c's confstr() starts from
	 * `const char *s = "";` and only replaces it when
	 * `name == _CS_PATH`.  An unrecognized name therefore falls through
	 * the same path a genuine empty value would: it writes a lone NUL
	 * into the caller's buffer and returns `i + 1` == 1.  A caller
	 * cannot tell an invalid name from a valid one whose value happens
	 * to be empty, and the mandated 0-plus-EINVAL never happens for any
	 * input.  (POSIX does distinguish those two cases: a valid name with
	 * no configuration-defined value returns 0 with errno *unchanged*,
	 * which is also unreachable here.)  Probed on this tree: both calls
	 * below return 1 with errno untouched.  Re-enable when confstr()
	 * rejects unknown names. */
	errno = 0;
	CHECK(confstr(-1, buf, sizeof buf) == 0 && errno == EINVAL);
	errno = 0;
	CHECK(confstr(12345, buf, sizeof buf) == 0 && errno == EINVAL);
#endif
}

/* swab.html.  DESCRIPTION: "shall copy nbytes bytes, which are pointed
 * to by src, to the object pointed to by dest, exchanging adjacent
 * bytes"; "If nbytes is odd, swab() copies and exchanges nbytes-1 bytes
 * and the disposition of the last byte is unspecified"; "If copying
 * takes place between objects that overlap, the behavior is undefined";
 * "If nbytes is negative, swab() does nothing."  RETURN VALUE: none.
 * ERRORS: no errors are defined.
 *
 * Pure byte shuffling with no platform component at all, so Wine is a
 * fully sound oracle -- this is the one function in this batch where a
 * Wine pass is strong evidence. */
static void test_swab(void)
{
	char dst[16];

	memset(dst, '@', sizeof dst);
	swab("abcdef", dst, 6);
	CHECK(!memcmp(dst, "badcfe", 6));
	CHECK(dst[6] == '@');		/* nothing written past nbytes */

	/* nbytes == 0 copies nothing */
	memset(dst, '@', sizeof dst);
	swab("abcdef", dst, 0);
	CHECK(dst[0] == '@');

	/* "If nbytes is negative, swab() does nothing." */
	memset(dst, '@', sizeof dst);
	swab("abcdef", dst, -4);
	CHECK(dst[0] == '@');

	/* Odd nbytes: the first nbytes-1 bytes are exchanged, and the last
	 * byte's disposition is unspecified -- so only the swapped prefix is
	 * asserted, and only that nothing beyond nbytes was touched.
	 * src/unistd/swab.c documents its own choice (copy it through
	 * unswapped); this test deliberately does not pin that, because
	 * POSIX leaves it open and a future change to it would not be a
	 * regression. */
	memset(dst, '@', sizeof dst);
	swab("abcde", dst, 5);
	CHECK(!memcmp(dst, "badc", 4));
	CHECK(dst[5] == '@');

	/* an exchange really is an exchange: doing it twice is identity */
	swab("abcdef", dst, 6);
	swab(dst, dst + 8, 6);
	CHECK(!memcmp(dst + 8, "abcdef", 6));
}

/* sync.html.  DESCRIPTION: "The sync() function shall cause all
 * information in memory that updates file systems to be scheduled for
 * writing out to all file systems"; "The writing, although scheduled,
 * is not necessarily complete upon return from sync()."  RETURN VALUE:
 * "The sync() function shall not return a value."  ERRORS: "No errors
 * are defined."
 *
 * There is nothing observable to assert beyond "it exists, it is
 * callable, it returns, and it is not allowed to fail" -- the page
 * defines no return value and no error, and explicitly disclaims that
 * the writing has completed.  What the test can and does check is that
 * a call does not disturb errno or the data around it, since
 * src/unistd/fsync.c implements it as `void sync(void) {}`.
 *
 * N/A, with the reason: the scheduling itself.  POSIX permits sync() to
 * be a no-op in every way an application can detect (fsync() is the call
 * with a completion guarantee, and test/unistd.c already covers it), so
 * there is no conforming observation that could distinguish this
 * implementation from any other. */
static void test_sync(void)
{
	int fd = open("sy.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	struct stat st;
	CHECK(fd >= 0);
	if (fd < 0) return;
	CHECK(write(fd, "data", 4) == 4);
	errno = 0;
	sync();
	CHECK(errno == 0);		/* "No errors are defined." */
	CHECK(close(fd) == 0);
	sync();
	CHECK(stat("sy.txt", &st) == 0 && st.st_size == 4);
	CHECK(unlink("sy.txt") == 0);
}

/* getlogin.html.  RETURN VALUE: getlogin() "shall return a pointer to
 * the login name or a null pointer if the user's login name cannot be
 * found"; getlogin_r() "shall return zero" on success, "otherwise, an
 * error number shall be returned to indicate the error" -- an errno
 * *value*, not -1, which is the clause most likely to be got wrong.
 * ERRORS: "[ERANGE] The value of namesize is smaller than the length of
 * the string to be returned including the terminating null character."
 *
 * The name comes from %USERNAME%/%USER% (src/unistd/ids.c), so this is
 * environment-dependent rather than pure: both are tested against each
 * other rather than against any fixed string. */
static void test_getlogin(void)
{
	char *l = getlogin();
	char buf[256];
	int r;

	if (!l) {
		/* permitted: "a null pointer if the user's login name cannot
		 * be found".  getlogin_r() must then agree by failing too. */
		CHECK(getlogin_r(buf, sizeof buf) != 0);
		printf("note: getlogin() found no login name here, _r path checked against that\n");
		return;
	}
	CHECK(strlen(l) > 0);

	/* getlogin_r() puts the same name in the caller's buffer, and
	 * returns 0 -- not the length, and not -1. */
	memset(buf, '@', sizeof buf);
	r = getlogin_r(buf, sizeof buf);
	CHECK(r == 0);
	CHECK(!strcmp(buf, l));

	/* exactly-fits is a success, not an ERANGE */
	memset(buf, '@', sizeof buf);
	CHECK(getlogin_r(buf, strlen(l) + 1) == 0);
	CHECK(!strcmp(buf, l));

	/* [ERANGE], returned as a value rather than set in errno */
	CHECK(getlogin_r(buf, strlen(l)) == ERANGE);
}

/* Identity and session stubs: fchown, fchownat, lchown, setregid,
 * setpgrp, setsid, tcgetpgrp, tcsetpgrp.  src/unistd/ids.c's banner is
 * the governing statement -- "There is one user as far as this library
 * is concerned" -- and src/termios/termios.c's says the same for
 * sessions: exactly one, fixed.
 *
 * chown.html RETURN VALUE: "Upon successful completion, these functions
 * shall return 0"; setpgid.html/setsid.html: setsid() "shall return the
 * value of the new process group ID"; tcgetpgrp.html RETURN VALUE: "the
 * value of the process group ID of the foreground process associated
 * with the terminal"; tcsetpgrp.html: 0 on success.
 *
 * N/A, with the reason, for the *effects* rather than the returns: NT
 * has no uid/gid to set and this library models exactly one user and one
 * session, so "the file's user ID shall be set", "the process shall
 * become a session leader" and "the foreground process group shall be
 * set" have nothing that could ever be observed to change.  This ledger
 * already records the same for chown/getuid/setuid/getpgrp; these eight
 * names are the never-called members of the same family.  What *is*
 * asserted is the part that is still a real contract: the return values,
 * and internal consistency with the non-stub members of the family that
 * test/unistd.c already covers. */
static void test_id_session_stubs(void)
{
	struct stat before, after;
	int fd = open("idst.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (fd < 0) return;
	CHECK(stat("idst.txt", &before) == 0);

	/* chown.html: 0 on success, for every spelling. */
	CHECK(fchown(fd, getuid(), getgid()) == 0);
	CHECK(lchown("idst.txt", getuid(), getgid()) == 0);
	CHECK(fchownat(AT_FDCWD, "idst.txt", getuid(), getgid(), 0) == 0);
	CHECK(fchownat(AT_FDCWD, "idst.txt", getuid(), getgid(), AT_SYMLINK_NOFOLLOW) == 0);
	/* -1 for either id means "do not change it", and here nothing
	 * changes for any value: the ids reported must be unmoved. */
	CHECK(fchown(fd, (uid_t)-1, (gid_t)-1) == 0);
	CHECK(stat("idst.txt", &after) == 0);
	CHECK(after.st_uid == before.st_uid && after.st_gid == before.st_gid);
	CHECK(after.st_uid == getuid() && after.st_gid == getgid());
	CHECK(close(fd) == 0);
	CHECK(unlink("idst.txt") == 0);

	/* setregid(): 0 on success; the ids are unchanged because there is
	 * only one. */
	CHECK(setregid(getgid(), getegid()) == 0);
	CHECK(setregid((gid_t)-1, (gid_t)-1) == 0);
	CHECK(getgid() == getegid());

	/* setpgrp()/setsid(): both report the single process group/session
	 * this platform has, and must agree with the getters test/unistd.c
	 * already covers. */
	CHECK(setpgrp() == getpgrp());
	CHECK(setsid() == getsid(0));
	CHECK(setsid() == getpgrp());

	/* tcgetpgrp()/tcsetpgrp() on the one foreground group there is.
	 * tcgetpgrp() must agree with getpgrp() rather than be some third
	 * answer, and tcsetpgrp() must accept what tcgetpgrp() just said. */
	CHECK(tcgetpgrp(0) == getpgrp());
	CHECK(tcsetpgrp(0, tcgetpgrp(0)) == 0);

#if 0	/* BUG: tcgetpgrp()/tcsetpgrp() never fail, not even on a
	 * descriptor that is not open.  tcgetpgrp.html ERRORS: "The
	 * tcgetpgrp() function *shall* fail if: [EBADF] The fildes argument
	 * is not a valid file descriptor" -- shall-fail, not may-fail, and
	 * tcsetpgrp.html carries the identical clause.
	 *
	 * Mechanism: src/unistd/ttyname.c:23-24 are
	 *     pid_t tcgetpgrp(int fd) { (void)fd; return 1; }
	 *     int tcsetpgrp(int fd, pid_t p) { (void)fd; (void)p; return 0; }
	 * -- fd is discarded without ever reaching __fd_get(), which is what
	 * every other fd-taking call in the library uses to produce EBADF.
	 * This is separable from the deliberate single-session design the
	 * src/termios/termios.c banner argues for: returning a fixed process
	 * group for a *valid* terminal descriptor is that design, but
	 * answering successfully for fd 4096 is an argument check that was
	 * simply never written.  Probed on this tree: both calls below
	 * succeed.  Re-enable when both validate fildes. */
	errno = 0;
	CHECK(tcgetpgrp(4096) == -1 && errno == EBADF);
	errno = 0;
	CHECK(tcsetpgrp(4096, getpgrp()) == -1 && errno == EBADF);
#endif
}

/* link.html (the linkat half).  DESCRIPTION: linkat() resolves a
 * relative path1/path2 against fd1/fd2, and "if linkat() is passed the
 * special value AT_FDCWD ... the current working directory shall be
 * used".  link() "shall atomically create a new link for the existing
 * file and the link count of the file shall be incremented by one".
 * ERRORS [EEXIST] "The path2 argument resolves to an existing directory
 * entry", [ENOENT] "the file named by path1 does not exist; or path1 or
 * path2 points to an empty string", [EBADF] for a relative path against
 * a descriptor that is neither AT_FDCWD nor valid.
 *
 * Filesystem behaviour -- real-Windows CI is the authority; hard links
 * also need a filesystem that has them, so the success half is
 * conditional the same way test/unistd.c's link() checks are. */
static void test_linkat(void)
{
	struct stat st;
	int fd = open("la-src.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, "abc", 3) == 3 && close(fd) == 0);

	if (linkat(AT_FDCWD, "la-src.txt", AT_FDCWD, "la-dst.txt", 0) == 0) {
		struct stat s2;
		/* "the link count of the file shall be incremented by one",
		 * and both names are the same file. */
		CHECK(stat("la-src.txt", &st) == 0);
		CHECK(stat("la-dst.txt", &s2) == 0);
		CHECK(st.st_nlink == 2);
		CHECK(st.st_ino == s2.st_ino);

		/* [EEXIST] path2 resolves to an existing directory entry */
		errno = 0;
		CHECK(linkat(AT_FDCWD, "la-src.txt", AT_FDCWD, "la-dst.txt", 0) == -1 && errno == EEXIST);
		CHECK(unlink("la-dst.txt") == 0);

		/* relative to real directory descriptors on both sides */
		CHECK(mkdir("ladir", 0755) == 0);
		{
			int dfd = open("ladir", O_RDONLY | O_DIRECTORY);
			int cfd = open(".", O_RDONLY | O_DIRECTORY);
			CHECK(dfd >= 0 && cfd >= 0);
			if (dfd >= 0 && cfd >= 0) {
				CHECK(linkat(cfd, "la-src.txt", dfd, "inner.txt", 0) == 0);
				CHECK(stat("ladir/inner.txt", &s2) == 0);
				CHECK(s2.st_ino == st.st_ino);
				CHECK(unlink("ladir/inner.txt") == 0);
			}
			if (dfd >= 0) CHECK(close(dfd) == 0);
			if (cfd >= 0) CHECK(close(cfd) == 0);
		}
		CHECK(rmdir("ladir") == 0);
	} else {
		printf("note: linkat() not supported here (errno %d), success path skipped\n", errno);
	}

	/* [ENOENT] for a missing path1 and for the empty string */
	errno = 0;
	CHECK(linkat(AT_FDCWD, "la-nope", AT_FDCWD, "la-dst2.txt", 0) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(linkat(AT_FDCWD, "", AT_FDCWD, "la-dst2.txt", 0) == -1 && errno == ENOENT);

	/* [EBADF] a relative path against a descriptor that is not open */
	errno = 0;
	CHECK(linkat(4096, "la-src.txt", AT_FDCWD, "la-dst3.txt", 0) == -1 && errno == EBADF);
	CHECK(stat("la-dst3.txt", &st) == -1);
	errno = 0;
	CHECK(linkat(AT_FDCWD, "la-src.txt", 4096, "la-dst3.txt", 0) == -1 && errno == EBADF);

	CHECK(unlink("la-src.txt") == 0);

	/* N/A, with the reason: AT_SYMLINK_FOLLOW.  src/unistd/link.c's
	 * linkat() opens path1 with FILE_OPEN_REPARSE_POINT unconditionally
	 * and ignores flags outright, so it always implements the
	 * flag-clear behaviour ("a new link is created for the symbolic
	 * link path1 and not its target").  Distinguishing the two needs a
	 * symbolic link to exist in the first place, which cannot be created
	 * in this environment (the blocker differs by leg -- see the fence
	 * named above; on the Wine leg it is an unimplemented
	 * FSCTL_SET_REPARSE_POINT rather than a privilege) -- so
	 * the clause cannot be exercised here either way, and asserting the
	 * flag-clear branch alone would claim coverage this test does not
	 * have.  The [EINVAL] for an invalid flag value is left unasserted:
	 * linkat() validates nothing, but linkat's [EINVAL] is a may-fail,
	 * unlike the shall-fail unlinkat() now enforces in
	 * test_unlinkat() above. */
}

/* ==================================================================
 * <unistd.h> header content -- the symbolic constants POSIX requires
 * this header to define.  Audit group U (XBD header contents); see
 * test/POSIX-COVERAGE.md "XBD header contents (group U)".
 *
 * These five fences are the largest single gap group U found, and the
 * one that bears most directly on what this libc exists for: autoconf
 * and gnulib probe _PC_NAME_MAX, _SC_SYMLOOP_MAX, _SC_IOV_MAX and
 * _SC_GETPW_R_SIZE_MAX as a matter of routine, and coreutils' stty
 * needs _POSIX_VDISABLE.
 * ================================================================== */

#if 0 /* UNIMPL: unistd.h.html DESCRIPTION: "The <unistd.h> header shall
	define the following symbolic constants for sysconf():", followed
	by a list of 125 _SC_* names.  The sentence is unconditional: no
	option-group margin marker guards the list, and an implementation
	that does not support an option still has to define that option's
	_SC_ name so sysconf() has something to answer -1 about.  ntlibc
	defines 15 of the 125; the other 110 are absent from include/
	altogether (triage: ABSENT).  Nothing records this --
	test/POSIX-GAP-ACCOUNTING.md enumerates the 1177 function
	interfaces and a symbolic constant is not one of them, and
	include/unistd.h's banner does not mention the omission.

	ACCEPTANCE CRITERION, which for this list is BOTH halves and not
	just the #define: sysconf.html specifies "[EINVAL] The value of
	the name argument is invalid" for an *invalid* name, and every
	name on this list is valid by definition of being on it.  So a
	definition alone would move the failure rather than remove it --
	src/unistd/sysconf.c's `default: errno = EINVAL; return -1` would
	then answer "that name does not exist" for a name <unistd.h>
	itself mandates, and a caller cannot distinguish that from a real
	rejection.  That is the "declared but unimplemented" trap in its
	exact form: the symbol appears, the consumer's configure test
	passes, and the consumer stands down its own replacement.  Where
	an option genuinely is unsupported the truthful answer is -1 with
	errno UNCHANGED, which sysconf.html permits ("If the variable
	corresponding to name has no limit ... sysconf() shall return -1
	without changing errno"), and which the assertion below accepts.

	Observed today: fails to COMPILE, "'_SC_2_CHAR_TERM' undeclared"
	(verified by un-fencing and building with x86_64-win32-tcc; a
	compile-time failure, so no Wine-vs-real-NT uncertainty). */
static void test_unistd_sysconf_names(void)
{
	static const int names[] = {
		_SC_2_CHAR_TERM, _SC_2_C_BIND, _SC_2_C_DEV, _SC_2_FORT_DEV,
		_SC_2_FORT_RUN, _SC_2_LOCALEDEF, _SC_2_PBS, _SC_2_PBS_ACCOUNTING,
		_SC_2_PBS_CHECKPOINT, _SC_2_PBS_LOCATE, _SC_2_PBS_MESSAGE,
		_SC_2_PBS_TRACK, _SC_2_SW_DEV, _SC_2_UPE, _SC_2_VERSION,
		_SC_ADVISORY_INFO, _SC_AIO_LISTIO_MAX, _SC_AIO_MAX,
		_SC_AIO_PRIO_DELTA_MAX, _SC_ARG_MAX, _SC_ASYNCHRONOUS_IO,
		_SC_ATEXIT_MAX, _SC_BARRIERS, _SC_BC_BASE_MAX, _SC_BC_DIM_MAX,
		_SC_BC_SCALE_MAX, _SC_BC_STRING_MAX, _SC_CHILD_MAX, _SC_CLK_TCK,
		_SC_CLOCK_SELECTION, _SC_COLL_WEIGHTS_MAX, _SC_CPUTIME,
		_SC_DELAYTIMER_MAX, _SC_EXPR_NEST_MAX, _SC_FSYNC, _SC_GETGR_R_SIZE_MAX,
		_SC_GETPW_R_SIZE_MAX, _SC_HOST_NAME_MAX, _SC_IOV_MAX, _SC_IPV6,
		_SC_JOB_CONTROL, _SC_LINE_MAX, _SC_LOGIN_NAME_MAX, _SC_MAPPED_FILES,
		_SC_MEMLOCK, _SC_MEMLOCK_RANGE, _SC_MEMORY_PROTECTION,
		_SC_MESSAGE_PASSING, _SC_MONOTONIC_CLOCK, _SC_MQ_OPEN_MAX,
		_SC_MQ_PRIO_MAX, _SC_NGROUPS_MAX, _SC_OPEN_MAX, _SC_PAGESIZE,
		_SC_PAGE_SIZE, _SC_PRIORITIZED_IO, _SC_PRIORITY_SCHEDULING,
		_SC_RAW_SOCKETS, _SC_READER_WRITER_LOCKS, _SC_REALTIME_SIGNALS,
		_SC_REGEXP, _SC_RE_DUP_MAX, _SC_RTSIG_MAX, _SC_SAVED_IDS,
		_SC_SEMAPHORES, _SC_SEM_NSEMS_MAX, _SC_SEM_VALUE_MAX,
		_SC_SHARED_MEMORY_OBJECTS, _SC_SHELL, _SC_SIGQUEUE_MAX, _SC_SPAWN,
		_SC_SPIN_LOCKS, _SC_SPORADIC_SERVER, _SC_SS_REPL_MAX, _SC_STREAM_MAX,
		_SC_SYMLOOP_MAX, _SC_SYNCHRONIZED_IO, _SC_THREADS,
		_SC_THREAD_ATTR_STACKADDR, _SC_THREAD_ATTR_STACKSIZE,
		_SC_THREAD_CPUTIME, _SC_THREAD_DESTRUCTOR_ITERATIONS,
		_SC_THREAD_KEYS_MAX, _SC_THREAD_PRIORITY_SCHEDULING,
		_SC_THREAD_PRIO_INHERIT, _SC_THREAD_PRIO_PROTECT,
		_SC_THREAD_PROCESS_SHARED, _SC_THREAD_ROBUST_PRIO_INHERIT,
		_SC_THREAD_ROBUST_PRIO_PROTECT, _SC_THREAD_SAFE_FUNCTIONS,
		_SC_THREAD_SPORADIC_SERVER, _SC_THREAD_STACK_MIN,
		_SC_THREAD_THREADS_MAX, _SC_TIMEOUTS, _SC_TIMERS, _SC_TIMER_MAX,
		_SC_TRACE, _SC_TRACE_EVENT_FILTER, _SC_TRACE_EVENT_NAME_MAX,
		_SC_TRACE_INHERIT, _SC_TRACE_LOG, _SC_TRACE_NAME_MAX,
		_SC_TRACE_SYS_MAX, _SC_TRACE_USER_EVENT_MAX, _SC_TTY_NAME_MAX,
		_SC_TYPED_MEMORY_OBJECTS, _SC_TZNAME_MAX, _SC_V6_ILP32_OFF32,
		_SC_V6_ILP32_OFFBIG, _SC_V6_LP64_OFF64, _SC_V6_LPBIG_OFFBIG,
		_SC_V7_ILP32_OFF32, _SC_V7_ILP32_OFFBIG, _SC_V7_LP64_OFF64,
		_SC_V7_LPBIG_OFFBIG, _SC_VERSION, _SC_XOPEN_CRYPT, _SC_XOPEN_ENH_I18N,
		_SC_XOPEN_REALTIME, _SC_XOPEN_REALTIME_THREADS, _SC_XOPEN_SHM,
		_SC_XOPEN_STREAMS, _SC_XOPEN_UNIX, _SC_XOPEN_UUCP, _SC_XOPEN_VERSION,
	};
	size_t i, j;

	/* Selectors for a switch must be distinct to be usable at all. */
	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		for (j = i + 1; j < sizeof names / sizeof names[0]; j++)
			CHECK(names[i] != names[j]);

	/* A mandated name is never an invalid name.  -1 is a legitimate
	 * answer ("no limit", or "option not supported"), but only with
	 * errno left alone. */
	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		long v;
		errno = 0;
		v = sysconf(names[i]);
		CHECK(!(v == -1 && errno == EINVAL));
	}
}
#endif

#if 0 /* UNIMPL: unistd.h.html DESCRIPTION: "The <unistd.h> header shall
	define the following symbolic constants for pathconf():", followed
	by a list of 21 _PC_* names.  Unconditional, same as the sysconf
	list.  ntlibc defines 9; _PC_2_SYMLINKS, _PC_ALLOC_SIZE_MIN,
	_PC_ASYNC_IO, _PC_FILESIZEBITS, _PC_PRIO_IO, _PC_REC_INCR_XFER_SIZE,
	_PC_REC_MAX_XFER_SIZE, _PC_REC_MIN_XFER_SIZE, _PC_REC_XFER_ALIGN,
	_PC_SYMLINK_MAX, _PC_SYNC_IO and _PC_TIMESTAMP_RESOLUTION are
	absent (triage: ABSENT).  gnulib and autoconf probe
	_PC_NAME_MAX/_PC_PATH_MAX (both present) but also _PC_SYMLINK_MAX,
	and a pathname-length-aware consumer that asks for
	_PC_FILESIZEBITS gets a compile error rather than an answer.

	ACCEPTANCE CRITERION: the definitions, plus the pathconf()/
	fpathconf() agreement the nine present names already hold to in
	test_fpathconf() above.  Unlike the _SC_ list this one does NOT
	require every name to be answerable: fpathconf.html makes
	"[EINVAL] The implementation does not support an association of
	the variable name with the specified file" a *may fail*, so
	rejecting, say, _PC_PRIO_IO for a regular file is conforming --
	what is not conforming is the name not existing.  The assertion
	below is written to that boundary: whatever pathconf() decides, it
	must decide the SAME thing through both entry points.

	Observed today: fails to COMPILE, "'_PC_2_SYMLINKS' undeclared". */
static void test_unistd_pathconf_names(void)
{
	static const int names[] = {
		_PC_2_SYMLINKS, _PC_ALLOC_SIZE_MIN, _PC_ASYNC_IO, _PC_CHOWN_RESTRICTED,
		_PC_FILESIZEBITS, _PC_LINK_MAX, _PC_MAX_CANON, _PC_MAX_INPUT,
		_PC_NAME_MAX, _PC_NO_TRUNC, _PC_PATH_MAX, _PC_PIPE_BUF, _PC_PRIO_IO,
		_PC_REC_INCR_XFER_SIZE, _PC_REC_MAX_XFER_SIZE, _PC_REC_MIN_XFER_SIZE,
		_PC_REC_XFER_ALIGN, _PC_SYMLINK_MAX, _PC_SYNC_IO,
		_PC_TIMESTAMP_RESOLUTION, _PC_VDISABLE,
	};
	size_t i, j;
	int fd;

	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		for (j = i + 1; j < sizeof names / sizeof names[0]; j++)
			CHECK(names[i] != names[j]);

	fd = open("upc.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);
	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		long a, b;
		int ea, eb;
		errno = 0;
		a = pathconf("upc.txt", names[i]);
		ea = errno;
		errno = 0;
		b = fpathconf(fd, names[i]);
		eb = errno;
		CHECK(a == b);
		CHECK(ea == eb);
	}
	CHECK(close(fd) == 0);
	CHECK(unlink("upc.txt") == 0);
}
#endif

#if 0 /* UNIMPL: unistd.h.html DESCRIPTION: "The <unistd.h> header shall
	define the following symbolic constants for the confstr()
	function:", followed by a list of 31 _CS_* names.  ntlibc defines
	exactly one of them, _CS_PATH; the other 30 -- the
	_CS_POSIX_V6_ and _CS_POSIX_V7_ programming-model CFLAGS/LDFLAGS/LIBS
	triples, _CS_POSIX_V7_THREADS_*, the two _CS_*_WIDTH_RESTRICTED_ENVS
	and _CS_V6_ENV/_CS_V7_ENV -- are absent (triage: ABSENT).  These
	are what a `getconf`-driven build system asks for when it wants the
	compiler flags for a programming model, which is precisely the
	bootstrap situation this libc is a target of.

	ACCEPTANCE CRITERION, deliberately narrow: THE DEFINITIONS ONLY.
	This fence claims nothing about what confstr() should return for
	them, and that restraint is not tidiness -- confstr()'s answers are
	entangled with an already-fenced BUG (see this file's test_confstr
	and POSIX-COVERAGE.md's "confstr() reports success for an invalid
	name": an unrecognized name returns 1 with errno untouched instead
	of 0 with [EINVAL]).  While that stands there is no assertion that
	can tell "recognized, empty value" from "unrecognized", so any
	claim about the values would be built on another agent's open
	defect.  When that BUG is fixed, the natural follow-on is that
	confstr() must not report these 30 as invalid; that is a separate
	fence for whoever fixes it, not this one.  Do not read an
	un-fencing of this test as acceptance that confstr() answers them.

	Observed today: fails to COMPILE, "'_CS_POSIX_V6_ILP32_OFF32_CFLAGS'
	undeclared". */
static void test_unistd_confstr_names(void)
{
	static const int names[] = {
		_CS_PATH,
		_CS_PATH, _CS_POSIX_V6_ILP32_OFF32_CFLAGS,
		_CS_POSIX_V6_ILP32_OFF32_LDFLAGS, _CS_POSIX_V6_ILP32_OFF32_LIBS,
		_CS_POSIX_V6_ILP32_OFFBIG_CFLAGS, _CS_POSIX_V6_ILP32_OFFBIG_LDFLAGS,
		_CS_POSIX_V6_ILP32_OFFBIG_LIBS, _CS_POSIX_V6_LP64_OFF64_CFLAGS,
		_CS_POSIX_V6_LP64_OFF64_LDFLAGS, _CS_POSIX_V6_LP64_OFF64_LIBS,
		_CS_POSIX_V6_LPBIG_OFFBIG_CFLAGS, _CS_POSIX_V6_LPBIG_OFFBIG_LDFLAGS,
		_CS_POSIX_V6_LPBIG_OFFBIG_LIBS, _CS_POSIX_V6_WIDTH_RESTRICTED_ENVS,
		_CS_POSIX_V7_ILP32_OFF32_CFLAGS, _CS_POSIX_V7_ILP32_OFF32_LDFLAGS,
		_CS_POSIX_V7_ILP32_OFF32_LIBS, _CS_POSIX_V7_ILP32_OFFBIG_CFLAGS,
		_CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS, _CS_POSIX_V7_ILP32_OFFBIG_LIBS,
		_CS_POSIX_V7_LP64_OFF64_CFLAGS, _CS_POSIX_V7_LP64_OFF64_LDFLAGS,
		_CS_POSIX_V7_LP64_OFF64_LIBS, _CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS,
		_CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS, _CS_POSIX_V7_LPBIG_OFFBIG_LIBS,
		_CS_POSIX_V7_THREADS_CFLAGS, _CS_POSIX_V7_THREADS_LDFLAGS,
		_CS_POSIX_V7_WIDTH_RESTRICTED_ENVS, _CS_V6_ENV, _CS_V7_ENV,
	};
	size_t i, j;

	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		for (j = i + 1; j < sizeof names / sizeof names[0]; j++)
			CHECK(names[i] != names[j]);

	/* "suitable for use in #if preprocessing directives" is not
	 * claimed for _CS_ names by the standard, so the only structural
	 * property to assert is that each is an integer constant
	 * expression usable as a confstr() selector, which the array
	 * initialiser above already forces. */
	CHECK(sizeof names / sizeof names[0] == 31);
}
#endif

#if 0 /* UNIMPL: unistd.h.html "Constants for Options and Option Groups"
	gives thirteen constants the wording "This symbol shall always be
	set to the value 200809L" -- _POSIX_ASYNCHRONOUS_IO,
	_POSIX_BARRIERS, _POSIX_CLOCK_SELECTION, _POSIX_MAPPED_FILES,
	_POSIX_MEMORY_PROTECTION, _POSIX_READER_WRITER_LOCKS,
	_POSIX_REALTIME_SIGNALS, _POSIX_SEMAPHORES, _POSIX_SPIN_LOCKS,
	_POSIX_THREADS, _POSIX_THREAD_SAFE_FUNCTIONS, _POSIX_TIMEOUTS and
	_POSIX_TIMERS.  "Always" is the operative word: unlike every other
	entry in that section, which the same page introduces with "The
	following symbolic constants, IF DEFINED in <unistd.h>, shall have
	a value of -1, 0, or greater", these thirteen are not optional to
	define.  ntlibc defines none of the thirteen, and no _POSIX_* or
	_XOPEN_* option constant at all beyond _POSIX_VERSION and
	_POSIX2_VERSION.  Triage: ABSENT.

	THIS FENCE'S ACCEPTANCE CRITERION IS NOT "ADD A #define", and that
	is why it is a clause of its own rather than part of the _SC_/_PC_/
	_CS_ fences above.  The value 200809L is a compile-time PROMISE to
	the application that the option is present -- that is the entire
	purpose of a constant an application may test with #if.  Seven of
	the thirteen are thread-related and the pthread family is a
	recorded absence (test/POSIX-GAP-ACCOUNTING.md); defining
	_POSIX_THREADS as 200809L with no threads behind it would be a
	false claim, and strictly worse than the omission, because an
	application that believes it cannot be corrected at runtime.  So
	the gap here is the OPTION, not the constant, and closing it means
	implementing the option.  What the omission costs today is
	smaller but real and previously unrecorded: an application asking
	"#ifdef _POSIX_TIMERS" gets the same silence from a libc that has
	clock_gettime()/clock_nanosleep() as from one that has nothing.

	Observed today: fails to COMPILE, "'_POSIX_ASYNCHRONOUS_IO'
	undeclared". */
static void test_unistd_mandatory_option_constants(void)
{
	static const long always[] = {
		_POSIX_ASYNCHRONOUS_IO, _POSIX_BARRIERS, _POSIX_CLOCK_SELECTION,
		_POSIX_MAPPED_FILES, _POSIX_MEMORY_PROTECTION,
		_POSIX_READER_WRITER_LOCKS, _POSIX_REALTIME_SIGNALS, _POSIX_SEMAPHORES,
		_POSIX_SPIN_LOCKS, _POSIX_THREADS, _POSIX_THREAD_SAFE_FUNCTIONS,
		_POSIX_TIMEOUTS, _POSIX_TIMERS,
	};
	size_t i;

	for (i = 0; i < sizeof always / sizeof always[0]; i++)
		CHECK(always[i] == 200809L);

	/* "The values shall be suitable for use in #if preprocessing
	 * directives" -- the property that makes the promise usable. */
#if defined(_POSIX_THREADS) && _POSIX_THREADS >= 200809L
	CHECK(1);
#else
	CHECK(0);
#endif
}
#endif

#if 0 /* UNIMPL: unistd.h.html, "Constants for Functions": "_POSIX_VDISABLE
	This symbol shall be defined to be the value of a character that
	shall disable terminal special character handling as described in
	Special Control Characters.  This symbol shall always be set to a
	value other than -1."  Mandatory in its own right -- it is not in
	the options section at all, and carries no option-group marker.
	ntlibc does not define it anywhere in include/ (triage: ABSENT),
	even though it already answers pathconf(_PC_VDISABLE) with 0
	(src/unistd/sysconf.c) and <termios.h> is implemented and audited
	(group A).  So the value this constant would have to carry already
	exists inside the library; only the constant naming it is missing.

	Consumer impact, and the reason this one is called out separately
	from the option constants above: coreutils' stty reads
	_POSIX_VDISABLE to print and set "undef" for a special character,
	and it is used at compile time, so its absence is a build failure
	rather than a degraded answer.

	ACCEPTANCE CRITERION: the definition, agreeing with what
	pathconf(_PC_VDISABLE) already reports -- POSIX has the two name
	the same thing, so a definition that disagreed with the running
	answer would be a new defect rather than a fix.

	Observed today: fails to COMPILE, "'_POSIX_VDISABLE' undeclared". */
static void test_unistd_posix_vdisable(void)
{
	/* "shall always be set to a value other than -1" */
	CHECK(_POSIX_VDISABLE != -1);

	/* pathconf.html: _PC_VDISABLE is "the value of the character that
	 * disables terminal special characters" for the file -- the same
	 * character this constant names. */
	errno = 0;
	CHECK(pathconf(".", _PC_VDISABLE) == _POSIX_VDISABLE);
	CHECK(errno == 0);

	/* Usable at compile time, which is how stty uses it. */
#if defined(_POSIX_VDISABLE) && _POSIX_VDISABLE != -1
	CHECK(1);
#else
	CHECK(0);
#endif
}
#endif

int main(void)
{
	char tmpl[] = "posixunistd-XXXXXX";
	char *dir = mkdtemp(tmpl);
	char origcwd[4096];

	CHECK(getcwd(origcwd, sizeof origcwd) == origcwd);
	CHECK(dir == tmpl);
	if (!dir) return 1;
	CHECK(chdir(dir) == 0);

	test_open_umask_bug();
	test_chmod_owner_can_always_chmod_readonly();
	test_utimensat_owner_can_touch_readonly();
	test_chmod_utimensat_attr_state_after_failure();
	test_dup2_self_preserves_cloexec();
	test_fcntl_dupfd_lowest();
	test_fcntl_setfl_ignores_accmode();
	test_fcntl_locks_are_noops();
	test_access_trailing_slash_enotdir();
	test_rename_same_file_noop();
	test_rename_new_dir_old_file_eisdir();
	test_rename_onto_nonempty_dir();
	test_creat_truncates_existing();
	test_unlink_open_file_stays_usable();
	test_lseek_seek_cur_negative();
	test_getcwd_off_by_one();
	test_stat_ino_distinct();
	test_stat_pipe_ino_distinct();
	test_stat_pipe_vs_console_distinct();
	test_stat_mtime_after_write();
	test_sysconf_child_max();
	test_pipe_ends_independent();
	test_ttyname_r_erange();
	test_stat_dir_size_is_zero();
	test_kill_neg1_reaches_self();
	test_kill_zero_is_own_group_of_one();
	test_access_real_effective_uid_identical();
	test_utimes();
	test_fpathconf();
	test_readlink();
	test_unlinkat();
	test_mkdirat();
	test_mkfifo_mknod_stubs();
	test_confstr();
	test_swab();
	test_sync();
	test_getlogin();
	test_id_session_stubs();
	test_linkat();

	CHECK(chdir(origcwd) == 0);
	CHECK(rmdir(dir) == 0);

	if (!fails) printf("posix-unistd: all tests passed\n");
	return fails != 0;
}
