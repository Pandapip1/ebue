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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include "ownership_stubs.h"
#include "stdio_impl.h"
#include "plat_stdio.h"
#include "plat_fd.h"

void perror(const char *s)
{
	int e = errno;
	/* perror() is the diagnostic and returns void; a failure writing stderr
	 * cannot be recursively reported or replace the caller's saved errno. */
	if (s && *s) { (void)fputs(s, stderr); (void)fputs(": ", stderr); }
	(void)fputs(strerror(e), stderr);
	(void)fputc('\n', stderr);
}

int remove(const char *path)
{
	if (unlink(path) == 0) return 0;
	if (errno == EISDIR) return rmdir(path);
	return -1;
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

int renameat(int olddirfd, const char *old, int newdirfd, const char *new)
{
	/* rename.html ERRORS: [EINVAL] "The old argument names an ancestor
	 * directory of the new argument, or old or new names a terminal ..,
	 * or .." -- this one piece stays portable (pure string logic on the
	 * final path component, no backend involvement), unlike everything
	 * else renameat() used to do here inline (NT path resolution, the
	 * directory-vs-non-directory type check, the actual rename), which
	 * moved into __plat_rename() -- see plat_stdio.h's own updated
	 * banner. */
	if (final_dot_component(old) || final_dot_component(new)) {
		errno = EINVAL;
		return -1;
	}
	return __plat_rename(olddirfd, old, newdirfd, new);
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
	const char *dir = tmpdir();
	char *tmpl;
	int fd;
	FILE *f;
	size_t n = strlen(dir);

	if (n > (size_t)-1 - sizeof "/ntlibcXXXXXX") { errno = ENOMEM; return 0; }
	tmpl = malloc(n + sizeof "/ntlibcXXXXXX");
	if (!tmpl) return 0;
	__ownership_writable_span(tmpl, n + sizeof "/ntlibcXXXXXX");
	__ownership_readable_span(dir, n);
	memcpy(tmpl, dir, n);
	__ownership_writable_span(tmpl + n, sizeof "/ntlibcXXXXXX");
	__ownership_readable_span("/ntlibcXXXXXX", sizeof "/ntlibcXXXXXX");
	memcpy(tmpl + n, "/ntlibcXXXXXX", sizeof "/ntlibcXXXXXX");
	fd = mkstemp(tmpl);
	if (fd < 0) { free(tmpl); return 0; }
	/* POSIX semantics: unlinked at once, gone the moment it is closed. */
	if (unlink(tmpl) < 0) {
		int e = errno;
		(void)close(fd);
		free(tmpl);
		errno = e;
		return 0;
	}
	free(tmpl);
	f = __file_new(fd, O_RDWR);
	if (!f) { int e = errno; (void)close(fd); errno = e; return 0; }
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
	__ownership_writable_span(s, sizeof tmpl);
	__ownership_readable_span(tmpl, sizeof tmpl);
	memcpy(s, tmpl, sizeof tmpl);
	return s;
}

withtok(heap_allocated)
char *tempnam(const char *dir, const char *pfx) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	const char *d = dir ? dir : tmpdir();
	size_t n = strlen(d), pn = pfx ? strlen(pfx) : 0;
	char *tmpl = malloc(n + 1 + pn + sizeof "XXXXXX");
	int fd;
	if (!tmpl) return 0;
	{
		char *dst = tmpl;
		const char *src = d;
		__ownership_writable_span(dst, n);
		__ownership_readable_span(src, n);
		memcpy(dst, src, n); // NOLINT(bugprone-not-null-terminated-result) -- built up piece by piece, terminated below
	}
	tmpl[n] = '/';
	if (pn) {
		__ownership_writable_span(tmpl + n + 1, pn);
		__ownership_readable_span(pfx, pn);
		memcpy(tmpl + n + 1, pfx, pn); // NOLINT(bugprone-not-null-terminated-result) -- ditto
	}
	__ownership_writable_span(tmpl + n + 1 + pn, sizeof "XXXXXX");
	__ownership_readable_span("XXXXXX", sizeof "XXXXXX");
	memcpy(tmpl + n + 1 + pn, "XXXXXX", sizeof "XXXXXX");
	fd = mkstemp(tmpl);
	if (fd < 0) { free(tmpl); return 0; }
	if (close(fd) < 0 || unlink(tmpl) < 0) {
		int e = errno;
		(void)unlink(tmpl);
		free(tmpl);
		errno = e;
		return 0;
	}
	return tmpl;
}

char *ctermid(char *s)
{
	static char buf[L_ctermid] = "/dev/tty";
	if (s) {
		__ownership_writable_span(s, sizeof "/dev/tty");
		__ownership_readable_span("/dev/tty", sizeof "/dev/tty");
		memcpy(s, "/dev/tty", sizeof "/dev/tty");
		return s;
	}
	return buf;
}

FILE *popen(const char *cmd, const char *mode) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
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
	if (saved < 0) {
		int e = errno;
		(void)close(fds[0]); (void)close(fds[1]);
		errno = e;
		return 0;
	}
	if (dup2(rw ? fds[0] : fds[1], child_std) < 0) {
		int e = errno;
		(void)close(saved); (void)close(fds[0]); (void)close(fds[1]);
		errno = e;
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

	if (dup2(saved, child_std) < 0) {
		int e = errno;
		(void)close(saved);
		(void)close(fds[0]);
		(void)close(fds[1]);
		errno = e;
		return 0;
	}
	(void)close(saved);
	(void)close(rw ? fds[0] : fds[1]);

	if (pid < 0) { (void)close(rw ? fds[1] : fds[0]); return 0; }

	f = __file_new(rw ? fds[1] : fds[0], rw ? O_WRONLY : O_RDONLY);
	if (!f) { int e = errno; (void)close(rw ? fds[1] : fds[0]); errno = e; return 0; }
	f->pid = pid;
	return f;
}

int pclose(FILE *f)
{
	int status;
	pid_t pid = f->pid;
	(void)fclose(f);
	if (pid < 0) { errno = ECHILD; return -1; }
	if (waitpid(pid, &status, 0) < 0) return -1;
	return status;
}

// NOLINTEND(misc-include-cleaner)
