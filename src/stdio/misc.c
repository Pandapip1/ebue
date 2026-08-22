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

int renameat(int olddirfd, const char *old, int newdirfd, const char *new)
{
	struct __ntpath op, np;
	IO_STATUS_BLOCK io;
	FILE_RENAME_INFORMATION *ri;
	HANDLE h;
	NTSTATUS st;
	size_t bufsz;

	if (__ntpath_at(olddirfd, old, &op, OBJ_CASE_INSENSITIVE) < 0) return -1;
	if (__ntpath_at(newdirfd, new, &np, OBJ_CASE_INSENSITIVE) < 0) { __ntpath_free(&op); return -1; }

	st = NtOpenFile(&h, DELETE | SYNCHRONIZE, &op.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	__ntpath_free(&op);
	if (!NT_SUCCESS(st)) { __ntpath_free(&np); return __set_errno_status(st); }

	bufsz = sizeof(FILE_RENAME_INFORMATION) + np.nt.Length;
	ri = __malloc(bufsz);
	if (!ri) { NtClose(h); __ntpath_free(&np); errno = ENOMEM; return -1; }
	ri->Flags = FILE_RENAME_REPLACE_IF_EXISTS | FILE_RENAME_POSIX_SEMANTICS;
	ri->RootDirectory = 0;
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
	 * STATUS_FILE_IS_A_DIRECTORY -- old's type from the handle already
	 * open on it, new's type from a handle-less attribute query (new was
	 * never opened). */
	if (st == STATUS_ACCESS_DENIED) {
		FILE_BASIC_INFORMATION obi, nbi;
		int old_isdir = NT_SUCCESS(NtQueryInformationFile(h, &io, &obi, sizeof obi, FileBasicInformation)) &&
		                (obi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
		NTSTATUS qst = NtQueryAttributesFile(&np.oa, &nbi);
		if (NT_SUCCESS(qst) && (nbi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			NtClose(h);
			__ntpath_free(&np);
			errno = old_isdir ? ENOTEMPTY : EISDIR;
			return -1;
		}
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

/* Like musl: a fixed short template, so the result always fits the
 * caller's char[L_tmpnam] no matter how long $TMP is.  The name is
 * relative to the current directory; mkstemp creates it, which is what
 * keeps successive names unique. */
char *tmpnam(char *s)
{
	static char buf[L_tmpnam];
	char tmpl[] = "tmpnam_XXXXXX";
	int fd;

	fd = mkstemp(tmpl);
	if (fd < 0) return 0;
	close(fd);
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

char *cuserid(char *s)
{
	static char buf[L_cuserid] = "user";
	if (s) { strcpy(s, buf); return s; }
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
