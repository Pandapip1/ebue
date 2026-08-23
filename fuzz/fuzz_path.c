/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __ntpath()/__ntpath_at() in src/internal/path.c -- the translation
 * every path-taking call (open, stat, access, unlink, rename, chmod,
 * link, opendir, spawn's program lookup) goes through on the way from a
 * caller's UTF-8 to the UNICODE_STRING/OBJECT_ATTRIBUTES the object
 * manager wants.  It does its own UTF-8->UTF-16 conversion, drive-letter
 * and separator rewriting, and trailing-slash handling, all ahead of any
 * real NT call -- exactly the kind of buffer arithmetic a fuzzer finds
 * bugs in that directed testing does not.
 *
 * struct __ntpath and the three functions are declared in
 * src/internal/libc.h, which -I$(srcdir)/src/internal already puts on
 * this harness's include path, so no local prototypes are needed here
 * (unlike __spawn in test/misc.c, which really is undeclared anywhere
 * public).
 *
 * The one property checked positively rather than just left to ASan: a
 * UNICODE_STRING.Length is a USHORT *byte* count, so a name of more than
 * __US_MAX_WCHARS (32766) UTF-16 code units cannot be described --
 * narrowing it would wrap, not truncate, and hand the object manager a
 * prefix of a different path.  __ntpath_at enforces this explicitly
 * (src/internal/path.c); __ntpath goes through
 * RtlDosPathNameToNtPathName_U_WithStatus, whose real implementation
 * fails an over-length name with STATUS_NAME_TOO_LONG on its own, and
 * fuzz/ntstubs.c's simulated one already rejects it the same way (the
 * `len * sizeof(WCHAR) > 0xfffe` check there).  Either way, success must
 * never come back with more code units than the ceiling allows.
 */
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

/* A directory handle for __ntpath_at to resolve relative names against.
 * "/tmp" exists in fuzz/ntstubs.c's simulated volume from start-up. */
static int dirfd(void)
{
	static int fd = -2;
	if (fd == -2) fd = open("/tmp", O_RDONLY | O_DIRECTORY);
	return fd;
}

static void check_result(const char *what, const char *path, struct __ntpath *np)
{
	size_t units = np->nt.Length / sizeof(WCHAR);
	if (units > __US_MAX_WCHARS)
		oracle_mismatch_i(what, path, (long long)units, (long long)__US_MAX_WCHARS);
	if (np->nt.MaximumLength < np->nt.Length)
		oracle_mismatch_i("MaximumLength < Length", path,
		                  (long long)np->nt.MaximumLength, (long long)np->nt.Length);
	/* The buffer really has to hold what Length claims: read every byte
	 * NtCreateFile would, so ASan can catch an over-claimed length even
	 * though nothing here formats it. */
	if (units) {
		volatile WCHAR sink = 0;
		size_t i;
		for (i = 0; i < units; i++) sink ^= np->nt.Buffer[i];
		(void)sink;
	}
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char buf[4096];
	size_t n = size < sizeof buf - 1 ? size : sizeof buf - 1;
	struct __ntpath np;
	int fd;

	if (!n) return 0;
	memcpy(buf, data, n);
	buf[n] = 0;
	if (memchr(buf, 0, n)) return 0;       /* embedded NUL: not one string */

	/* ---- __ntpath: absolute resolution, drive letters, /dev/null ---- */
	if (__ntpath(buf, &np, 0) == 0) {
		check_result("__ntpath", buf, &np);
		__ntpath_free(&np);
	}

	/* ---- __ntpath_at, AT_FDCWD: must behave exactly like __ntpath ---- */
	if (__ntpath_at(AT_FDCWD, buf, &np, 0) == 0) {
		check_result("__ntpath_at(AT_FDCWD)", buf, &np);
		__ntpath_free(&np);
	}

	/* ---- __ntpath_at, a real directory handle: the relative-name path,
	 * which skips the DOS->NT rewrite and goes straight to slash-fixing
	 * and length-checking. ---------------------------------------------- */
	fd = dirfd();
	if (fd >= 0 && __ntpath_at(fd, buf, &np, 0) == 0) {
		check_result("__ntpath_at(dirfd)", buf, &np);
		__ntpath_free(&np);
	}

	/* ---- a bad dirfd must fail cleanly, not crash ---- */
	if (__ntpath_at(12345, buf, &np, 0) == 0) __ntpath_free(&np);

	return 0;
}
