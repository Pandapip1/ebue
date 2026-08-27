/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The odds and ends of <stdio.h>: perror, remove/rename, tmpfile and its
 * name-only relatives, and popen/pclose.
 *
 * rename is NtSetInformationFile(FileRenameInformation) given the
 * destination's full NT path with no RootDirectory, which NT resolves
 * the same way opening that path would; that only works within one
 * volume, which is what POSIX rename promises anyway (EXDEV otherwise).
 *
 * popen has no /bin/sh to hand the command to, so it hands it to cmd.exe
 * /c instead -- the same choice every from-scratch Windows C runtime
 * without a POSIX subsystem makes, since cmd is what "the shell" means
 * on this OS.  Its path comes from %ComSpec%, which every Windows since
 * the days of command.com sets to cmd's full path: __spawn needs one
 * (it resolves relative to the current directory, not PATH), and
 * %ComSpec% is the one every other Windows program trusts for the same
 * reason.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include "stdio_impl.h"

void perror(const char *s)
{
	int e = errno;
	if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
	fputs(strerror(e), stderr);
	fputc('\n', stderr);
}

int remove(const char *path)
{
	if (unlink(path) == 0) return 0;
	if (errno == EISDIR) return rmdir(path);
	return -1;
}

/* POSIX classifies a symbolic link as a non-directory file whatever it
 * points at; NT gives a directory symlink FILE_ATTRIBUTE_DIRECTORY on
 * the link itself.  Same predicate as src/stat/stat.c's
 * mode_from_attrs(), so renameat() and lstat() agree on what a link is. */
static int isdir_attrs(ULONG attrs, ULONG tag)
{
	if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
	    (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT || tag == IO_REPARSE_TAG_LX_SYMLINK))
		return 0;
	return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int final_dot_component(const char *path)
{
	const char *end = path + strlen(path), *start;
	while (end > path && (end[-1] == '/' || end[-1] == '\\')) end--;
	start = end;
	while (start > path && start[-1] != '/' && start[-1] != '\\') start--;
	return (end - start == 1 && start[0] == '.') ||
	       (end - start == 2 && start[0] == '.' && start[1] == '.');
}

static int ntpath_is_ancestor(const struct __ntpath *old,
		const struct __ntpath *new)
{
	size_t i, on = old->nt.Length / sizeof(WCHAR);
	size_t nn = new->nt.Length / sizeof(WCHAR);
	if (old->oa.RootDirectory != new->oa.RootDirectory || on >= nn)
		return 0;
	for (i = 0; i < on; i++) {
		WCHAR a = old->nt.Buffer[i], b = new->nt.Buffer[i];
		if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
		if (a == '/') a = '\\';
		if (b == '/') b = '\\';
		if (a != b) return 0;
	}
	return new->nt.Buffer[on] == '\\' || new->nt.Buffer[on] == '/';
}

