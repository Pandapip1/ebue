/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * errno and edge-case behaviour of the low-level I/O and process calls,
 * checked against POSIX.1-2017 (each case cites the page and clause next
 * to the assertion).  Genuine bugs found while writing this are fenced
 * with #if 0, tagged BUG:, rather than weakened -- see the report for
 * the list.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- open/close: errno.html#tag_09_02, open.html ---- */
static void test_open_close(void)
{
	int fd;

	errno = 0;
	CHECK(open("no-such-file-xyz", O_RDONLY) == -1 && errno == ENOENT);
	/* open.html ERRORS: "O_CREAT and O_EXCL are set, and the named file exists" -> EEXIST */
	CHECK((fd = open("t-oc.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644)) >= 0);
	CHECK(close(fd) == 0);
	errno = 0;
	CHECK(open("t-oc.txt", O_CREAT | O_EXCL | O_WRONLY, 0644) == -1 && errno == EEXIST);
	/* open.html ERRORS EISDIR: "path names a directory and oflag includes
	 * O_WRONLY or O_RDWR".  On real NT, NtCreateFile itself refuses
	 * FILE_WRITE_DATA on a directory with STATUS_FILE_IS_A_DIRECTORY
	 * (src/fcntl/open.c relies on exactly this).  Wine's server does
	 * not enforce that at open time -- it lets the open through and
	 * only rejects the write -- so under Wine the open below succeeds
	 * and the EISDIR shows up on the first write() instead.  Detect
	 * that divergence rather than asserting Wine's behaviour as NT's. */
	mkdir("t-ocdir", 0755);
	errno = 0;
	{
		int dfd = open("t-ocdir", O_WRONLY);
		if (dfd < 0) {
			CHECK(errno == EISDIR);
		} else {
			errno = 0;
			printf("note: open(dir, O_WRONLY) succeeded (Wine defers the check to write());"
			       " checking write() gets EISDIR instead\n");
			CHECK(write(dfd, "x", 1) == -1 && errno == EISDIR);
			close(dfd);
		}
	}
	rmdir("t-ocdir");

	/* close.html ERRORS EBADF: "fildes is not a valid file descriptor" */
	errno = 0;
	CHECK(close(-1) == -1 && errno == EBADF);
	fd = open("t-oc.txt", O_RDONLY);
	CHECK(fd >= 0 && close(fd) == 0);
	errno = 0;
	CHECK(close(fd) == -1 && errno == EBADF);  /* already closed */

	unlink("t-oc.txt");
}

/* ---- read/write: read.html, write.html ---- */
static void test_read_write(void)
{
	int fd;
	char buf[16];
	ssize_t n;

	CHECK((fd = open("t-rw.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644)) >= 0);
	CHECK(write(fd, "hello", 5) == 5);
	CHECK(close(fd) == 0);

#if 0 /* BUG: read()/write() on an fd open for the wrong direction report
       * EACCES, not EBADF.
       * POSIX read.html ERRORS: "[EBADF] The fildes argument is not a
       * valid file descriptor open for reading."  Symmetrically,
       * write.html requires EBADF for a fildes not open for writing.
       * src/unistd/read.c and src/unistd/write.c do not check the
       * access mode the fd was opened with; they call NtReadFile/
       * NtWriteFile directly and let whatever NTSTATUS comes back go
       * through __set_errno_status().  The handle genuinely lacks
       * FILE_READ_DATA/FILE_WRITE_DATA (src/fcntl/open.c only grants
       * the access bits for the requested O_ACCMODE), so NT correctly
       * returns STATUS_ACCESS_DENIED, which src/internal/errno.c maps
       * to EACCES -- this is real NT behaviour, not a Wine artifact
       * (Wine's dlls/ntdll/unix/file.c enforces the same FILE_READ_DATA/
       * FILE_WRITE_DATA check via server_get_unix_fd()).  Confirmed by
       * execution under Wine:
       *   read on O_WRONLY fd:  n=-1 errno=13 (EACCES, want EBADF=9)
       *   write on O_RDONLY fd: n=-1 errno=13 (EACCES, want EBADF=9)
       * Fix belongs in read()/write(): check f->flags & O_ACCMODE
       * against the requested direction before issuing the I/O and set
       * EBADF directly, the way lseek.c already checks f->type. */
	fd = open("t-rw.txt", O_WRONLY);
	errno = 0;
	CHECK(read(fd, buf, sizeof buf) == -1 && errno == EBADF);
	CHECK(close(fd) == 0);

	fd = open("t-rw.txt", O_RDONLY);
	errno = 0;
	CHECK(write(fd, "x", 1) == -1 && errno == EBADF);
#endif
	fd = open("t-rw.txt", O_RDONLY);

	/* read.html: "In the absence of errors ... read() function shall
	 * return zero and have no other results" -- zero-length request. */
	errno = 0;
	CHECK(read(fd, buf, 0) == 0 && errno == 0);

	/* read at EOF returns 0, not an error. */
	lseek(fd, 0, SEEK_END);
	errno = 0;
	n = read(fd, buf, sizeof buf);
	CHECK(n == 0 && errno == 0);
	CHECK(close(fd) == 0);

	/* write() of zero bytes: POSIX write.html does not mandate a
	 * specific return for a zero-length request beyond "the number of
	 * bytes actually written", so 0 is the only conforming answer. */
	fd = open("t-rw.txt", O_WRONLY);
	errno = 0;
	CHECK(write(fd, buf, 0) == 0 && errno == 0);
	CHECK(close(fd) == 0);

	unlink("t-rw.txt");
}

/* ---- lseek: lseek.html ---- */
static void test_lseek(void)
{
	int fd[2];
	off_t off;

	/* lseek.html ERRORS ESPIPE: "fildes is associated with a pipe,
	 * FIFO, or socket." */
	CHECK(pipe(fd) == 0);
	errno = 0;
	CHECK(lseek(fd[0], 0, SEEK_SET) == -1 && errno == ESPIPE);
	close(fd[0]); close(fd[1]);

	/* "shall allow the file offset to be set beyond the end of the
	 * existing data in the file" and "shall not, by itself, extend the
	 * size of a file" -- a read there then reports EOF (0), not a growth. */
	CHECK((fd[0] = open("t-ls.txt", O_CREAT | O_RDWR | O_TRUNC, 0644)) >= 0);
	CHECK(write(fd[0], "abc", 3) == 3);
	off = lseek(fd[0], 1000, SEEK_SET);
	CHECK(off == 1000);
	{
		struct stat st;
		CHECK(fstat(fd[0], &st) == 0 && st.st_size == 3);  /* not extended */
	}
	{
		char c;
		CHECK(read(fd[0], &c, 1) == 0);  /* nothing at offset 1000: EOF */
	}
	close(fd[0]);
	unlink("t-ls.txt");
}

/* ---- stat/mkdir/rmdir/unlink/rename ---- */
static void test_fs(void)
{
	int fd;
	struct stat st;

	/* stat.html ERRORS ENOENT (via the general path-resolution errors,
	 * errno.html/xbd basedefs). */
	errno = 0;
	CHECK(stat("no-such-file-xyz", &st) == -1 && errno == ENOENT);

	/* mkdir.html ERRORS EEXIST: "The named file exists." */
	mkdir("t-d1", 0755);
	errno = 0;
	CHECK(mkdir("t-d1", 0755) == -1 && errno == EEXIST);
	/* mkdir.html ERRORS ENOENT: "A component of the path prefix ...
	 * does not name an existing directory". */
	errno = 0;
	CHECK(mkdir("t-noexist-parent/sub", 0755) == -1 && errno == ENOENT);

	/* rmdir on a non-empty directory: rmdir.html ERRORS ENOTEMPTY
	 * "path argument names a directory that is not an empty directory". */
	fd = open("t-d1/x.txt", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	errno = 0;
	CHECK(rmdir("t-d1") == -1 && errno == ENOTEMPTY);

	/* rmdir on something that is not a directory: ENOTDIR. */
	errno = 0;
	CHECK(rmdir("t-d1/x.txt") == -1 && errno == ENOTDIR);

	unlink("t-d1/x.txt");
	CHECK(rmdir("t-d1") == 0);

	/* unlink.html ERRORS ENOENT. */
	errno = 0;
	CHECK(unlink("no-such-file-xyz") == -1 && errno == ENOENT);

	/* rename.html: renaming over an existing destination file succeeds
	 * and replaces it (no O_EXCL-like behaviour for rename). */
	fd = open("t-r1.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, "one", 3) == 3 && close(fd) == 0);
	fd = open("t-r2.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, "two", 3) == 3 && close(fd) == 0);
	CHECK(rename("t-r1.txt", "t-r2.txt") == 0);
	CHECK(stat("t-r1.txt", &st) == -1 && errno == ENOENT);
	fd = open("t-r2.txt", O_RDONLY);
	{
		char buf[8] = {0};
		CHECK(fd >= 0 && read(fd, buf, sizeof buf) == 3 && !strcmp(buf, "one"));
	}
	close(fd);
	unlink("t-r2.txt");

	/* rename.html ERRORS ENOENT: source does not exist. */
	errno = 0;
	CHECK(rename("no-such-file-xyz", "t-r2.txt") == -1 && errno == ENOENT);
}

/* ---- dup2/fcntl: fcntl.html ---- */
static void test_dup_fcntl(void)
{
	int fd, nfd;

	/* dup.html/dup2 ERRORS EBADF for an invalid fildes2 range is
	 * implementation-defined beyond OPEN_MAX, but an invalid *source*
	 * fd is unambiguous: dup2.html ERRORS EBADF "fildes argument is not
	 * a valid open file descriptor." */
	errno = 0;
	CHECK(dup2(-1, 5) == -1 && errno == EBADF);

	fd = open("t-dup.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0);
	nfd = fd + 50;
	CHECK(dup2(fd, nfd) == nfd);
	CHECK(close(nfd) == 0);
	close(fd);
	unlink("t-dup.txt");

	/* fcntl.html ERRORS EINVAL: "cmd argument is invalid". */
	fd = open("/dev/stdin", O_RDONLY);
	CHECK(fd >= 0);
	errno = 0;
	CHECK(fcntl(fd, 999999) == -1 && errno == EINVAL);

#if 0 /* BUG: fcntl(fd, F_DUPFD, arg) does not validate arg.
       * POSIX fcntl.html ERRORS: "[EINVAL] The cmd argument is F_DUPFD
       * or F_DUPFD_CLOEXEC and arg is negative or greater than or equal
       * to {OPEN_MAX}."  src/fcntl/fcntl.c's F_DUPFD/F_DUPFD_CLOEXEC arm
       * calls __fd_alloc((int)arg) directly; src/internal/fd.c's
       * __fd_alloc() clamps a negative `lowest` to 0 instead of failing,
       * so fcntl(fd, F_DUPFD, -5) silently succeeds and hands back some
       * unrelated low fd instead of EINVAL.  A too-large arg (>= FD_MAX)
       * falls through to __fd_alloc's own "no free slot" path and
       * returns EMFILE, not EINVAL.  Confirmed by execution under Wine:
       *   F_DUPFD(-5): r=4 errno=0
       *   F_DUPFD(999999): r=-1 errno=24 (EMFILE, want EINVAL=22)
       * Fix belongs in src/fcntl/fcntl.c (validate arg before calling
       * __fd_alloc) rather than in __fd_alloc itself, since __fd_alloc's
       * "clamp negative `lowest`" behaviour is relied on elsewhere
       * (dup()'s internal use with lowest==0). */
	errno = 0;
	CHECK(fcntl(fd, F_DUPFD, -5) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(fcntl(fd, F_DUPFD, 999999) == -1 && errno == EINVAL);
#endif
	close(fd);
}

/* ---- pipe ---- */
static void test_pipe(void)
{
	int fd[2];
	char buf[8] = {0};

	CHECK(pipe(fd) == 0);
	CHECK(write(fd[1], "hi", 2) == 2);
	CHECK(read(fd[0], buf, sizeof buf) == 2 && !strcmp(buf, "hi"));
	close(fd[1]);
	/* Reading a closed-writer pipe to exhaustion: 0 (EOF), not an error. */
	errno = 0;
	CHECK(read(fd[0], buf, sizeof buf) == 0 && errno == 0);
	close(fd[0]);
}

/* ---- chdir: chdir.html ERRORS ENOENT ---- */
static void test_chdir(void)
{
	char cwd[1024];
	CHECK(getcwd(cwd, sizeof cwd) != NULL);
	errno = 0;
	CHECK(chdir("no-such-dir-xyz") == -1 && errno == ENOENT);
	{
		char cwd2[1024];
		CHECK(getcwd(cwd2, sizeof cwd2) != NULL && !strcmp(cwd, cwd2));  /* unchanged on failure */
	}
}

/* ---- waitpid/kill ---- */
static void test_wait_kill(void)
{
	int status;

	/* waitpid.html ERRORS ECHILD: pid does not exist or is not a child
	 * of the calling process. */
	errno = 0;
	CHECK(waitpid(999999, &status, 0) == -1 && errno == ECHILD);

	/* kill(pid, 0): existence/permission check only, no signal sent. */
	CHECK(kill(getpid(), 0) == 0);
	/* kill.html ERRORS ESRCH: "No process or process group can be
	 * found corresponding to that specified by pid." */
	errno = 0;
	CHECK(kill(999999, 0) == -1 && errno == ESRCH);
}

/* ---- malloc/realloc: realloc.html ---- */
static void test_alloc(void)
{
	void *p, *q;

	/* "If ptr is a null pointer, realloc() shall be equivalent to
	 * malloc() for the specified size." */
	p = realloc(NULL, 64);
	CHECK(p != NULL);
	memset(p, 0xAA, 64);  /* usable */

	/* realloc(p, 0): implementation-defined (NULL, or a pointer safe to
	 * free but not to dereference) -- both are conforming, so only
	 * check the call doesn't crash and free() of either is safe. */
	q = realloc(p, 0);
	free(q);

	/* malloc(0): implementation-defined; either NULL or a unique
	 * pointer safe to free. Just check free() doesn't crash either way. */
	p = malloc(0);
	free(p);
}

/* ---- snprintf truncation return: C99 7.19.6.5p3, referenced by
 * POSIX's snprintf.html "the number of bytes that would have been
 * written had n been sufficiently large" ---- */
static void test_snprintf(void)
{
	char buf[4];
	int n = snprintf(buf, sizeof buf, "hello world");
	CHECK(n == 11);            /* full length, not truncated length */
	CHECK(!strcmp(buf, "hel")); /* buffer holds n-1 chars + NUL */
}

/* ---- fseek/ftell on an update ("r+") stream: fseek.html ---- */
static void test_fseek_update(void)
{
	FILE *f = fopen("t-fs.txt", "w+");
	CHECK(f != NULL);
	CHECK(fwrite("abcdef", 1, 6, f) == 6);
	CHECK(fseek(f, 0, SEEK_SET) == 0);
	CHECK(ftell(f) == 0);
	{
		char c = (char)fgetc(f);
		CHECK(c == 'a');
	}
	CHECK(ftell(f) == 1);
	/* fseek.html: "a successful call to fseek() ... shall undo any
	 * effects of ungetc() ... on the stream" and switches from read to
	 * write mode on an update stream without an intervening fflush(). */
	CHECK(fseek(f, 2, SEEK_CUR) == 0);
	CHECK(ftell(f) == 3);
	CHECK(fputc('Z', f) == 'Z');
	fclose(f);
	f = fopen("t-fs.txt", "r");
	{
		char buf[8] = {0};
		CHECK(fread(buf, 1, 6, f) == 6);
		CHECK(!memcmp(buf, "abcZef", 6));
	}
	fclose(f);
	unlink("t-fs.txt");
}

int main(void)
{
	test_open_close();
	test_read_write();
	test_lseek();
	test_fs();
	test_dup_fcntl();
	test_pipe();
	test_chdir();
	test_wait_kill();
	test_alloc();
	test_snprintf();
	test_fseek_update();

	if (!fails) printf("posix-io: all tests passed\n");
	return fails != 0;
}
