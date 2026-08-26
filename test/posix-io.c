/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * errno and edge-case behaviour of the low-level I/O and process calls,
 * checked against POSIX.1-2017 (each case cites the page and clause next
 * to the assertion).  Genuine bugs found while writing this are fenced
 * with #if 0, tagged BUG:, rather than weakened -- see the report for
 * the list.
 */
#include "test-policy.h"
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

	fd = open("t-rw.txt", O_WRONLY);
	errno = 0;
	CHECK(read(fd, buf, sizeof buf) == -1 && errno == EBADF);
	CHECK(close(fd) == 0);

	fd = open("t-rw.txt", O_RDONLY);
	errno = 0;
	CHECK(write(fd, "x", 1) == -1 && errno == EBADF);

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

	/* write.html DESCRIPTION: "For regular files, no data transfer shall
	 * occur past the offset maximum established in the open file
	 * description associated with fildes."  ERRORS, shall fail:
	 * "[EFBIG] The file is a regular file, nbyte is greater than 0, and
	 * the starting position is greater than or equal to the offset
	 * maximum established in the open file description associated with
	 * fildes."
	 *
	 * pwrite() is the vehicle, not write(), and the reason is measured
	 * rather than stylistic.  The clause needs a starting position at or
	 * past the offset maximum; for write() that is the file position,
	 * and lseek() cannot put a descriptor there on this host.  Binary
	 * searched under Wine on ext4: the largest offset NtSetInformation-
	 * File(FilePositionInformation) accepts is 0xffffffff000, and every
	 * larger one -- OFF_MAX, 2^62 -- comes back [EINVAL], so the
	 * precondition of the write() spelling is not constructible here.
	 * (glibc on ext4 refuses the same seek with the same errno, for the
	 * same filesystem reason.)  pwrite() takes the starting position as
	 * an argument and so sidesteps the seek entirely.
	 *
	 * MEASURED BEFORE THE FIX: pwrite(fd, "x", 1, OFF_MAX) returned -1
	 * with [EINVAL], NT's STATUS_INVALID_PARAMETER for a write it cannot
	 * place -- not [EFBIG]. */
	fd = open("t-offmax.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (fd >= 0) {
		/* off_t is _Int64 (include/alltypes.h.in), so the offset
		 * maximum an open file description can express is this. */
		off_t offmax = (off_t)0x7fffffffffffffffLL;

		errno = 0;
		CHECK(pwrite(fd, "x", 1, offmax) == -1);
		CHECK(errno == EFBIG);

		/* "nbyte is greater than 0" is part of the clause, not
		 * decoration: a zero-length request at the same position is
		 * outside it and must not be reported as [EFBIG].  This is the
		 * boundary that keeps the fix from degenerating into "any
		 * pwrite at a large offset is EFBIG". */
		errno = 0;
		CHECK(!(pwrite(fd, "x", 0, offmax) == -1 && errno == EFBIG));

		/* and a pwrite nowhere near the maximum still works */
		errno = 0;
		CHECK(pwrite(fd, "hello", 5, 0) == 5);

		CHECK(close(fd) == 0);
		unlink("t-offmax.txt");
	}
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

/* ---- a path longer than Windows' {MAX_PATH}, which POSIX says nothing
 * about and <limits.h> promises 4096 bytes of ----
 *
 * XBD <limits.h>: {PATH_MAX} is the "Maximum number of bytes in a
 * pathname, including the terminating null character", and this library
 * defines it as 4096 (include/limits.h) and reports it from
 * sysconf(_PC_PATH_MAX) (src/unistd/sysconf.c).  A caller is therefore
 * entitled to a 4000-byte pathname, and open/stat/mkdir/unlink/rmdir --
 * every interface that goes through src/internal/path.c -- has to carry
 * one.
 *
 * They did not.  Real Windows' RtlDosPathNameToNtPathName_U applies the
 * Win32 {MAX_PATH} = 260 ceiling to any name it normalises, so on the
 * windows-test CI legs every one of these calls used to fail with
 * [ENAMETOOLONG] at 261 bytes of resolved path, silently capping the
 * library at a sixteenth of its own advertised {PATH_MAX}.  Wine has no
 * such ceiling, which is exactly why nothing noticed: this assertion
 * cannot fail under Wine, and it is the real-Windows leg it is written
 * for.  See src/internal/path.c's nt_path_over_max_path() for the
 * measurements and the fix.
 *
 * The construction is deliberately independent of the working
 * directory's own depth -- two 200-byte components under a short one
 * put the RELATIVE path past 400 bytes on its own, so the test means
 * the same thing whether it runs from "D:\a\ntlibc\ntlibc" or from a
 * drive root.  Every individual component stays within {NAME_MAX}
 * (255), so this is a statement about path length only and does not
 * quietly re-test the component limit. */
