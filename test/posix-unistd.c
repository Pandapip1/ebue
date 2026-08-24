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

/* sys_stat.h.html: S_IRUSR/S_IRGRP/S_IROTH (0444) "read permission" for
 * owner/group/other. src/stat/chmod.c's chmod_handle() comment: "chmod
 * can only express one thing on NTFS: whether the file is read-only" --
 * it tests "mode & 0222" and flips FILE_ATTRIBUTE_READONLY, the only
 * readability-adjacent bit NTFS exposes without ACL surgery ntlibc does
 * not do.  There is no NTFS attribute for "unreadable but not
 * read-only", so the read bits src/stat/stat.c's mode_from_attrs()
 * synthesizes (0444, always set) cannot be cleared by chmod() at all --
 * confirmed live below rather than asserted from the source comment. */
#if 0 /* N/A: chmod.html DESCRIPTION "set the file permission bits ...
       * to the value contained in mode" -- but chmod(path, 0) here
       * leaves S_IRUSR|S_IRGRP|S_IROTH set anyway.  NTFS has exactly
       * one relevant attribute (FILE_ATTRIBUTE_READONLY, mapped from
       * mode&0222); there is no "deny read" attribute a chmod-only
       * implementation can flip without also doing full ACL editing
       * (DENY ACEs), which ntlibc's chmod() does not attempt. */
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
#if 0 /* N/A: chmod.html says mode's 0111 bits become the new S_IX{USR,GRP,OTH}
       * bits; here chmod(0000) on a .exe leaves them set, and
       * chmod(0777) on a .txt cannot set them -- neither is
       * observable because NT has no execute-permission attribute to
       * write, only the filename ntlibc already used to fake st_mode. */
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
#if 0 /* N/A: chmod.html says the individual mode bits requested become
       * the new mode; here S_IWGRP/S_IWOTH alone have the identical,
       * all-or-nothing effect as S_IWUSR|S_IWGRP|S_IWOTH together, and
       * st_mode afterwards never reflects which of the three bits was
       * actually asked for.  Mechanism: chmod_handle() in
       * src/stat/chmod.c tests "mode & 0222" as one aggregate boolean
       * against the single FILE_ATTRIBUTE_READONLY bit NTFS exposes;
       * there is no NTFS concept of group- or other-write distinct
       * from owner-write to store the difference in. */
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

/* kill.html ERRORS EPERM: "The process does not have permission to send
 * the signal to any receiving process" -- POSIX's own example is a
 * real/effective/saved-uid mismatch.  src/unistd/ids.c documents "There
 * is one user as far as this library is concerned": getuid(), geteuid()
 * and the saved set-uid this library never separately tracks are all
 * always 1000, and setuid()/seteuid()/setreuid() are no-ops that cannot
 * create a second identity.  There being only one uid, ever, on this
 * platform is what makes a uid-mismatch EPERM structurally impossible
 * here -- not merely hard to trigger under Wine, since real hardware
 * has the identical single-fixed-uid design. */
#if 0 /* N/A: kill.html ERRORS EPERM (uid mismatch case) -- ntlibc has
       * exactly one uid (1000, src/unistd/ids.c), always, on every NT
       * target it runs on; setuid()/seteuid()/setreuid() are no-ops,
       * so no process this library controls can ever have a differing
       * real/effective/saved uid to be checked against. */
static void test_kill_eperm_uid_mismatch(void)
{
	CHECK(getuid() == geteuid());
	CHECK(seteuid(0) == 0);		/* no-op; cannot actually change identity */
	CHECK(geteuid() == 1000);		/* still 1000 -- no second uid was created */
	errno = 0;
	CHECK(kill(getpid(), 0) == -1 && errno == EPERM);	/* never observable */
}
#endif

/* kill.html ERRORS EPERM: the other route to EPERM here is NT's own
 * process-protection ACLs, independent of ntlibc's uid model entirely
 * -- src/signal/signal.c's kill() maps NtOpenProcess's
 * STATUS_ACCESS_DENIED to EPERM.  On real Windows, pid 4 is always the
 * "System" process, protected against PROCESS_TERMINATE from an
 * unprivileged caller, so kill(4, 0) (the sig==0 existence/permission
 * probe) should fail EPERM there.  Under Wine there is no "System"
 * process at pid 4 to protect -- NtOpenProcess fails with an
 * invalid-CID status, which kill() maps to ESRCH instead -- so detect
 * that shape and skip the assertion rather than asserting either one
 * blindly (same detect-and-note pattern as test/unistd.c's read-only
 * unlink/chmod cases). CI runs a real-Windows leg, so this is not
 * merely Wine-hostile -- it does run for real there. */
static void test_kill_eperm_protected_process(void)
{
	errno = 0;
	if (kill((pid_t)4, 0) == -1 && errno == ESRCH) {
		printf("note: pid 4 is not a protected process here (Wine has no "
		       "\"System\" process); EPERM path not reachable\n");
		return;
	}
	CHECK(kill((pid_t)4, 0) == -1);
	CHECK(errno == EPERM);
}

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
	test_stat_mtime_after_write();
	test_sysconf_child_max();
	test_pipe_ends_independent();
	test_ttyname_r_erange();
	test_stat_dir_size_is_zero();
	test_kill_neg1_reaches_self();
	test_kill_zero_is_own_group_of_one();
	test_kill_eperm_protected_process();
	test_access_real_effective_uid_identical();

	CHECK(chdir(origcwd) == 0);
	CHECK(rmdir(dir) == 0);

	if (!fails) printf("posix-unistd: all tests passed\n");
	return fails != 0;
}
