/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <unistd.h>'s three *at()
 * link interfaces -- symlinkat(), readlinkat() and the linkat()
 * clauses test/posix-unistd.c's test_linkat() left open.  All three
 * are in test/POSIX-GAP-ACCOUNTING.md's "Implemented, not
 * clause-audited (357)" unistd.h row, and symlinkat() is the one name
 * in that row with no assertion of its own anywhere in test/*.c
 * (test/posix-glob.c merely calls it while building a fixture).
 *
 * Pages:
 *   https://pubs.opengroup.org/onlinepubs/9699919799/functions/symlink.html
 *   https://pubs.opengroup.org/onlinepubs/9699919799/functions/readlink.html
 *   https://pubs.opengroup.org/onlinepubs/9699919799/functions/link.html
 *
 * A symbolic link on NT is a reparse point, and *creating* one needs
 * SeCreateSymbolicLinkPrivilege or Developer Mode
 * (src/unistd/link.c's banner).  Whether this run has that is an
 * environment fact, not a code fact, so every clause that needs a
 * symlink to exist first is behind a single probe: symlinkat() is
 * tried once, and if it fails with [EPERM] the dependent groups are
 * reported as SKIP lines and the process exits 77 ("unverified") the
 * way test/posix-socket.c does, rather than passing vacuously.
 * Everything that can be decided *without* the privilege -- the error
 * clauses that are reached before the reparse point is ever written --
 * runs unconditionally.
 *
 * Fence vocabulary is test/posix-termios.c's: BUG / UNIMPL / N/A.
 *
 * Oracle: NT filesystem behaviour, so the real-Windows CI legs are the
 * authority.  Wine implements reparse points well enough to create and
 * read a symlink without the privilege, which is why the privileged
 * half of this file is exercised locally at all -- but a Wine pass is
 * evidence about Wine's reparse-point emulation, not about NTFS.  The
 * two fenced findings below are both readable straight out of
 * src/unistd/link.c and do not depend on which of the two is running.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

static int fails;
static int skips;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Set by main() once, from a single trial symlinkat(): non-zero when
 * this run can create symbolic links. */
static int have_symlinks;

/* ============================================================
 * symlinkat -- the clauses reachable without the privilege
 * ============================================================ */

/* symlink.html ERRORS.  Every one of these is decided by
 * src/unistd/link.c's NtCreateFile(..., FILE_CREATE, ...) or by
 * __ntpath_at() before it, i.e. before the reparse point that needs
 * the privilege is ever written -- so they are testable in any
 * environment and are not behind the have_symlinks gate. */