#if NTLIBC_TEST(BUG, posix_io_rmdir_rejects_dot) /* BUG: rmdir("d/.") deletes d.  rmdir.html DESCRIPTION: "If the
	 * path argument refers to a path whose final component is either
	 * dot or dot-dot, rmdir() shall fail."  ERRORS, shall fail:
	 * "[EINVAL] The path argument contains a last component that is
	 * dot."  unlinkat.html inherits both through AT_REMOVEDIR.
	 *
	 * Mechanism: src/unistd/unlink.c's __unlink_at() -- which is all of
	 * rmdir(), unlink() and unlinkat() -- never looks at the final
	 * component at all.  It goes from __ntpath_at() straight to
	 * NtOpenFile(... FILE_DIRECTORY_FILE) and sets the delete
	 * disposition.  And by the time it sees the name, the dot is gone:
	 * src/internal/path.c resolves "." and ".." lexically before the
	 * filesystem is ever consulted -- through
	 * RtlDosPathNameToNtPathName_U_WithStatus() on the AT_FDCWD and
	 * absolute branch (the file's own banner cites GetFullPathName's
	 * Remarks for exactly this), and through normalize_rel()'s explicit
	 * `len == 1 && w[i] == '.'` / ".." handling on the dirfd-relative
	 * branch.
	 *
	 * So the call POSIX requires to fail instead succeeds -- on the
	 * *predecessor* of the component that should have refused it.
	 * "d/." removes d; "d/.." removes d's parent.  This is the one
	 * missing argument check in this area that destroys data rather
	 * than returning the wrong errno.
	 *
	 * The same collapse is already measured in this tree one layer up:
	 * test/POSIX-COVERAGE.md's renameat row records "observed, a rename
	 * of `dir/.` succeeded".  Same normalization, same cause.
	 *
	 * The check has to happen on the caller's string, before
	 * __ntpath_at(), because nothing downstream can still see the dot.
	 *
	 * Re-enable when rmdir()/unlinkat(AT_REMOVEDIR) refuse a final dot
	 * or dot-dot component. */
static void test_rmdir_rejects_dot(void)
{
	struct stat st;

	CHECK(mkdir("t-dot.d", 0755) == 0);

	errno = 0;
	CHECK(rmdir("t-dot.d/.") == -1 && errno == EINVAL);
	CHECK(stat("t-dot.d", &st) == 0 && S_ISDIR(st.st_mode));   /* must survive */

	errno = 0;
	CHECK(unlinkat(AT_FDCWD, "t-dot.d/.", AT_REMOVEDIR) == -1 && errno == EINVAL);
	CHECK(stat("t-dot.d", &st) == 0 && S_ISDIR(st.st_mode));

	/* dot-dot: the clause covers it too, and here it would take out the
	 * directory the test itself is running in. */
	errno = 0;
	CHECK(rmdir("t-dot.d/..") == -1);
	CHECK(stat("t-dot.d", &st) == 0 && S_ISDIR(st.st_mode));

	CHECK(rmdir("t-dot.d") == 0);
}
#endif