int renameat(int olddirfd, const char *old, int newdirfd, const char *new)
{
	struct __ntpath op, np;
	IO_STATUS_BLOCK io;
	FILE_RENAME_INFORMATION *ri;
	HANDLE h;
	NTSTATUS st;
	size_t bufsz;
	FILE_ATTRIBUTE_TAG_INFORMATION oti, nti;
	int old_isdir, new_exists, new_isdir;

	if (final_dot_component(old) || final_dot_component(new)) {
		errno = EINVAL;
		return -1;
	}
	if (__ntpath_at(olddirfd, old, &op, OBJ_CASE_INSENSITIVE) < 0) return -1;
	if (__ntpath_at(newdirfd, new, &np, OBJ_CASE_INSENSITIVE) < 0) { __ntpath_free(&op); return -1; }
	if (ntpath_is_ancestor(&op, &np)) {
		__ntpath_free(&op);
		__ntpath_free(&np);
		errno = EINVAL;
		return -1;
	}

	/* FILE_READ_ATTRIBUTES is requested alongside DELETE because the
	 * type check below queries FileBasicInformation on this same handle
	 * to learn whether old is a directory.
	 * NtQueryInformationFile(FileBasicInformation) requires
	 * FILE_READ_ATTRIBUTES on real NT (same requirement as
	 * src/stat/chmod.c's query and src/stat/utimensat.c's, see the
	 * latter's comment); DELETE alone is enough for the rename itself
	 * (FileRenameInformation's IopSetOperationAccess entry is DELETE),
	 * so adding FILE_READ_ATTRIBUTES here is purely additive and cannot
	 * newly deny the open. */
	st = NtOpenFile(&h, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &op.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	__ntpath_free(&op);
	if (!NT_SUCCESS(st)) { __ntpath_free(&np); return __set_errno_status(st); }

	/* rename.html ERRORS: "the old argument names a directory and the new
	 * argument names a non-directory file" is [ENOTDIR], and RETURN VALUE
	 * requires that on failure "neither the file named by old nor the file
	 * named by new shall be changed or created".  NT honours neither:
	 * FileRenameInformation[Ex] with FILE_RENAME_REPLACE_IF_EXISTS will
	 * rename a directory straight over an existing regular file, unlink
	 * the victim and report success.  The check therefore has to happen
	 * HERE, before the set -- once NT has run, the file is already gone
	 * and there is nothing left to diagnose.
	 *
	 * old's type comes from the handle already open on it (which is why
	 * the open above asks for FILE_READ_ATTRIBUTES); new's from a
	 * handle-less attribute query, since new is never opened.  Both are
	 * reused by the STATUS_ACCESS_DENIED disambiguation below, which used
	 * to make these same two queries for itself after the fact. */
	old_isdir = NT_SUCCESS(NtQueryInformationFile(h, &io, &oti, sizeof oti, FileAttributeTagInformation)) &&
	            isdir_attrs(oti.FileAttributes, oti.ReparseTag);
	{
		HANDLE nh;
		NTSTATUS nst = NtOpenFile(&nh, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
		                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
		new_exists = NT_SUCCESS(nst);
		new_isdir = 0;
		if (new_exists) {
			if (NT_SUCCESS(NtQueryInformationFile(nh, &io, &nti, sizeof nti, FileAttributeTagInformation)))
				new_isdir = isdir_attrs(nti.FileAttributes, nti.ReparseTag);
			NtClose(nh);
		}
	}
	if (old_isdir && new_exists && !new_isdir) {
		NtClose(h);
		__ntpath_free(&np);
		errno = ENOTDIR;
		return -1;
	}

	bufsz = sizeof(FILE_RENAME_INFORMATION) + np.nt.Length;
	ri = __malloc(bufsz);
	if (!ri) { NtClose(h); __ntpath_free(&np); errno = ENOMEM; return -1; }
	ri->Flags = FILE_RENAME_REPLACE_IF_EXISTS | FILE_RENAME_POSIX_SEMANTICS;
	/* renameat.html DESCRIPTION: "If new is a relative path, the file is
	 * located relative to the directory associated with the file
	 * descriptor newfd instead of the current working directory."
	 * __ntpath_at() expresses exactly that by putting newfd's handle in
	 * np.oa.RootDirectory and leaving np.nt unqualified, so the handle
	 * has to be carried into the rename request too -- FILE_RENAME_
	 * INFORMATION's RootDirectory is the same "resolve FileName against
	 * this directory" mechanism as OBJECT_ATTRIBUTES'.  Hardcoding 0
	 * here threw newfd away and asked NT to resolve a bare relative name
	 * against nothing.  np.oa.RootDirectory is 0 for an absolute path or
	 * AT_FDCWD, where np.nt is already a full NT path, so this is a
	 * superset of the old behaviour rather than a change to it. */
	ri->RootDirectory = np.oa.RootDirectory;
	ri->FileNameLength = np.nt.Length;
	memcpy(ri->FileName, np.nt.Buffer, np.nt.Length);

	st = NtSetInformationFile(h, &io, ri, (ULONG)bufsz, FileRenameInformationEx);
	if (st == STATUS_INVALID_PARAMETER || st == STATUS_INVALID_INFO_CLASS ||
	    st == STATUS_NOT_SUPPORTED || st == STATUS_NOT_IMPLEMENTED) {
		ri->Flags = FILE_RENAME_REPLACE_IF_EXISTS;
		st = NtSetInformationFile(h, &io, ri, (ULONG)bufsz, FileRenameInformation);
	}
	__free(ri);

	/* rename.html ERRORS: STATUS_ACCESS_DENIED is what NT answers both
	 * when new names a directory and old does not (should be EISDIR) and
	 * when new names a non-empty directory (should be EEXIST/ENOTEMPTY);
	 * the generic map in __set_errno_status turns both into plain
	 * EACCES, which is right for genuine permission failures but wrong
	 * here.  Disambiguate by type, the way open.c already special-cases
	 * STATUS_FILE_IS_A_DIRECTORY, using the types established above. */
	if (st == STATUS_ACCESS_DENIED && new_isdir) {
		NtClose(h);
		__ntpath_free(&np);
		errno = old_isdir ? ENOTEMPTY : EISDIR;
		return -1;
	}

	NtClose(h);
	__ntpath_free(&np);
	if (st == STATUS_NOT_SAME_DEVICE) { errno = EXDEV; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int rename(const char *old, const char *new) { return renameat(AT_FDCWD, old, AT_FDCWD, new); }

/* Where a temporary file goes: $TMPDIR/$TMP/$TEMP, in that order, or the
 * current directory if none of them are set. */
static const char *tmpdir(void)
{
	const char *d = getenv("TMPDIR");
	if (!d || !*d) d = getenv("TMP");
	if (!d || !*d) d = getenv("TEMP");
	if (!d || !*d) d = ".";
	return d;
}

FILE *tmpfile(void)
{
	char *tmpl;
	int fd;
	FILE *f;
	size_t n = strlen(tmpdir());

	tmpl = malloc(n + sizeof "/ntlibcXXXXXX");
	if (!tmpl) return 0;
	memcpy(tmpl, tmpdir(), n);
	memcpy(tmpl + n, "/ntlibcXXXXXX", sizeof "/ntlibcXXXXXX");
	fd = mkstemp(tmpl);
	if (fd < 0) { free(tmpl); return 0; }
	/* POSIX semantics: unlinked at once, gone the moment it is closed. */
	unlink(tmpl);
	free(tmpl);
	f = __file_new(fd, O_RDWR);
	if (!f) { int e = errno; close(fd); errno = e; return 0; }
	return f;
}

/* tmpnam.html DESCRIPTION: "shall generate a string that is a valid
 * pathname that does not name an existing file", and "shall be able to
 * generate up to {TMP_MAX} different strings".  The one thing this
 * function must therefore not do is create the file, and creating it is
 * exactly what it used to do: it called mkstemp() on a "tmpnam_XXXXXX"
 * template and handed back the name mkstemp had just created.  The
 * documented use of the result is an O_CREAT|O_EXCL create -- the only
 * safe way to use a name this function produces -- and that got [EEXIST]
 * every single time, while every call left a zero-byte tmpnam_* file
 * behind in the caller's directory.
 *
 * mktemp() is the same generator without the create: fill the X's, stat,
 * retry while the name is taken.  It touches nothing, which is what
 * glibc's __GT_NOCREATE and musl's tmpnam do as well.
 *
 * tempnam() below would satisfy the letter of the clause by its own
 * route -- create, close, unlink -- and that is deliberately not what
 * this does.  Create-then-unlink needs write access to a directory
 * tmpnam is only supposed to be naming; it leaves the file behind if the
 * process dies in between; and its unlink() runs on a name no handle is
 * held on any more, so in a directory another user can write to it can
 * be aimed at whatever now answers to that name rather than at the file
 * that was created.  Not creating has none of those failure modes.
 *
 * What is left is the window between naming and the caller's create,
 * which is inherent to the interface (it is why the page is obsolescent
 * and why mkstemp() exists) and is not made worse or better by either
 * choice.  It is why the six random characters are load-bearing: they
 * are what stops the name from being guessed and pre-empted before the
 * caller gets to it, and O_CREAT|O_EXCL on the result -- which now
 * succeeds -- is what makes losing that race a failed open rather than a
 * clobbered file.
 *
 * The four-hex-digit call counter ahead of the random part makes
 * "{TMP_MAX} different strings" a guarantee instead of a per-pair 62^-6
 * probability: the first 65536 calls in a process cannot collide with
 * each other however the generator happens to draw.  As before, the
 * template is short and fixed so the result always fits the caller's
 * char[L_tmpnam] no matter how long $TMP is, and the name stays relative
 * to the current directory. */
char *tmpnam(char *s)
{
	static const char hex[] = "0123456789abcdef";
	static char buf[L_tmpnam];
	static unsigned seq;
	char tmpl[] = "tmpnam_0000XXXXXX";
	unsigned n = seq++;
	int e = errno, i;

	for (i = 0; i < 4; i++) tmpl[10 - i] = hex[(n >> (4 * i)) & 15];
	/* mktemp() reports failure by emptying the template, and sets errno
	 * to 0 when it succeeds -- which tmpnam(), like any other function
	 * here, must not do to its caller.
	 *
	 * The analyzer's advice on the next line -- "use mkstemp() instead"
	 * -- is exactly the change this function was fixed to undo, so it is
	 * suppressed at the one call site rather than tree-wide.  mkstemp()
	 * differs from mktemp() precisely in that it *creates* the file, and
	 * tmpnam.html requires a name that "does not name an existing file";
	 * taking the advice reintroduces the [EEXIST]-on-every-documented-use
	 * defect.  The residual TOCTOU window the check is really about is
	 * inherent to tmpnam()'s interface, is why the page is obsolescent
	 * and why mkstemp() exists as a separate function, and is narrowed
	 * here the only way it can be: six random characters plus a
	 * per-process counter, with the caller's O_CREAT|O_EXCL as the actual
	 * boundary.  glibc (__GT_NOCREATE) and musl reach the same shape. */
	if (!*mktemp(tmpl)) return 0; // NOLINT(clang-analyzer-security.insecureAPI.mktemp) -- see above: mkstemp() is the defect, not the fix
	errno = e;
	if (!s) s = buf;
	memcpy(s, tmpl, sizeof tmpl);
	return s;
}

char *tempnam(const char *dir, const char *pfx)
{
	const char *d = dir ? dir : tmpdir();
	size_t n = strlen(d), pn = pfx ? strlen(pfx) : 0;
	char *tmpl = malloc(n + 1 + pn + sizeof "XXXXXX");
	int fd;
	if (!tmpl) return 0;
	memcpy(tmpl, d, n); // NOLINT(bugprone-not-null-terminated-result) -- built up piece by piece, terminated below
	tmpl[n] = '/';
	if (pn) memcpy(tmpl + n + 1, pfx, pn); // NOLINT(bugprone-not-null-terminated-result) -- ditto
	memcpy(tmpl + n + 1 + pn, "XXXXXX", sizeof "XXXXXX");
	fd = mkstemp(tmpl);
	if (fd < 0) { free(tmpl); return 0; }
	close(fd);
	unlink(tmpl);
	return tmpl;
}

char *ctermid(char *s)
{
	static char buf[L_ctermid] = "/dev/tty";
	if (s) { strcpy(s, "/dev/tty"); return s; }
	return buf;
}

FILE *popen(const char *cmd, const char *mode)
{
	int rw = mode[0] == 'w';
	int fds[2], saved, child_std;
	char *shell;
	char *argv[4];
	int pid;
	FILE *f;

	if (mode[0] != 'r' && mode[0] != 'w') { errno = EINVAL; return 0; }
	if (pipe(fds) < 0) return 0;

	/* Reading: the child's stdout is the pipe's write end.  Writing: the
	 * child's stdin is the pipe's read end.  Either way that fd is
	 * swapped in for the duration of the spawn and put back after. */
	child_std = rw ? 0 : 1;
	saved = dup(child_std);
	if (saved < 0) { close(fds[0]); close(fds[1]); return 0; }
	if (dup2(rw ? fds[0] : fds[1], child_std) < 0) {
		close(saved); close(fds[0]); close(fds[1]);
		return 0;
	}

	{
		const char *comspec = getenv("ComSpec");
		if (!comspec || !*comspec) comspec = "C:\\Windows\\System32\\cmd.exe";
		shell = strdup(comspec);
	}
	if (!shell) { pid = -1; }
	else {
		argv[0] = shell; argv[1] = (char *)"/c"; argv[2] = (char *)cmd; argv[3] = 0;
		pid = __spawn(shell, argv, 0);
		free(shell);
	}

	dup2(saved, child_std);
	close(saved);
	close(rw ? fds[0] : fds[1]);

	if (pid < 0) { close(rw ? fds[1] : fds[0]); return 0; }

	f = __file_new(rw ? fds[1] : fds[0], rw ? O_WRONLY : O_RDONLY);
	if (!f) { int e = errno; close(rw ? fds[1] : fds[0]); errno = e; return 0; }
	f->pid = pid;
	return f;
}

int pclose(FILE *f)
{
	int status;
	pid_t pid = f->pid;
	fclose(f);
	if (pid < 0) { errno = ECHILD; return -1; }
	if (waitpid(pid, &status, 0) < 0) return -1;
	return status;
}