static void test_symlinkat_errors(void)
{
	int fd, dfd, r;

	fd = open("sl-plain.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(mkdir("sl-dir", 0755) == 0);

	/* "[EEXIST] The path2 argument names an existing file."  Also
	 * DESCRIPTION: "If the symlink() function fails for any reason
	 * other than [EIO], any file named by path2 shall be unaffected"
	 * -- checked by reading the existing file's size back. */
	errno = 0;
	r = symlinkat("whatever", AT_FDCWD, "sl-plain.txt");
	printf("observed: symlinkat(\"whatever\", AT_FDCWD, \"sl-plain.txt\") "
	       "[existing regular file] = %d, errno=%d (want -1/%d EEXIST)\n",
	       r, errno, EEXIST);
	CHECK(r == -1 && errno == EEXIST);
	{
		struct stat st;
		CHECK(stat("sl-plain.txt", &st) == 0 && S_ISREG(st.st_mode));
	}
	/* The same clause over a directory.  It is reported next to the
	 * regular-file case above because the two differ in exactly one
	 * thing on the NT side: src/unistd/link.c picks FILE_DIRECTORY_FILE
	 * or FILE_NON_DIRECTORY_FILE from whether *target* is a directory,
	 * and "whatever" does not exist, so both calls reach NtCreateFile
	 * with FILE_CREATE | FILE_NON_DIRECTORY_FILE -- and only the second
	 * one aims that at an existing directory.  Which of the two
	 * conditions the filesystem reports first is the whole question,
	 * and only the real-NT legs can answer it, so print both. */
	errno = 0;
	r = symlinkat("whatever", AT_FDCWD, "sl-dir");
	printf("observed: symlinkat(\"whatever\", AT_FDCWD, \"sl-dir\") "
	       "[existing directory] = %d, errno=%d (want -1/%d EEXIST; "
	       "%d EISDIR would mean NT checked FILE_NON_DIRECTORY_FILE "
	       "before the FILE_CREATE collision)\n",
	       r, errno, EEXIST, EISDIR);
	CHECK(r == -1 && errno == EEXIST);

	/* "[ENOENT] A component of the path prefix of path2 does not name
	 * an existing file or path2 is an empty string." */
	errno = 0;
	CHECK(symlinkat("t", AT_FDCWD, "sl-nodir/x") == -1 && errno == ENOENT);
	errno = 0;
	CHECK(symlinkat("t", AT_FDCWD, "") == -1 && errno == ENOENT);

	/* "[ENOTDIR] A component of the path prefix of path2 names an
	 * existing file that is neither a directory nor a symbolic link to
	 * a directory." */
	errno = 0;
	CHECK(symlinkat("t", AT_FDCWD, "sl-plain.txt/x") == -1 && errno == ENOTDIR);

	/* symlinkat()'s own two shall-fail clauses:
	 * "[EBADF] The path2 argument does not specify an absolute path
	 *  and the fd argument is neither AT_FDCWD nor a valid file
	 *  descriptor open for reading or searching."
	 * "[ENOTDIR] The path2 argument is not an absolute path and fd is
	 *  a file descriptor associated with a non-directory file." */
	errno = 0;
	CHECK(symlinkat("t", 4096, "sl-rel") == -1 && errno == EBADF);
	CHECK(access("sl-rel", F_OK) == -1);	/* and nothing was created */

	dfd = open("sl-plain.txt", O_RDONLY);
	CHECK(dfd >= 0);
	if (dfd >= 0) {
		errno = 0;
		CHECK(symlinkat("t", dfd, "sl-rel") == -1 && errno == ENOTDIR);
		CHECK(close(dfd) == 0);
	}

	/* "[ENAMETOOLONG] ... the length of the path1 argument is longer
	 * than {SYMLINK_MAX}."  src/unistd/link.c bounds path1 by what a
	 * REPARSE_DATA_BUFFER's USHORT lengths can describe rather than by
	 * {SYMLINK_MAX}, which is a larger limit than the page's minimum
	 * and therefore still conforms -- the clause only requires that
	 * *some* over-long path1 be rejected, and test/unistd.c:375
	 * already pins that case.  Checked here only for the property that
	 * matters to this page: the failure leaves no debris behind. */
	{
		char big[70000];
		memset(big, 'a', sizeof big - 1);
		big[sizeof big - 1] = 0;
		errno = 0;
		CHECK(symlinkat(big, AT_FDCWD, "sl-toolong") == -1);
		CHECK(errno == ENAMETOOLONG);
		CHECK(access("sl-toolong", F_OK) == -1);
	}

	CHECK(unlink("sl-plain.txt") == 0);
	CHECK(rmdir("sl-dir") == 0);

	/* N/A, with the mechanism, for the rest of symlink.html's ERRORS:
	 * [EACCES] needs a directory the caller cannot write, which one
	 * fixed identity (src/unistd/ids.c) cannot construct; [EROFS]
	 * needs a read-only mount; [ENOSPC] needs a full filesystem;
	 * [EIO] is a hardware error; [ELOOP] needs a symbolic-link cycle
	 * in the *prefix* of path2, which src/internal/path.c hands to
	 * NT's own resolver rather than walking itself.  Each of those
	 * would be unreachable even with a fully general symlinkat(). */
}

/* ============================================================
 * symlinkat -- the clauses that need a symbolic link to exist
 * ============================================================ */
static void test_symlinkat_creates(void)
{
	char buf[PATH_MAX];
	ssize_t n;
	struct stat st;

	if (!have_symlinks) {
		printf("SKIP posix-unistd-links symlinkat creation clauses "
		       "(symlinkat() cannot create a link here: NT needs "
		       "SeCreateSymbolicLinkPrivilege or Developer Mode, "
		       "src/unistd/link.c banner)\n");
		skips++;
		return;
	}

	/* symlink.html DESCRIPTION: "The symlink() function shall create a
	 * symbolic link called path2 that contains the string pointed to
	 * by path1", and "The string pointed to by path1 shall be treated
	 * only as a string and shall not be validated as a pathname."  The
	 * second half is the clause an implementation that resolves path1
	 * would fail: the target here does not exist and never will. */
	CHECK(symlinkat("no/such/target-xyz", AT_FDCWD, "sl-a") == 0);
	memset(buf, '@', sizeof buf);
	n = readlink("sl-a", buf, sizeof buf);
	CHECK(n == (ssize_t)strlen("no/such/target-xyz"));
	if (n > 0) CHECK(!memcmp(buf, "no/such/target-xyz", (size_t)n));

	/* DESCRIPTION: "All interfaces specified by POSIX.1-2017 shall
	 * behave as if the contents of symbolic links can always be read,
	 * except that the value of the file mode bits returned in the
	 * st_mode field of the stat structure is unspecified."  So
	 * S_ISLNK() is required and the permission bits are not. */
	CHECK(lstat("sl-a", &st) == 0);
	CHECK(S_ISLNK(st.st_mode));

	/* RETURN VALUE: "Upon successful completion, these functions shall
	 * return 0."  An absolute path1 is the other of the two shapes
	 * src/unistd/link.c builds differently (SYMLINK_FLAG_RELATIVE and
	 * the "\??\" prefix), so both are exercised. */
	{
		char abs[PATH_MAX];
		CHECK(getcwd(abs, sizeof abs) == abs);
		CHECK(strlen(abs) + 12 < sizeof abs);
		strcat(abs, "/sl-abs-tgt");
		CHECK(symlinkat(abs, AT_FDCWD, "sl-b") == 0);
		memset(buf, '@', sizeof buf);
		n = readlink("sl-b", buf, sizeof buf);
		CHECK(n > 0);
		/* The NT round trip is case- and separator-preserving only up
		 * to the "\??\" stripping and the backslash-to-slash rewrite
		 * src/unistd/link.c and readlinkat() perform as a pair, so
		 * what is asserted is that the pair is lossless. */
		if (n > 0) CHECK((size_t)n == strlen(abs) && !memcmp(buf, abs, (size_t)n));
		CHECK(unlink("sl-b") == 0);
	}

	/* DESCRIPTION: "The symlinkat() function shall be equivalent to
	 * the symlink() function except in the case where path2 specifies
	 * a relative path.  In this case the symbolic link is created
	 * relative to the directory associated with the file descriptor
	 * fd." */
	CHECK(mkdir("sl-d", 0755) == 0);
	{
		int dfd = open("sl-d", O_RDONLY | O_DIRECTORY);
		CHECK(dfd >= 0);
		if (dfd >= 0) {
			CHECK(symlinkat("inner-target", dfd, "inner") == 0);
			/* created *there*, not in the current directory */
			CHECK(lstat("sl-d/inner", &st) == 0 && S_ISLNK(st.st_mode));
			CHECK(lstat("inner", &st) == -1);
			memset(buf, '@', sizeof buf);
			n = readlinkat(dfd, "inner", buf, sizeof buf);
			CHECK(n == (ssize_t)strlen("inner-target"));
			if (n > 0) CHECK(!memcmp(buf, "inner-target", (size_t)n));
			CHECK(unlinkat(dfd, "inner", 0) == 0);
			CHECK(close(dfd) == 0);
		}
	}

	/* DESCRIPTION: "If symlinkat() is passed the special value AT_FDCWD
	 * in the fd parameter, the current working directory shall be used
	 * and the behavior shall be identical to a call to symlink()." */
	CHECK(symlinkat("cwd-target", AT_FDCWD, "sl-c") == 0);
	CHECK(symlink("cwd-target", "sl-c2") == 0);
	{
		char a[PATH_MAX], b[PATH_MAX];
		ssize_t na = readlink("sl-c", a, sizeof a);
		ssize_t nb = readlink("sl-c2", b, sizeof b);
		CHECK(na > 0 && na == nb && !memcmp(a, b, (size_t)na));
	}
	CHECK(unlink("sl-c") == 0);
	CHECK(unlink("sl-c2") == 0);

	/* "If path2 names a symbolic link, symlink() shall fail and set
	 * errno to [EEXIST]" -- called out separately in the DESCRIPTION
	 * from the ERRORS entry, because it must hold even though a
	 * dangling symbolic link names nothing that exists. */
	errno = 0;
	CHECK(symlinkat("other", AT_FDCWD, "sl-a") == -1 && errno == EEXIST);
	/* and the existing link is unaffected */
	memset(buf, '@', sizeof buf);
	n = readlink("sl-a", buf, sizeof buf);
	CHECK(n == (ssize_t)strlen("no/such/target-xyz"));

	CHECK(unlink("sl-a") == 0);
	CHECK(rmdir("sl-d") == 0);

	/* N/A: "The symbolic link's user ID shall be set to the process'
	 * effective user ID.  The symbolic link's group ID shall be set to
	 * the group ID of the parent directory or to the effective group
	 * ID of the process."  src/stat/stat.c reports one fixed uid/gid
	 * for every file on this platform (src/unistd/ids.c's single
	 * identity), so both halves are true by construction and neither
	 * can be observed to be otherwise.
	 *
	 * N/A: "Upon successful completion, symlink() shall mark for
	 * update the last data access, last data modification, and last
	 * file status change timestamps of the symbolic link."  A reparse
	 * point's own timestamps are not reachable through this library:
	 * lstat() reports the link (src/stat/stat.c opens with
	 * FILE_OPEN_REPARSE_POINT), but there is no second call that would
	 * update them for a comparison, and the containing directory's
	 * mtime is what test/posix-unistd.c's mkdirat/unlinkat tests
	 * already cover for the entry-creation half. */
}

/* ============================================================
 * readlinkat -- the clauses test_readlink() in test/posix-unistd.c
 * does not reach (its own dirfd errors)
 * ============================================================ */
static void test_readlinkat_dirfd(void)
{
	char buf[64];
	int fd, dfd;

	fd = open("rla.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && close(fd) == 0);

	/* readlink.html, the readlinkat() section, both shall-fail:
	 * "[EBADF] The path argument does not specify an absolute path and
	 *  the fd argument is neither AT_FDCWD nor a valid file descriptor
	 *  open for reading or searching."
	 * "[ENOTDIR] The path argument is not an absolute path and fd is a
	 *  file descriptor associated with a non-directory file."
	 * RETURN VALUE: "Otherwise, these functions shall return a value
	 * of -1, leave the buffer unchanged, and set errno" -- so the
	 * buffer is checked untouched on every failure below. */
	memset(buf, '@', sizeof buf);
	errno = 0;
	CHECK(readlinkat(4096, "rel", buf, sizeof buf) == -1 && errno == EBADF);
	CHECK(buf[0] == '@');

	dfd = open("rla.txt", O_RDONLY);
	CHECK(dfd >= 0);
	if (dfd >= 0) {
		memset(buf, '@', sizeof buf);
		errno = 0;
		CHECK(readlinkat(dfd, "rel", buf, sizeof buf) == -1 && errno == ENOTDIR);
		CHECK(buf[0] == '@');
		CHECK(close(dfd) == 0);
	}

	/* readlink.html "[ENOTDIR] ... A component of the path prefix
	 * names an existing file that is neither a directory nor a
	 * symbolic link to a directory". */
	memset(buf, '@', sizeof buf);
	errno = 0;
	CHECK(readlinkat(AT_FDCWD, "rla.txt/x", buf, sizeof buf) == -1 && errno == ENOTDIR);
	CHECK(buf[0] == '@');

	/* An absolute path must be resolved without consulting fd at all,
	 * even a bad one -- "The path argument does not specify an
	 * absolute path" is the precondition of both clauses above.  The
	 * expected outcome here is [EINVAL] (a regular file is not a
	 * symbolic link), not [EBADF]. */
	{
		char abs[PATH_MAX];
		CHECK(getcwd(abs, sizeof abs) == abs);
		CHECK(strlen(abs) + 10 < sizeof abs);
		strcat(abs, "/rla.txt");
		errno = 0;
		CHECK(readlinkat(4096, abs, buf, sizeof buf) == -1);
		CHECK(errno == EINVAL);
	}

	/* "[EINVAL] The path argument names a file that is not a symbolic
	 * link" -- for a *directory* as well as the regular file
	 * test/posix-unistd.c's test_readlink() already covers, since a
	 * directory is the other shape src/unistd/link.c's
	 * FSCTL_GET_REPARSE_POINT can be handed. */
	CHECK(mkdir("rla-d", 0755) == 0);
	memset(buf, '@', sizeof buf);
	errno = 0;
	CHECK(readlinkat(AT_FDCWD, "rla-d", buf, sizeof buf) == -1 && errno == EINVAL);
	CHECK(buf[0] == '@');
	CHECK(rmdir("rla-d") == 0);

	CHECK(unlink("rla.txt") == 0);

	/* N/A: "If the value of bufsize is greater than {SSIZE_MAX}, the
	 * result is implementation-defined" -- explicitly latitude, and
	 * nothing on this platform can allocate a buffer that large to try
	 * it with.  N/A: [EACCES], [ELOOP], [EIO] for the same reasons as
	 * the symlinkat error group above. */
}

/* ============================================================
 * linkat -- the clauses test_linkat() in test/posix-unistd.c leaves
 * open, and the AT_SYMLINK_FOLLOW finding
 * ============================================================ */
static void test_linkat_remaining(void)
{
	int fd;

	fd = open("lk-src.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, "abc", 3) == 3 && close(fd) == 0);
	CHECK(mkdir("lk-dir", 0755) == 0);

	/* link.html "[ENOTDIR] A component of either path prefix names an
	 * existing file that is neither a directory nor a symbolic link to
	 * a directory ..." -- for both path1 and path2. */
	errno = 0;
	CHECK(linkat(AT_FDCWD, "lk-src.txt/x", AT_FDCWD, "lk-a", 0) == -1 && errno == ENOTDIR);
	errno = 0;
	CHECK(linkat(AT_FDCWD, "lk-src.txt", AT_FDCWD, "lk-src.txt/x", 0) == -1 && errno == ENOTDIR);
	CHECK(access("lk-a", F_OK) == -1);

	/* linkat()'s own "[ENOTDIR] The path1 or path2 argument is not an
	 * absolute path and fd1 or fd2, respectively, is a file descriptor
	 * associated with a non-directory file." */
	fd = open("lk-src.txt", O_RDONLY);
	CHECK(fd >= 0);
	if (fd >= 0) {
		errno = 0;
		CHECK(linkat(fd, "rel", AT_FDCWD, "lk-b", 0) == -1 && errno == ENOTDIR);
		errno = 0;
		CHECK(linkat(AT_FDCWD, "lk-src.txt", fd, "rel", 0) == -1 && errno == ENOTDIR);
		CHECK(close(fd) == 0);
	}
	CHECK(access("lk-b", F_OK) == -1);

	/* "[ENOENT] A component of either path prefix does not exist" --
	 * the path2 side, which test_linkat() checks only for path1. */
	errno = 0;
	CHECK(linkat(AT_FDCWD, "lk-src.txt", AT_FDCWD, "lk-nodir/x", 0) == -1 && errno == ENOENT);
	errno = 0;
	CHECK(linkat(AT_FDCWD, "lk-src.txt", AT_FDCWD, "", 0) == -1 && errno == ENOENT);

	/* "[EEXIST] The path2 argument resolves to an existing directory
	 * entry" -- including when path2 is the same name as path1, and
	 * when it is a directory. */
	errno = 0;
	CHECK(linkat(AT_FDCWD, "lk-src.txt", AT_FDCWD, "lk-src.txt", 0) == -1 && errno == EEXIST);
	errno = 0;
	CHECK(linkat(AT_FDCWD, "lk-src.txt", AT_FDCWD, "lk-dir", 0) == -1 && errno == EEXIST);

#if 0	/* BUG: linkat() on a directory fails with an errno link.html
	 * does not list.
	 *
	 * link.html ERRORS: "These functions *shall* fail if: ... [EPERM]
	 * The file named by path1 is a directory and either the calling
	 * process does not have appropriate privileges or the
	 * implementation prohibits using link() on directories."  NTFS
	 * does prohibit hard links to directories, so the clause's second
	 * branch applies exactly and [EPERM] is what the page requires.
	 * [EISDIR] appears nowhere in link.html's ERRORS list.
	 *
	 * Mechanism: src/unistd/link.c's linkat() has no case for a
	 * directory path1 at all -- it lets NtSetInformationFile fail with
	 * STATUS_FILE_IS_A_DIRECTORY and passes that to
	 * __set_errno_status(), whose table (src/internal/errno.c) maps it
	 * to EISDIR.  That mapping is right for the calls where EISDIR
	 * *is* a specified errno (open(), rename() -- both already
	 * covered in test/posix-unistd.c); it is this call site that needs
	 * to translate.  Probed on this tree:
	 * linkat(AT_FDCWD, "lk-dir", AT_FDCWD, "lk-dir2", 0) returns -1
	 * with errno 21 (EISDIR).  Re-enable when linkat() reports EPERM
	 * for a directory path1. */
	errno = 0;
	CHECK(linkat(AT_FDCWD, "lk-dir", AT_FDCWD, "lk-dir2", 0) == -1 && errno == EPERM);
#endif
	/* Whatever the errno, the call must fail and leave no entry: that
	 * half is asserted unfenced. */
	CHECK(linkat(AT_FDCWD, "lk-dir", AT_FDCWD, "lk-dir2", 0) == -1);
	CHECK(access("lk-dir2", F_OK) == -1);

	if (have_symlinks) {
		CHECK(symlinkat("lk-src.txt", AT_FDCWD, "lk-sym") == 0);

		/* link.html DESCRIPTION: "If path1 names a symbolic link,
		 * ... [if] the AT_SYMLINK_FOLLOW flag is clear ... a new
		 * link is created for the symbolic link path1 and not its
		 * target."  That branch is what src/unistd/link.c
		 * implements unconditionally, and it is correct for flag 0. */
		if (linkat(AT_FDCWD, "lk-sym", AT_FDCWD, "lk-hardsym", 0) == 0) {
			struct stat st;
			CHECK(lstat("lk-hardsym", &st) == 0);
			CHECK(S_ISLNK(st.st_mode));
			CHECK(unlink("lk-hardsym") == 0);
		} else {
			printf("SKIP posix-unistd-links linkat-on-symlink "
			       "(hard link to a reparse point failed here, errno=%d)\n", errno);
			skips++;
		}

#if 0		/* BUG: linkat() ignores its flag argument, so
		 * AT_SYMLINK_FOLLOW does nothing.
		 *
		 * link.html DESCRIPTION: "If path1 names a symbolic link,
		 * ... [if] the AT_SYMLINK_FOLLOW flag is set ... a new link
		 * is created for the file referred to by path1."  With the
		 * flag set, the new entry must be a hard link to the
		 * *target* -- a regular file -- not a second name for the
		 * symbolic link.
		 *
		 * Mechanism: src/unistd/link.c:21 declares
		 * `int linkat(..., int flags)` and line 27 is `(void)flags;`
		 * -- the argument is discarded, and the open on line 31
		 * passes FILE_OPEN_REPARSE_POINT unconditionally, which is
		 * precisely the flag-*clear* behaviour.  So the two branches
		 * the page distinguishes are collapsed into one.
		 *
		 * test/POSIX-GAP-ACCOUNTING.md's successor-session notes
		 * record this clause as N/A on the grounds that
		 * distinguishing the branches "needs a symbolic link, which
		 * needs SeCreateSymbolicLinkPrivilege and is not available
		 * on the CI images this suite is the authority on".  That is
		 * an accurate statement about the *test environment* and it
		 * is why this fence sits behind have_symlinks -- but it is
		 * not a reason to call the clause inapplicable: the defect
		 * is visible in the source without running anything, and it
		 * is reachable in any environment that can create a symbolic
		 * link at all.  Probed on this tree (Wine, which creates
		 * reparse points without the privilege): the entry created
		 * with AT_SYMLINK_FOLLOW is itself a symbolic link, i.e.
		 * S_ISLNK() is true where the clause requires S_ISREG().
		 * Re-enable when linkat() honours flags. */
		{
			struct stat st;
			CHECK(linkat(AT_FDCWD, "lk-sym", AT_FDCWD, "lk-follow", AT_SYMLINK_FOLLOW) == 0);
			CHECK(lstat("lk-follow", &st) == 0);
			CHECK(!S_ISLNK(st.st_mode));
			CHECK(S_ISREG(st.st_mode));
			CHECK(unlink("lk-follow") == 0);
		}
#endif
		CHECK(unlink("lk-sym") == 0);
	} else {
		printf("SKIP posix-unistd-links linkat AT_SYMLINK_FOLLOW clauses "
		       "(no symbolic link can be created here: NT needs "
		       "SeCreateSymbolicLinkPrivilege or Developer Mode)\n");
		skips++;
	}

	CHECK(rmdir("lk-dir") == 0);
	CHECK(unlink("lk-src.txt") == 0);

	/* N/A, with the mechanism: [EMLINK] needs {LINK_MAX} links to one
	 * file (pathconf() answers 1023 and NTFS's real limit is 1024, so
	 * constructing it means creating a thousand entries per run --
	 * measurable cost for a limit the platform, not this code,
	 * enforces); [EXDEV] needs two filesystems, which a CI image is
	 * not guaranteed to have; [ENOSPC]/[EROFS]/[EACCES]/[ELOOP] for
	 * the same reasons given in the symlinkat error group above. */
}

int main(void)
{
	char tmpl[] = "posixunistdlinks-XXXXXX";
	char *dir = mkdtemp(tmpl);
	char origcwd[4096];

	CHECK(getcwd(origcwd, sizeof origcwd) == origcwd);
	CHECK(dir == tmpl);
	if (!dir) return 1;
	CHECK(chdir(dir) == 0);

	/* One trial creation decides whether the privileged half of this
	 * file can run.  symlink.html's [EPERM]-shaped outcome is not on
	 * the page at all -- POSIX has no error for "this system will not
	 * let you make symbolic links" -- so it is reported as an
	 * environment fact rather than asserted in either direction.
	 * src/unistd/link.c maps STATUS_PRIVILEGE_NOT_HELD and
	 * STATUS_ACCESS_DENIED to EPERM for exactly this. */
	have_symlinks = (symlinkat("probe-target", AT_FDCWD, "sl-probe") == 0);
	if (have_symlinks) CHECK(unlink("sl-probe") == 0);
	else printf("note: symbolic links unavailable here (symlinkat errno=%d)\n", errno);

	test_symlinkat_errors();
	test_symlinkat_creates();
	test_readlinkat_dirfd();
	test_linkat_remaining();

	CHECK(chdir(origcwd) == 0);
	CHECK(rmdir(dir) == 0);

	if (fails) return 1;
	if (skips) {
		printf("posix-unistd-links: no failures, but %d assertion group(s) "
		       "did not run in this environment (see SKIP lines above); "
		       "exiting 77 (unverified) rather than reporting a pass\n", skips);
		return 77;
	}
	printf("posix-unistd-links: all tests passed\n");
	return 0;
}
