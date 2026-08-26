/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Resolving a program name for execvp.
 *
 * A name with a directory part (a '/', a '\\', or a drive letter) is
 * taken as-is; __spawn reports ENOENT if it does not exist.  Anything
 * else is looked up in each directory of PATH, trying the name and then
 * the name with ".exe" appended, which is what Windows expects an image
 * to be called.  PATH here is the Windows variable, whose entries are
 * separated by ';' -- a ':' cannot be the separator because every
 * absolute entry ("C:\Windows") contains one.  An empty entry means the
 * current directory, as on Unix.
 *
 * A candidate needs both access(X_OK), backed by $LXMOD when present,
 * and __is_program(), below: the first two bytes are "MZ" (an image NT's
 * own loader will take) or "#!" (a
 * script, which execvp() must hand to a command interpreter -- exec.html
 * DESCRIPTION, the clause that scopes the [ENOEXEC] error "except for
 * execlp() and execvp()").  Reading exactly those two bytes is not an
 * invention: exec.html APPLICATION USAGE names it as one of the two
 * historical strategies -- "some historical implementations handle shell
 * scripts is by recognizing the first two bytes of the file as the
 * character string \"#!\"".
 *
 * The two checks answer different halves of XBD 8.3's requirement that
 * PATH locate "an executable file ... with appropriate execution
 * permissions": $LXMOD supplies permission and the short read rejects a
 * directory or data file which happens to carry that permission.  The
 * only normative statement of the match
 * criterion is XBD 8.3, under PATH: "The list shall be searched from
 * beginning to end, applying the filename to each prefix, until an
 * executable file with the specified name and appropriate execution
 * permissions is found."  That is a *property of the file*, with no
 * mandated way of probing for it -- exec.html itself says only that "the
 * path prefix for this file is obtained by a search of the directories
 * passed as the environment variable PATH", and neither page mentions
 * access() or X_OK anywhere.  musl agrees by construction: its
 * src/process/execvp.c calls no access() at all, it simply execve()s
 * each candidate and continues on EACCES/ENOENT/ENOTDIR.  That algorithm
 * is not available here -- NT process creation is atomic, so a failed
 * attempt is not free the way a failed execve() on a fork()ed child is
 * -- but it settles whether access() was ever the required mechanism.
 * It was not.  Even MSVCRT splits these two questions the same way: its
 * documented extension list (.com/.exe/.bat/.cmd) is on the _exec/_spawn
 * pages, describing *PATH search order*, while the _stat page says only
 * that the user execute bits follow "the filename extension" and
 * enumerates nothing.
 *
 * Content sniffing stays here rather than in stat(): it is needed once
 * per PATH candidate, while putting it in stat() would open and read every
 * regular file merely to report metadata.
 *
 * Cygwin is the cautionary case here, and the difference is where the
 * sniff sits.  Its noacl fallback sniffs inside stat(), pays the open
 * and read on every stat() of every file, and never caches the negative
 * result -- so a non-executable file is re-opened and re-read on each
 * fstat, even on the same descriptor.  That is why its manual documents
 * `ls -l` as slow and offers exec/cygexec mount options to switch the
 * sniff off.  This sniff is reached once per PATH candidate inside one
 * execvp(), never from stat(), so there is no repeated-call pattern for
 * a cache to serve; the fix for Cygwin's problem is not to add a cache
 * here but to keep the sniff out of stat(), which src/stat/stat.c does.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "libc.h"

/* Can NT start this file, or is it a script something else can run?
 *
 * FILE_NON_DIRECTORY_FILE matters as much as the two bytes do: without
 * it a PATH entry with an empty program name appended ("C:\Windows\")
 * opens the *directory* and the search accepts it, which is how
 * execvp("") used to resolve to the first directory in PATH and try to
 * execute it (test/POSIX-COVERAGE.md, exec group, bug 1).
 *
 * FILE_OPEN_NO_RECALL and the two RECALL_ON_* attribute checks keep this
 * from waking a cloud-backed placeholder.  A OneDrive-style provider
 * fetches the entire file when one is opened or first read, so a PATH
 * search that sniffed blindly could pull megabytes over a network to
 * look at two bytes -- and a PATH directory full of placeholders would
 * do it once per candidate.  A placeholder is answered "no" without
 * being read: it is not a program this search can vouch for, and a
 * caller who knows better can still name it with a path and reach
 * __spawn directly, which is what execv() does.
 *
 * Any other failure is "no" for the same reason.
 */
int __is_program(const char *path)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	FILE_BASIC_INFORMATION bi;
	LARGE_INTEGER off = 0;
	HANDLE h;
	NTSTATUS s;
	unsigned char b[4];

	if (__ntpath_at(AT_FDCWD, path, &np, OBJ_CASE_INSENSITIVE) < 0) return 0;
	s = NtOpenFile(&h, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io,
	               FILE_SHARE_VALID_FLAGS,
	               FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_OPEN_NO_RECALL);
	__ntpath_free(&np);
	if (!NT_SUCCESS(s)) return 0;

	/* Offline or not-yet-hydrated: do not touch the data. */
	if (NT_SUCCESS(NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation)) &&
	    (bi.FileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
	                          FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))) {
		NtClose(h);
		return 0;
	}

	io.Information = 0;
	s = NtReadFile(h, 0, 0, 0, &io, b, sizeof b, &off, 0);
	NtClose(h);
	if (!NT_SUCCESS(s) || io.Information < 2) return 0;
	if ((b[0] == 'M' && b[1] == 'Z') || (b[0] == '#' && b[1] == '!')) return 1;
