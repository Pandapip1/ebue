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
	 * Wine also refuses a plain chmod() back from read-only (it maps
	 * FILE_WRITE_ATTRIBUTES to a Unix O_WRONLY open, which a read-only
	 * file's Unix mode 0444 does not permit); test/unistd.c's "chmod
	 * back" case works around that with fchmod on an O_RDONLY
	 * descriptor, which Wine's NtSetInformationFile does accept. */
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

	CHECK(chdir(origcwd) == 0);
	CHECK(rmdir(dir) == 0);

	if (!fails) printf("posix-unistd: all tests passed\n");
	return fails != 0;
}
