/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * unistd/stat/fcntl: open, read/write/lseek, dup, fcntl, stat, mkdir,
 * unlink, rename, link, access, chdir/getcwd, chmod, ftruncate,
 * utimensat, pipe, and the assorted small ones.  Everything happens in a
 * fresh mkdtemp directory that is removed at the end.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int read_all(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY);
	ssize_t r;
	if (fd < 0) return -1;
	r = read(fd, buf, n);
	close(fd);
	return (int)r;
}

int main(void)
{
	char tmpl[] = "unistdtest-XXXXXX";
	char *dir;
	char origcwd[4096];
	int fd, fd2, fd3;
	char buf[256];
	struct stat st;

	CHECK(getcwd(origcwd, sizeof origcwd) == origcwd);
	dir = mkdtemp(tmpl);
	CHECK(dir == tmpl);
	if (!dir) return 1;
	CHECK(chdir(dir) == 0);

	/* open: O_CREAT|O_EXCL, EEXIST on repeat */
	fd = open("a.txt", O_CREAT | O_EXCL | O_RDWR, 0644);
	CHECK(fd >= 0);
	errno = 0;
	CHECK(open("a.txt", O_CREAT | O_EXCL | O_RDWR, 0644) == -1 && errno == EEXIST);
	errno = 0;
	CHECK(open("nonexistent.txt", O_RDONLY) == -1 && errno == ENOENT);

	/* write/read/lseek */
	CHECK(write(fd, "hello world", 11) == 11);
	CHECK(lseek(fd, 0, SEEK_CUR) == 11);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	memset(buf, 0, sizeof buf);
	CHECK(read(fd, buf, sizeof buf) == 11);
	CHECK(!memcmp(buf, "hello world", 11));
	CHECK(read(fd, buf, sizeof buf) == 0);   /* EOF */
	CHECK(lseek(fd, 6, SEEK_SET) == 6);
	CHECK(read(fd, buf, 5) == 5 && !memcmp(buf, "world", 5));
	CHECK(lseek(fd, -5, SEEK_CUR) == 6);
	CHECK(lseek(fd, -1, SEEK_END) == 10);
	CHECK(read(fd, buf, 1) == 1 && buf[0] == 'd');
	CHECK(lseek(fd, 0, SEEK_END) == 11);
	errno = 0;
	CHECK(lseek(fd, -100, SEEK_SET) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(lseek(fd, 0, 99) == -1 && errno == EINVAL);
	/* seek past EOF then write: hole reads as zeros */
	CHECK(lseek(fd, 20, SEEK_SET) == 20);
	CHECK(write(fd, "X", 1) == 1);
	CHECK(fstat(fd, &st) == 0 && st.st_size == 21);
	CHECK(lseek(fd, 11, SEEK_SET) == 11);
	memset(buf, 0x55, sizeof buf);
	CHECK(read(fd, buf, 10) == 10);
	{
		int i, zeros = 1;
		for (i = 0; i < 9; i++) if (buf[i]) zeros = 0;
		CHECK(zeros && buf[9] == 'X');
	}
	/* pread/pwrite do not move the offset */
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(pwrite(fd, "J", 1, 4) == 1);
	CHECK(pread(fd, buf, 5, 0) == 5 && !memcmp(buf, "hellJ", 5));
	CHECK(lseek(fd, 0, SEEK_CUR) == 0);
	CHECK(pread(fd, buf, 10, 1000) == 0);
	CHECK(lseek(fd, 0, SEEK_CUR) == 0);
	/* pwrite beyond EOF extends the file but does not move the offset */
	CHECK(lseek(fd, 2, SEEK_SET) == 2);
	CHECK(pwrite(fd, "Z", 1, 30) == 1);
	CHECK(lseek(fd, 0, SEEK_CUR) == 2);
	CHECK(lseek(fd, 0, SEEK_END) == 31);
	/* pread then read: read continues from the original offset */
	CHECK(lseek(fd, 1, SEEK_SET) == 1);
	CHECK(pread(fd, buf, 3, 5) == 3);
	CHECK(read(fd, buf, 3) == 3 && !memcmp(buf, "ell", 3));
	CHECK(lseek(fd, 0, SEEK_CUR) == 4);
	/* pwrite on an O_APPEND fd: Linux ignores the offset and appends, and
	 * the position must still not move; we only require that it works and
	 * leaves the offset alone. */
	{
		int afd = open("a.txt", O_WRONLY | O_APPEND);
		CHECK(afd >= 0);
		CHECK(lseek(afd, 0, SEEK_SET) == 0);
		CHECK(pwrite(afd, "Q", 1, 3) == 1);
		CHECK(lseek(afd, 0, SEEK_CUR) == 0);
		CHECK(close(afd) == 0);
	}
	/* back to the 21-byte file the checks below expect */
	CHECK(ftruncate(fd, 21) == 0);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	/* fsync, isatty, ttyname */
	CHECK(fsync(fd) == 0);
	CHECK(fdatasync(fd) == 0);
	errno = 0;
	CHECK(isatty(fd) == 0 && errno == ENOTTY);
	errno = 0;
	CHECK(ttyname(fd) == 0 && errno == ENOTTY);
	CHECK(ttyname_r(fd, buf, sizeof buf) == ENOTTY);
	/* fstat on it */
	CHECK(fstat(fd, &st) == 0);
	CHECK(S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode));
	CHECK(st.st_size == 21);
	CHECK(st.st_nlink >= 1);
	CHECK(st.st_mode & 0200);
	CHECK(st.st_mtime > 1700000000);         /* after Nov 2023 */
	CHECK(st.st_mtime < 4102444800LL);       /* before 2100 */

	/* dup/dup2/dup3 share the offset */
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	fd2 = dup(fd);
	CHECK(fd2 >= 0 && fd2 != fd);
	CHECK(read(fd2, buf, 5) == 5);
	CHECK(lseek(fd, 0, SEEK_CUR) == 5);
	CHECK(dup2(fd, fd) == fd);
	CHECK(dup2(fd, 100) == 100);
	CHECK(lseek(100, 0, SEEK_CUR) == 5);
	CHECK(close(100) == 0);
	CHECK(dup2(fd, fd2) == fd2);             /* onto an open fd: closes it first */
	CHECK(lseek(fd2, 0, SEEK_CUR) == 5);
	errno = 0;
	CHECK(dup3(fd, fd, 0) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(dup2(fd, -1) == -1 && errno == EBADF);
	errno = 0;
	CHECK(dup(999) == -1 && errno == EBADF);
	CHECK(dup3(fd, 101, O_CLOEXEC) == 101);
	CHECK(fcntl(101, F_GETFD) == FD_CLOEXEC);
	CHECK(close(101) == 0);
	CHECK(close(fd2) == 0);
	errno = 0;
	CHECK(close(fd2) == -1 && errno == EBADF);

	/* fcntl */
	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(lseek(fd, 0, SEEK_CUR) == 5);       /* handle remade, position kept */
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDWR);
	CHECK(!(fcntl(fd, F_GETFL) & O_APPEND));
	fd2 = fcntl(fd, F_DUPFD, 50);
	CHECK(fd2 >= 50);
	CHECK(lseek(fd2, 0, SEEK_CUR) == 5);
	CHECK(fcntl(fd2, F_GETFD) == 0);
	CHECK(close(fd2) == 0);
	fd2 = fcntl(fd, F_DUPFD_CLOEXEC, 60);
	CHECK(fd2 >= 60 && fcntl(fd2, F_GETFD) == FD_CLOEXEC);
	CHECK(close(fd2) == 0);
	errno = 0;
	CHECK(fcntl(fd, 12345) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(fcntl(999, F_GETFD) == -1 && errno == EBADF);
	CHECK(close(fd) == 0);

	/* O_CLOEXEC at open */
	fd = open("a.txt", O_RDONLY | O_CLOEXEC);
	CHECK(fd >= 0 && fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
	CHECK(close(fd) == 0);

	/* O_TRUNC */
	fd = open("a.txt", O_WRONLY | O_TRUNC);
	CHECK(fd >= 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == 0);
	CHECK(write(fd, "abc", 3) == 3);
	CHECK(close(fd) == 0);
	CHECK(stat("a.txt", &st) == 0 && st.st_size == 3);

	/* O_APPEND: writes land at the end regardless of seek */
	fd = open("a.txt", O_WRONLY | O_APPEND);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_GETFL) & O_APPEND);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(write(fd, "def", 3) == 3);
	CHECK(close(fd) == 0);
	CHECK(read_all("a.txt", buf, sizeof buf) == 6 && !memcmp(buf, "abcdef", 6));

	/* creat */
	fd = creat("c.txt", 0644);
	CHECK(fd >= 0);
	CHECK(write(fd, "zz", 2) == 2);
	CHECK(close(fd) == 0);
	CHECK(stat("c.txt", &st) == 0 && st.st_size == 2);
	CHECK(unlink("c.txt") == 0);

	/* path handling: forward slashes, ./x, sub/../x */
	CHECK(mkdir("sub", 0755) == 0);
	fd = open("sub/inner.txt", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0);
	CHECK(write(fd, "in", 2) == 2);
	CHECK(close(fd) == 0);
	CHECK(stat("./sub/inner.txt", &st) == 0 && st.st_size == 2);
	CHECK(stat("sub/../a.txt", &st) == 0 && st.st_size == 6);
	CHECK(stat("./a.txt", &st) == 0 && st.st_size == 6);
	CHECK(stat("sub/", &st) == 0 && S_ISDIR(st.st_mode));
	CHECK(read_all("sub/../sub/inner.txt", buf, sizeof buf) == 2);
	CHECK(read_all("./sub/inner.txt", buf, sizeof buf) == 2);
	{
		char abs[4096 + 64];
		CHECK(getcwd(abs, 4096) != 0);
		strcat(abs, "/sub/inner.txt");
		CHECK(!strchr(abs, '\\'));
		CHECK(stat(abs, &st) == 0 && st.st_size == 2);
	}
	/* read on a directory fd is EISDIR; open O_DIRECTORY on a file fails */
	fd = open("sub", O_RDONLY);
	CHECK(fd >= 0);
	errno = 0;
	CHECK(read(fd, buf, 10) == -1 && errno == EISDIR);
	CHECK(fstat(fd, &st) == 0 && S_ISDIR(st.st_mode));
	CHECK(close(fd) == 0);
	errno = 0;
	CHECK(open("a.txt", O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR);
	/* Opening a directory for writing reports EISDIR -- but only on
	 * systems that reject the open itself.  Wine's server retries a
	 * write-access open of a directory read-only (server/fd.c, "if we
	 * tried to open a directory for write access, retry read-only"), and
	 * real Windows also hands back a usable handle here (measured on a
	 * windows-latest CI runner), so on both the open succeeds and the
	 * write is what reports EISDIR.  Accept either shape. */
	errno = 0;
	fd = open("sub", O_WRONLY);
	if (fd == -1) CHECK(errno == EISDIR);
	else {
		printf("note: open(dir, O_WRONLY) succeeded; checking write instead\n");
		errno = 0;
		CHECK(write(fd, "x", 1) == -1 && errno == EISDIR);
		CHECK(close(fd) == 0);
	}
	/* openat relative to a directory fd */
	fd = open("sub", O_RDONLY | O_DIRECTORY);
	CHECK(fd >= 0);
	fd2 = openat(fd, "inner.txt", O_RDONLY);
	CHECK(fd2 >= 0);
	CHECK(read(fd2, buf, 2) == 2 && !memcmp(buf, "in", 2));
	CHECK(close(fd2) == 0);
	CHECK(fstatat(fd, "inner.txt", &st, 0) == 0 && st.st_size == 2);
	errno = 0;
	CHECK(openat(fd, "missing", O_RDONLY) == -1 && errno == ENOENT);
	CHECK(close(fd) == 0);

	/* stat/lstat on directory and file */
	CHECK(stat("sub", &st) == 0 && S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode));
	CHECK(st.st_mode & 0100);
	CHECK(lstat("sub", &st) == 0 && S_ISDIR(st.st_mode));
	CHECK(lstat("a.txt", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 6);
	errno = 0;
	CHECK(stat("nope", &st) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(stat("nope/deeper", &st) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(lstat("nope", &st) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(fstat(999, &st) == -1 && errno == EBADF);
	CHECK(stat("/dev/null", &st) == 0 && S_ISCHR(st.st_mode));
	/* st_ino identical for the same file by different names */
	{
		struct stat s1, s2;
		CHECK(stat("a.txt", &s1) == 0 && stat("./sub/../a.txt", &s2) == 0);
		CHECK(s1.st_ino == s2.st_ino && s1.st_dev == s2.st_dev);
	}

	/* mkdir/rmdir */
	errno = 0;
	CHECK(mkdir("sub", 0755) == -1 && errno == EEXIST);
	errno = 0;
	CHECK(mkdir("a.txt", 0755) == -1 && errno == EEXIST);
	errno = 0;
	CHECK(mkdir("nope/sub2", 0755) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(rmdir("sub") == -1 && errno == ENOTEMPTY);
	errno = 0;
	CHECK(rmdir("nope") == -1 && errno == ENOENT);
	errno = 0;
	CHECK(rmdir("a.txt") == -1 && errno == ENOTDIR);
	errno = 0;
	CHECK(unlink("sub") == -1 && errno == EISDIR);
	CHECK(mkdir("sub/deep", 0755) == 0);
	CHECK(rmdir("sub/deep") == 0);
	CHECK(stat("sub/deep", &st) == -1);

	/* unlink */
	errno = 0;
	CHECK(unlink("nope") == -1 && errno == ENOENT);
	CHECK(unlink("sub/inner.txt") == 0);
	CHECK(stat("sub/inner.txt", &st) == -1);
	CHECK(rmdir("sub") == 0);
	CHECK(stat("sub", &st) == -1);

	/* rename, including over an existing file */
	fd = open("r1.txt", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0 && write(fd, "one", 3) == 3 && close(fd) == 0);
	fd = open("r2.txt", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0 && write(fd, "twotwo", 6) == 6 && close(fd) == 0);
	CHECK(rename("r1.txt", "r3.txt") == 0);
	CHECK(stat("r1.txt", &st) == -1 && stat("r3.txt", &st) == 0 && st.st_size == 3);
	CHECK(rename("r3.txt", "r2.txt") == 0);
	CHECK(stat("r3.txt", &st) == -1);
	CHECK(read_all("r2.txt", buf, sizeof buf) == 3 && !memcmp(buf, "one", 3));
	errno = 0;
	CHECK(rename("nope", "r4.txt") == -1 && errno == ENOENT);
	CHECK(mkdir("rd", 0755) == 0);
	CHECK(rename("rd", "rd2") == 0);
	CHECK(stat("rd2", &st) == 0 && S_ISDIR(st.st_mode));
	CHECK(rmdir("rd2") == 0);

	/* link: hard link shares data, nlink goes up */
	if (link("r2.txt", "l.txt") == 0) {
		CHECK(stat("l.txt", &st) == 0 && st.st_size == 3);
		CHECK(st.st_nlink == 2);
		{
			struct stat s2;
			CHECK(stat("r2.txt", &s2) == 0 && s2.st_ino == st.st_ino);
		}
		fd = open("l.txt", O_WRONLY | O_APPEND);
		CHECK(fd >= 0 && write(fd, "+", 1) == 1 && close(fd) == 0);
		CHECK(read_all("r2.txt", buf, sizeof buf) == 4);
		errno = 0;
		CHECK(link("r2.txt", "l.txt") == -1 && errno == EEXIST);
		CHECK(unlink("l.txt") == 0);
		CHECK(stat("r2.txt", &st) == 0 && st.st_nlink == 1);
	} else {
		printf("note: link() not supported here (errno %d), skipped\n", errno);
	}
	errno = 0;
	CHECK(link("nope", "l2.txt") == -1 && errno == ENOENT);
	CHECK(unlink("r2.txt") == 0);

	/* Lengths that do not fit a USHORT must fail, not wrap.  A
	 * UNICODE_STRING's Length and a reparse buffer's name lengths are
	 * USHORTs counting bytes, so a path or symlink target past ~32k code
	 * units used to narrow into a *shorter* name -- naming some other
	 * object entirely -- instead of being refused. */
	{
		char *big = malloc(40001);
		char cwdbefore[4096], cwdafter[4096];
		CHECK(big != 0);
		CHECK(getcwd(cwdbefore, sizeof cwdbefore) == cwdbefore);
		if (big) {
			memset(big, 'x', 40000);
			big[40000] = 0;
			errno = 0;
			CHECK(chdir(big) == -1 && errno == ENAMETOOLONG);
			CHECK(getcwd(cwdafter, sizeof cwdafter) == cwdafter);
			CHECK(!strcmp(cwdbefore, cwdafter));
			errno = 0;
			CHECK(symlink(big, "toolong.lnk") == -1 && errno == ENAMETOOLONG);
			/* and no half-made link left behind */
			CHECK(stat("toolong.lnk", &st) == -1);
			CHECK(lstat("toolong.lnk", &st) == -1);
			free(big);
		}
	}

	/* access */
	CHECK(access("a.txt", F_OK) == 0);
	CHECK(access("a.txt", R_OK) == 0);
	CHECK(access("a.txt", W_OK) == 0);
	CHECK(access("a.txt", R_OK | W_OK) == 0);
	errno = 0;
	CHECK(access("a.txt", X_OK) == -1 && errno == EACCES);
	errno = 0;
	CHECK(access("nope", F_OK) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(access("nope", R_OK) == -1 && errno == ENOENT);
	CHECK(access(".", X_OK) == 0);

	/* chmod read-only: stat loses write bits, W_OK fails, write-open EACCES */
	CHECK(chmod("a.txt", 0444) == 0);
	CHECK(stat("a.txt", &st) == 0 && !(st.st_mode & 0222) && (st.st_mode & 0444));
	errno = 0;
	CHECK(access("a.txt", W_OK) == -1 && errno == EACCES);
	CHECK(access("a.txt", R_OK) == 0);
	errno = 0;
	CHECK(open("a.txt", O_WRONLY) == -1 && errno == EACCES);
	errno = 0;
	CHECK(open("a.txt", O_RDWR) == -1 && errno == EACCES);
	fd = open("a.txt", O_RDONLY);
	CHECK(fd >= 0);
	CHECK(close(fd) == 0);
	/* chmod back.  Wine's server maps FILE_WRITE_ATTRIBUTES to a Unix
	 * O_WRONLY open (server/fd.c, FILE_UNIX_WRITE_ACCESS) and keeps
	 * read-only files at mode 0444, so under Wine any open asking for
	 * write attributes on a read-only file is refused with EACCES; real
	 * NT allows it.  Detect that and fall back to fchmod on a read-only
	 * descriptor, which Wine's NtSetInformationFile accepts. */
	{
		int wine_ro_quirk = 0;
		errno = 0;
		if (chmod("a.txt", 0644) == -1) {
			CHECK(errno == EACCES);
			wine_ro_quirk = 1;
			printf("note: chmod back from read-only refused (Wine); using fchmod on an O_RDONLY fd\n");
			fd = open("a.txt", O_RDONLY);
			CHECK(fd >= 0);
			CHECK(fchmod(fd, 0644) == 0);
			CHECK(close(fd) == 0);
		}
		CHECK(stat("a.txt", &st) == 0 && (st.st_mode & 0200));
		CHECK(access("a.txt", W_OK) == 0);
		fd = open("a.txt", wine_ro_quirk ? O_RDONLY : O_WRONLY);
		CHECK(fd >= 0);
		CHECK(fchmod(fd, 0444) == 0);
		CHECK(fstat(fd, &st) == 0 && !(st.st_mode & 0222));
		CHECK(fchmod(fd, 0644) == 0);
		CHECK(fstat(fd, &st) == 0 && (st.st_mode & 0200));
		CHECK(close(fd) == 0);
		errno = 0;
		CHECK(chmod("nope", 0644) == -1 && errno == ENOENT);
		/* a file created read-only via the mode argument */
		fd = open("ro.txt", O_CREAT | O_WRONLY, 0444);
		CHECK(fd >= 0);
		CHECK(write(fd, "r", 1) == 1);
		CHECK(close(fd) == 0);
		CHECK(stat("ro.txt", &st) == 0 && !(st.st_mode & 0222));
		errno = 0;
		CHECK(open("ro.txt", O_WRONLY) == -1 && errno == EACCES);
		/* unlink() of a read-only file is a separate Wine quirk from
		 * the chmod-by-path one wine_ro_quirk above detects (that one
		 * is fixed in src/stat/chmod.c's fchmodat() -- see
		 * test/posix-unistd.c's test_chmod_owner_can_always_chmod_readonly()
		 * -- but src/unistd/unlink.c still opens with
		 * FILE_WRITE_ATTRIBUTES unconditionally, so it still hits
		 * Wine's WA-open-denied-on-read-only-attribute refusal; real
		 * NT does not).  Detect unlink's own failure independently
		 * rather than reusing wine_ro_quirk, which no longer implies
		 * this one now that chmod is fixed. */
		errno = 0;
		if (unlink("ro.txt") == -1) {
			CHECK(errno == EACCES);
			fd = open("ro.txt", O_RDONLY);
			CHECK(fd >= 0 && fchmod(fd, 0644) == 0 && close(fd) == 0);
			CHECK(unlink("ro.txt") == 0);
		}
		CHECK(stat("ro.txt", &st) == -1);
	}
	/* umask */
	{
		mode_t o = umask(027);
		CHECK(umask(o) == 027);
		CHECK(umask(o) == o);
	}

	/* ftruncate grow and shrink */
	fd = open("a.txt", O_RDWR);
	CHECK(fd >= 0);
	CHECK(ftruncate(fd, 100) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == 100);
	CHECK(lseek(fd, 6, SEEK_SET) == 6);
	CHECK(read(fd, buf, 4) == 4 && !buf[0] && !buf[1] && !buf[2] && !buf[3]);
	CHECK(ftruncate(fd, 2) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == 2);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(read(fd, buf, sizeof buf) == 2 && !memcmp(buf, "ab", 2));
	errno = 0;
	CHECK(ftruncate(fd, -1) == -1 && errno == EINVAL);
	CHECK(close(fd) == 0);
	fd = open("a.txt", O_RDONLY);
	CHECK(fd >= 0);
	errno = 0;
	CHECK(ftruncate(fd, 1) == -1 && errno == EINVAL);
	CHECK(close(fd) == 0);
	errno = 0;
	CHECK(ftruncate(999, 0) == -1 && errno == EBADF);
	CHECK(truncate("a.txt", 5) == 0);
	CHECK(stat("a.txt", &st) == 0 && st.st_size == 5);
	errno = 0;
	CHECK(truncate("nope", 0) == -1 && errno == ENOENT);

	/* posix_fadvise: a no-op that validates fd/advice and returns an
	 * error number directly rather than -1/errno */
	fd = open("a.txt", O_RDWR);
	CHECK(fd >= 0);
	CHECK(posix_fadvise(fd, 0, 0, POSIX_FADV_NORMAL) == 0);
	CHECK(posix_fadvise(fd, 0, 100, POSIX_FADV_SEQUENTIAL) == 0);
	CHECK(posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0);
	CHECK(posix_fadvise(fd, 0, 0, 999) == EINVAL);
	CHECK(posix_fadvise(999, 0, 0, POSIX_FADV_NORMAL) == EBADF);

	/* posix_fallocate: really reserves storage and can grow the file */
	CHECK(posix_fallocate(fd, 0, 4096) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size >= 4096);
	CHECK(posix_fallocate(fd, 0, 1) == 0);   /* already covered: a no-op */
	CHECK(posix_fallocate(fd, -1, 1) == EINVAL);
	CHECK(close(fd) == 0);
	CHECK(posix_fallocate(999, 0, 1) == EBADF);
	{
		int p[2];
		CHECK(pipe(p) == 0);
		CHECK(posix_fallocate(p[0], 0, 1) == ESPIPE);
		CHECK(close(p[0]) == 0 && close(p[1]) == 0);
	}

	/* utimensat/utime/futimens set mtime, read back via stat */
	{
		struct timespec ts[2];
		struct utimbuf ub;
		ts[0].tv_sec = 1000000000; ts[0].tv_nsec = 0;
		ts[1].tv_sec = 1234567890; ts[1].tv_nsec = 500000000;
		CHECK(utimensat(AT_FDCWD, "a.txt", ts, 0) == 0);
		CHECK(stat("a.txt", &st) == 0);
		CHECK(st.st_mtime == 1234567890);
		CHECK(st.st_mtim.tv_nsec == 500000000);
		CHECK(st.st_atime == 1000000000);
		/* UTIME_OMIT leaves mtime alone */
		ts[0].tv_sec = 1100000000; ts[0].tv_nsec = 0;
		ts[1].tv_nsec = UTIME_OMIT;
		CHECK(utimensat(AT_FDCWD, "a.txt", ts, 0) == 0);
		CHECK(stat("a.txt", &st) == 0 && st.st_mtime == 1234567890 && st.st_atime == 1100000000);
		ub.actime = 1300000000; ub.modtime = 1400000000;
		CHECK(utime("a.txt", &ub) == 0);
		CHECK(stat("a.txt", &st) == 0 && st.st_mtime == 1400000000 && st.st_atime == 1300000000);
		/* UTIME_NOW / NULL: back to roughly now */
		CHECK(utimensat(AT_FDCWD, "a.txt", 0, 0) == 0);
		CHECK(stat("a.txt", &st) == 0 && st.st_mtime > 1700000000);
		fd = open("a.txt", O_RDWR);
		CHECK(fd >= 0);
		ts[0].tv_nsec = UTIME_OMIT;
		ts[1].tv_sec = 1500000000; ts[1].tv_nsec = 0;
		CHECK(futimens(fd, ts) == 0);
		CHECK(fstat(fd, &st) == 0 && st.st_mtime == 1500000000);
		CHECK(close(fd) == 0);
		errno = 0;
		CHECK(utimensat(AT_FDCWD, "nope", ts, 0) == -1 && errno == ENOENT);
		errno = 0;
		CHECK(utimensat(AT_FDCWD, "/dev/null/invalid", ts, 0) == -1 && errno == ENOTDIR);

		/* futimesat: same as utimensat/timeval AT_FDCWD, timeval usec
		 * resolution */
		{
			struct timeval tv[2];
			tv[0].tv_sec = 1600000000; tv[0].tv_usec = 0;
			tv[1].tv_sec = 1650000000; tv[1].tv_usec = 250000;
			CHECK(futimesat(AT_FDCWD, "a.txt", tv) == 0);
			CHECK(stat("a.txt", &st) == 0);
			CHECK(st.st_mtime == 1650000000);
			CHECK(st.st_mtim.tv_nsec == 250000000);
			CHECK(st.st_atime == 1600000000);
			errno = 0;
			CHECK(futimesat(AT_FDCWD, "nope", tv) == -1 && errno == ENOENT);
		}
	}

	/* chdir/getcwd round trip */
	{
		char cwd[4096], cwd2[4096], small[4];
		char *p;
		CHECK(getcwd(cwd, sizeof cwd) == cwd);
		CHECK(strlen(cwd) > strlen(origcwd));
		CHECK(!strncmp(cwd, origcwd, strlen(origcwd)));
		CHECK(!strcmp(cwd + strlen(origcwd) + 1, dir));
		CHECK(!strchr(cwd, '\\'));
		CHECK(cwd[strlen(cwd) - 1] != '/');
		errno = 0;
		CHECK(getcwd(small, sizeof small) == 0 && errno == ERANGE);
		errno = 0;
		CHECK(getcwd(cwd2, 0) == 0 && errno == EINVAL);
		p = getcwd(0, 0);
		CHECK(p != 0);
		if (p) { CHECK(!strcmp(p, cwd)); free(p); }
		p = getcwd(0, strlen(cwd) + 1);
		CHECK(p != 0);
		if (p) { CHECK(!strcmp(p, cwd)); free(p); }
		errno = 0;
		CHECK(getcwd(0, 2) == 0 && errno == ERANGE);
		p = get_current_dir_name();
		CHECK(p != 0);
		if (p) { CHECK(!strcmp(p, cwd)); free(p); }
		CHECK(mkdir("cd", 0755) == 0);
		CHECK(chdir("cd") == 0);
		CHECK(getcwd(cwd2, sizeof cwd2) == cwd2);
		CHECK(strlen(cwd2) == strlen(cwd) + 3 && !strcmp(cwd2 + strlen(cwd), "/cd"));
		CHECK(chdir("..") == 0);
		CHECK(getcwd(cwd2, sizeof cwd2) == cwd2 && !strcmp(cwd2, cwd));
		CHECK(chdir("./cd/..") == 0);
		CHECK(getcwd(cwd2, sizeof cwd2) == cwd2 && !strcmp(cwd2, cwd));
		/* absolute chdir */
		CHECK(chdir(origcwd) == 0);
		CHECK(getcwd(cwd2, sizeof cwd2) == cwd2 && !strcmp(cwd2, origcwd));
		CHECK(chdir(cwd) == 0);
		CHECK(getcwd(cwd2, sizeof cwd2) == cwd2 && !strcmp(cwd2, cwd));
		/* chdir.html ERRORS: [ENOTDIR] when a component of the path
		 * prefix -- or the name itself -- is an existing non-directory,
		 * [ENOENT] when it simply is not there.  NT reports the two
		 * prefix cases identically (STATUS_OBJECT_PATH_NOT_FOUND for
		 * both, measured on Windows 11 Pro 22621 / NTFS), so these four
		 * mirror that probe exactly: they are what tells us if a
		 * different Windows build ever disagrees. */
		errno = 0;
		CHECK(chdir("nope") == -1 && errno == ENOENT);
		errno = 0;
		CHECK(chdir("a.txt") == -1 && errno == ENOTDIR);
		errno = 0;
		CHECK(chdir("a.txt/below") == -1 && errno == ENOTDIR);
		errno = 0;
		CHECK(chdir("nope/below") == -1 && errno == ENOENT);
		errno = 0;
		CHECK(chdir("") == -1 && errno == ENOENT);
		/* and a plain success still succeeds after all of that */
		CHECK(chdir(".") == 0);
		CHECK(getcwd(cwd2, sizeof cwd2) == cwd2 && !strcmp(cwd2, cwd));
		/* fchdir */
		fd = open("cd", O_RDONLY | O_DIRECTORY);
		CHECK(fd >= 0);
		CHECK(fchdir(fd) == 0);
		CHECK(getcwd(cwd2, sizeof cwd2) == cwd2 && !strcmp(cwd2 + strlen(cwd), "/cd"));
		CHECK(close(fd) == 0);
		CHECK(chdir("..") == 0);
		CHECK(rmdir("cd") == 0);
	}

	/* pipe */
	{
		int p[2];
		CHECK(pipe(p) == 0);
		CHECK(p[0] >= 0 && p[1] >= 0 && p[0] != p[1]);
		CHECK((fcntl(p[0], F_GETFL) & O_ACCMODE) == O_RDONLY);
		CHECK((fcntl(p[1], F_GETFL) & O_ACCMODE) == O_WRONLY);
		CHECK(write(p[1], "pipe data", 9) == 9);
		memset(buf, 0, sizeof buf);
		CHECK(read(p[0], buf, 4) == 4 && !memcmp(buf, "pipe", 4));
		CHECK(read(p[0], buf, sizeof buf) == 5 && !memcmp(buf, " data", 5));
		errno = 0;
		CHECK(lseek(p[0], 0, SEEK_CUR) == -1 && errno == ESPIPE);
		errno = 0;
		CHECK(pread(p[0], buf, 1, 0) == -1 && errno == ESPIPE);
		errno = 0;
		CHECK(pwrite(p[1], "x", 1, 0) == -1 && errno == ESPIPE);
		CHECK(fstat(p[0], &st) == 0 && S_ISFIFO(st.st_mode));
		/* fsync.html: "[EINVAL] fildes is bound to a special file
		 * which does not support synchronization." A pipe is exactly
		 * that -- src/unistd/fsync.c used to report success for any
		 * non-regular-file descriptor, which this asserted as
		 * correct; it is not (see test/posix-io.c's
		 * posix_io_fsync_pipe_einval, un-fenced alongside this). */
		errno = 0;
		CHECK(fsync(p[1]) == -1 && errno == EINVAL);
		errno = 0;
		CHECK(isatty(p[0]) == 0 && errno == ENOTTY);
		CHECK(write(p[1], "last", 4) == 4);
		CHECK(close(p[1]) == 0);
		CHECK(read(p[0], buf, sizeof buf) == 4 && !memcmp(buf, "last", 4));
		CHECK(read(p[0], buf, sizeof buf) == 0);   /* EOF after writer closed */
		CHECK(read(p[0], buf, sizeof buf) == 0);
		CHECK(close(p[0]) == 0);
		CHECK(pipe2(p, O_CLOEXEC) == 0);
		CHECK(fcntl(p[0], F_GETFD) == FD_CLOEXEC && fcntl(p[1], F_GETFD) == FD_CLOEXEC);
		CHECK(close(p[0]) == 0 && close(p[1]) == 0);
	}

	/* read/write/lseek on bad fds */
	errno = 0;
	CHECK(read(999, buf, 1) == -1 && errno == EBADF);
	errno = 0;
	CHECK(write(999, buf, 1) == -1 && errno == EBADF);
	errno = 0;
	CHECK(lseek(999, 0, SEEK_SET) == -1 && errno == EBADF);
	errno = 0;
	CHECK(write(-1, buf, 1) == -1 && errno == EBADF);
	errno = 0;
	CHECK(fsync(999) == -1 && errno == EBADF);
	/* zero-length read/write are no-ops */
	fd = open("a.txt", O_RDWR);
	CHECK(fd >= 0);
	CHECK(read(fd, buf, 0) == 0);
	CHECK(write(fd, buf, 0) == 0);
	CHECK(lseek(fd, 0, SEEK_CUR) == 0);
	/* fd numbering: lowest free slot is reused */
	fd2 = dup(fd);
	fd3 = dup(fd);
	CHECK(fd2 >= 0 && fd3 > fd2);
	CHECK(close(fd2) == 0);
	CHECK(dup(fd) == fd2);
	CHECK(close(fd2) == 0 && close(fd3) == 0);
	CHECK(close(fd) == 0);

	/* sleep/usleep */
	CHECK(sleep(0) == 0);
	CHECK(usleep(1000) == 0);
	{
		struct timespec ts = { 0, 2000000 }, rem;
		CHECK(nanosleep(&ts, &rem) == 0);
		ts.tv_nsec = 2000000000L;
		errno = 0;
		CHECK(nanosleep(&ts, 0) == -1 && errno == EINVAL);
	}

	/* sysconf/pathconf */
	CHECK(sysconf(_SC_PAGESIZE) > 0);
	CHECK(sysconf(_SC_PAGESIZE) == getpagesize());
	CHECK(sysconf(_SC_OPEN_MAX) > 100);
	CHECK(sysconf(_SC_OPEN_MAX) == getdtablesize());
	CHECK(sysconf(_SC_NPROCESSORS_ONLN) >= 1);
	CHECK(sysconf(_SC_CLK_TCK) > 0);
	CHECK(sysconf(_SC_VERSION) == _POSIX_VERSION);
	errno = 0;
	CHECK(sysconf(-12345) == -1 && errno == EINVAL);
	CHECK(pathconf(".", _PC_NAME_MAX) > 0);
	CHECK(pathconf(".", _PC_PATH_MAX) > 0);
	errno = 0;
	CHECK(pathconf(".", -1) == -1 && errno == EINVAL);

	/* gethostname */
	memset(buf, 0, sizeof buf);
	CHECK(gethostname(buf, sizeof buf) == 0);
	CHECK(buf[0] != 0);
	CHECK(strlen(buf) < sizeof buf);
	errno = 0;
	CHECK(gethostname(buf, 1) == 0 && errno == 0);

	/* pids and ids */
	CHECK(getpid() > 0);
	CHECK(getpid() == getpid());
	CHECK(getppid() >= 0);   /* 0 under Wine when the parent is a Unix process */
	CHECK(gettid() > 0);
	CHECK(getuid() == geteuid());
	CHECK(getgid() == getegid());
	CHECK(setuid(getuid()) == 0);
	CHECK(seteuid(geteuid()) == 0);
	CHECK(setgid(getgid()) == 0);
	CHECK(setegid(getegid()) == 0);
	{
		gid_t g[4];
		int n = getgroups(4, g);
		CHECK(n >= 1 && g[0] == getgid());
		CHECK(getgroups(0, 0) >= 1);
	}
	CHECK(getpgrp() > 0);
	CHECK(getpgid(0) > 0);
	CHECK(getsid(0) > 0);
	CHECK(chown("a.txt", getuid(), getgid()) == 0);
	CHECK(nice(0) == 0);
	errno = 0;
	CHECK(chroot("/") == -1 && errno == EPERM);
	CHECK(issetugid() == 0);
	CHECK(ctermid(0) != 0);
	CHECK(ctermid(buf) == buf && buf[0]);

	/* cleanup */
	CHECK(unlink("a.txt") == 0);
	CHECK(chdir(origcwd) == 0);
	CHECK(rmdir(dir) == 0);
	CHECK(stat(dir, &st) == -1);

	if (!fails) printf("unistd: all tests passed\n");
	return fails != 0;
}