static void test_long_path(void)
{
	char comp[201], dir[512], path[1024], buf[8] = {0};
	struct stat st;
	int fd;

	memset(comp, 'p', sizeof comp - 1);
	comp[sizeof comp - 1] = 0;

	CHECK(mkdir("lp.d", 0755) == 0);
	strcpy(dir, "lp.d/");
	strcat(dir, comp);
	errno = 0;
	CHECK(mkdir(dir, 0755) == 0);
	strcpy(path, dir);
	strcat(path, "/");
	strcat(path, comp);
	CHECK(strlen(path) > 260);

	errno = 0;
	fd = open(path, O_CREAT | O_WRONLY, 0644);
	if (fd < 0) printf("FAIL %s:%d: open(<%d-byte path>) (errno=%d)\n",
		__FILE__, __LINE__, (int)strlen(path), errno);
	CHECK(fd >= 0);
	if (fd >= 0) {
		CHECK(write(fd, "long", 4) == 4);
		CHECK(close(fd) == 0);
		/* and the rest of the path-taking surface reaches it too */
		errno = 0;
		CHECK(stat(path, &st) == 0 && st.st_size == 4);
		fd = open(path, O_RDONLY);
		CHECK(fd >= 0 && read(fd, buf, sizeof buf) == 4 && !strcmp(buf, "long"));
		if (fd >= 0) CHECK(close(fd) == 0);
		errno = 0;
		CHECK(unlink(path) == 0);
	}
	errno = 0;
	CHECK(rmdir(dir) == 0);
	CHECK(rmdir("lp.d") == 0);
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

	errno = 0;
	CHECK(fcntl(fd, F_DUPFD, -5) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(fcntl(fd, F_DUPFD, 999999) == -1 && errno == EINVAL);
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

#if NTLIBC_TEST(BUG, posix_io_fsync_pipe_einval) /* BUG: fsync() on a pipe reports success where POSIX requires a
	 * shall-fail [EINVAL].  fsync.html ERRORS: "[EINVAL] The fildes
	 * argument does not refer to a file on which this operation is
	 * possible."
	 *
	 * Mechanism: src/unistd/fsync.c:13 is
	 *     if (f->type != __FD_FILE) return 0;
	 * -- every descriptor that is not a regular file short-circuits to
	 * success before NtFlushBuffersFile() is ever reached, so a pipe,
	 * a socket and a console fd all report that they were flushed.
	 * EINVAL appears nowhere in that file.  Returning 0 is the
	 * dangerous direction: a caller that fsync()s a pipe to force
	 * durability is told it succeeded.
	 *
	 * This is NOT a consequence of <sys/mman.h> Pass 1.  It is a
	 * pre-existing gap that Pass 1 only made VISIBLE: OPTS fsync/7-1
	 * asserts exactly this clause and had never been built before,
	 * because it #includes <sys/mman.h> (fsync/7-1.c:16) and that
	 * header did not exist.  Shipping the header moved the test out of
	 * the header-absent class into the set that runs, and it fails.
	 *
	 * Re-enable when fsync() rejects a non-file descriptor.  fdatasync()
	 * is a bare alias of fsync() (src/unistd/fsync.c:19) and inherits
	 * the same defect, so it is asserted here too. */
static void test_fsync_pipe_einval(void)
{
	int fd[2];

	CHECK(pipe(fd) == 0);
	errno = 0;
	CHECK(fsync(fd[1]) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(fdatasync(fd[1]) == -1 && errno == EINVAL);
	close(fd[0]);
	close(fd[1]);
}
#endif

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

/* ==================================================================
 * <fcntl.h> header content -- mandatory symbolic constants ntlibc does
 * not define.  Audit group U (XBD header contents); see
 * test/POSIX-COVERAGE.md "XBD header contents (group U)".
 * ================================================================== */

#if NTLIBC_TEST(UNIMPL, posix_io_fcntl_h_access_mode_constants) /* UNIMPL: fcntl.h.html DESCRIPTION: "The <fcntl.h> header shall
	define the following symbolic constants for use as the file access
	modes for open(), openat(), and fcntl(). The values shall be
	unique, except that O_EXEC and O_SEARCH may have equal values...
	O_EXEC Open for execute only (non-directory files)... O_SEARCH
	Open directory for search only", and, in the file-status-flag
	list, "O_TTY_INIT Set the termios structure terminal parameters to
	a state that provides conforming behavior... The O_TTY_INIT flag
	can have the value zero and in this case it need not be
	bitwise-distinct from the other flags."  ntlibc defines
	O_RDONLY/O_WRONLY/O_RDWR/O_ACCMODE and every O_* creation and
	status flag POSIX lists including O_DSYNC, O_RSYNC, O_DIRECTORY
	and O_NOFOLLOW, but not O_EXEC, O_SEARCH or O_TTY_INIT.  Triage:
	ABSENT (no declaration anywhere in include/).  None of the three
	is optional: no option-group marker guards them, and O_TTY_INIT
	is explicitly permitted to be zero, which is the standard's own
	way of saying an implementation with nothing to do for it still
	has to define it.  Scope of this fence: the HEADER CONSTANTS only.
	Whether open() would then have to give O_SEARCH a directory
	handle with traverse-only access, and O_EXEC an execute-only one,
	is a larger, separate gap that this fence deliberately does not
	claim -- do not read an un-fencing of this test as acceptance of
	that.  Observed today: fails to COMPILE, "'O_EXEC' undeclared". */
static void test_fcntl_h_access_mode_constants(void)
{
	/* "The values shall be unique, except that O_EXEC and O_SEARCH
	 * may have equal values." */
	CHECK(O_RDONLY != O_WRONLY && O_RDONLY != O_RDWR && O_WRONLY != O_RDWR);
	CHECK(O_EXEC != O_RDONLY && O_EXEC != O_WRONLY && O_EXEC != O_RDWR);
	CHECK(O_SEARCH != O_RDONLY && O_SEARCH != O_WRONLY && O_SEARCH != O_RDWR);

	/* "O_ACCMODE Mask for file access modes" -- a file access mode
	 * that the mask does not cover cannot be recovered from
	 * fcntl(F_GETFL), which is what the mask is for. */
	CHECK((O_EXEC & O_ACCMODE) == O_EXEC);
	CHECK((O_SEARCH & O_ACCMODE) == O_SEARCH);

	/* O_TTY_INIT may be zero, so the only thing to assert is that it
	 * exists and the preprocessor can see it. */
#if defined(O_TTY_INIT) && defined(O_EXEC) && defined(O_SEARCH)
	CHECK(O_TTY_INIT >= 0);
#else
	CHECK(0);
#endif
}
#endif

int main(void)
{
	test_open_close();
	test_read_write();
	test_lseek();
	test_fs();
	test_long_path();
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