#ifdef _NTLIBC_NATIVE_BUILD
	/* The sanitizer shim starts copied test images as their native ELF
	 * host binary.  Treat that native image signature exactly as the NT
	 * build treats MZ; this branch cannot enter a PE build. */
	if (io.Information >= 4 && b[0] == 0x7f && b[1] == 'E' &&
	    b[2] == 'L' && b[3] == 'F') return 1;
#endif
	return 0;
}

static int has_dir(const char *name)
{
	if (strchr(name, '/') || strchr(name, '\\')) return 1;
	if (((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':') return 1;
	return 0;
}

static char *try_dir(const char *dir, size_t dlen, const char *name)
{
	size_t nlen = strlen(name);
	/* dlen/nlen come from a PATH entry and the program name; the taint
	 * checker can't see that the allocation size below exactly matches
	 * what the writes that follow need (dlen + optional separator +
	 * nlen+1 + up to ".exe\0"), with no clamp needed since malloc simply
	 * fails on an absurd PATH rather than overflowing. */
	char *p = malloc(dlen + 1 + nlen + 4 + 1); // NOLINT(clang-analyzer-optin.taint.TaintedAlloc)
	if (!p) return 0;
	if (dlen) {
		memcpy(p, dir, dlen);
		if (p[dlen-1] != '/' && p[dlen-1] != '\\') p[dlen++] = '\\'; // NOLINT(clang-analyzer-security.ArrayBound)
	}
	memcpy(p + dlen, name, nlen + 1);
	if (access(p, X_OK) == 0 && __is_program(p)) return p;
	memcpy(p + dlen + nlen, ".exe", 5);
	if (access(p, X_OK) == 0 && __is_program(p)) return p;
	free(p);
	return 0;
}

char *__find_program(const char *name, int use_path)
{
	const char *path, *p;
	char *r;
	/* The empty string names nothing, and it has to be answered here
	 * rather than left to the search below.  exec.html's [ENOENT] is
	 * explicit -- "A component of path or file does not name an
	 * existing file or path or file is an empty string" -- but "" has
	 * no directory part, so has_dir() sends it into the PATH loop,
	 * where try_dir() appends it to a PATH entry and produces
	 * `<entry>\`: the directory itself, with nothing after it.
	 * A search implementation can otherwise accidentally turn it into
	 * `<entry>\`, so reject it before consulting PATH.  __is_program()
	 * also refuses directories, but this check stays: a shall-fail clause
	 * should not
	 * rest on an open flag two functions away, and musl rejects the
	 * empty string up front for the same reason.) */
	if (!name[0]) { errno = ENOENT; return 0; }
	if (!use_path || has_dir(name)) {
		r = malloc(strlen(name) + 1);
		if (r) strcpy(r, name);
		return r;
	}
	path = getenv("PATH");
	if (!path) path = "";
	p = path;
	for (;;) {
		const char *e = strchr(p, ';');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		r = try_dir(p, len, name);
		if (r) return r;
		if (!e) break;
		p = e + 1;
	}
	errno = ENOENT;
	return 0;
}
