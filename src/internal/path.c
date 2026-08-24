/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Turning the paths programs use into the paths the object manager wants.
 *
 * A program hands in UTF-8 with either kind of slash, relative or
 * absolute, possibly with a drive letter and possibly not; ntdll wants
 * UTF-16 in the \??\C:\... form, inside a UNICODE_STRING, inside an
 * OBJECT_ATTRIBUTES.  RtlDosPathNameToNtPathName_U does the hard part
 * (resolving relative paths against the current directory, . and ..,
 * per-drive current directories, UNC names); this file does the rest.
 *
 * A rooted path with no drive ("/usr/bin/sh") is taken relative to the
 * root of the current drive, which is the same thing Windows itself does
 * with "\usr\bin\sh".  "/dev/null" is the one Unix device name given a
 * meaning: it becomes NUL.  Everything else is passed through.
 */
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

static WCHAR *dos_from_posix(const char *path, size_t *wlen, int *trailing)
{
	WCHAR *w;
	size_t i, n;

	if (!strcmp(path, "/dev/null")) path = "NUL";
	else if (!strcmp(path, "/dev/tty")) path = "CON";
	else if (!strncmp(path, "/dev/", 5)) {
		/* /dev/stdin etc. are handled by open() via the fd table; anything
		 * else under /dev has no Windows counterpart. */
	}
	w = __utf8_to_utf16(path, &n);
	if (!w) return 0;
	for (i = 0; i < n; i++)
		if (w[i] == '/') w[i] = '\\';
	/* Strip a trailing slash: "dir/" must mean "dir" to NtCreateFile --
	 * but remember it was there ("root" paths like "/" or "C:\" do not
	 * count: they can only ever name a directory) so the caller can
	 * still reject the name if what it resolves to is not one. */
	if (trailing) *trailing = n > 1 && w[n-1] == '\\' && !(n == 3 && w[1] == ':');
	while (n > 1 && w[n-1] == '\\' && !(n == 3 && w[1] == ':')) w[--n] = 0;
	if (wlen) *wlen = n;
	return w;
}

/* access.html ERRORS ENOTDIR / open.html DESCRIPTION: a trailing slash
 * requires the resolved name to be a directory.  The slash itself is
 * already stripped from *out (NtCreateFile does not accept one), so this
 * re-checks the object type with a handle-less attribute query.  A name
 * that does not exist yet, or that a query cannot be answered for some
 * other reason, is left to whatever real operation the caller goes on to
 * do -- this only rejects a trailing slash on something that positively
 * exists and is not a directory. */
static int reject_if_not_dir(struct __ntpath *out)
{
	FILE_BASIC_INFORMATION bi;
	NTSTATUS st = NtQueryAttributesFile(&out->oa, &bi);
	if (NT_SUCCESS(st) && !(bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		__ntpath_free(out);
		errno = ENOTDIR;
		return -1;
	}
	return 0;
}

/* open.html (and stat, access, unlink, mkdir, utime, ... -- the clause is
 * boilerplate across the file-system surface) ERRORS [ENOTDIR]: "A
 * component of the path prefix names an existing file that is neither a
 * directory nor a symbolic link to a directory."
 *
 * NT gives no way to tell that apart from a prefix that simply is not
 * there: the object manager answers both with
 * STATUS_OBJECT_PATH_NOT_FOUND, which maps to ENOENT (right for the
 * second case, wrong for the first).  So the prefix is checked here, the
 * same way reject_if_not_dir() checks the last component for the
 * trailing-slash half of the very same clause: a handle-less attribute
 * query, and a verdict only when the answer is positive.
 *
 * Cost is one query for a path that has a prefix at all; the deeper
 * ancestors are only ever looked at once a nearer one has come back
 * missing, i.e. on a path that was going to fail regardless.  The walk
 * runs from the nearest ancestor outwards, so the first one that exists
 * decides: if it is a directory the whole prefix is a directory chain (a
 * directory's own parents cannot be anything else) and there is nothing
 * to report; if it is not, that is the POSIX ENOTDIR case.  Anything
 * else -- a query that fails for some other reason, a name with no
 * prefix to speak of -- is left to the real operation, exactly as
 * reject_if_not_dir() leaves it.
 *
 * root is the index of the first character that may be truncated at: for
 * an NT path "\??\C:\a\b" everything up to and including the drive's
 * backslash is fixed, and for a name relative to a RootDirectory handle
 * nothing is.
 *
 * A caveat for anyone testing this under Wine rather than on NT: Wine's
 * NtQueryAttributesFile (dlls/ntdll/unix/file.c) passes the Unix name
 * lookup_unix_name() built relative to the root handle straight to
 * get_file_info(), i.e. to a plain stat() against the *process* working
 * directory, so a RootDirectory-relative query answers about the wrong
 * file -- "not found", or positively about a same-named file in the cwd
 * if one happens to exist.  That hits the pre-existing
 * reject_if_not_dir() the same way (under Wine, openat(dirfd, "file/",
 * ...) is not rejected either), and NT resolves such a name properly --
 * ObOpenObjectByName is handed the whole OBJECT_ATTRIBUTES, root handle
 * included -- so the dirfd-relative half of both checks is a
 * real-Windows question, not a Wine one. */
static int reject_if_prefix_not_dir(struct __ntpath *out, size_t root)
{
	FILE_BASIC_INFORMATION bi;
	USHORT full = out->nt.Length;
	size_t i = full / sizeof(WCHAR);

	while (i > root) {
		NTSTATUS st;
		if (out->nt.Buffer[--i] != '\\') continue;
		out->nt.Length = (USHORT)(i * sizeof(WCHAR));
		st = NtQueryAttributesFile(&out->oa, &bi);
		out->nt.Length = full;
		if (NT_SUCCESS(st)) {
			if (bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
			__ntpath_free(out);
			errno = ENOTDIR;
			return -1;
		}
		if (st != STATUS_OBJECT_NAME_NOT_FOUND && st != STATUS_OBJECT_PATH_NOT_FOUND)
			return 0;
	}
	return 0;
}

/* Where reject_if_prefix_not_dir() may start truncating an NT path.  A
 * drive path ("\??\C:\...") may lose everything below "\??\C:\"; a name
 * that is not of that shape -- "\??\NUL", a UNC name ("\??\UNC\server\
 * share\..."), whose leading components are not files at all -- has no
 * prefix this can say anything about, and is reported as having none. */
static size_t nt_prefix_root(const UNICODE_STRING *nt)
{
	const WCHAR *b = nt->Buffer;
	size_t n = nt->Length / sizeof(WCHAR);

	if (n < 7 || b[0] != '\\' || b[1] != '?' || b[2] != '?' || b[3] != '\\') return n;
	if (!(((b[4] | 0x20) >= 'a' && (b[4] | 0x20) <= 'z') && b[5] == ':' && b[6] == '\\')) return n;
	return 7;
}

int __ntpath(const char *path, struct __ntpath *out, ULONG attributes)
{
	WCHAR *dos;
	size_t n;
	int trailing;
	NTSTATUS st;

	if (!path) { errno = EFAULT; return -1; }
	if (!*path) { errno = ENOENT; return -1; }

	dos = dos_from_posix(path, &n, &trailing);
	if (!dos) return -1;

	/* Same ceiling, and the same reason, as the hand-built UNICODE_STRING
	 * in __ntpath_at() below: a name past __US_MAX_WCHARS code units
	 * cannot be described by one at all.  POSIX wants ENAMETOOLONG for an
	 * over-long name, and reporting it here rather than letting the Rtl's
	 * failure fall into the catch-all ENOENT below is what makes every
	 * caller of this layer agree with chdir(), which has always checked
	 * its own hand-built string (src/unistd/chdir.c). */
	if (n > __US_MAX_WCHARS) { __free(dos); errno = ENAMETOOLONG; return -1; }

	memset(out, 0, sizeof *out);
	st = RtlDosPathNameToNtPathName_U_WithStatus(dos, &out->nt, 0, 0);
	if (!NT_SUCCESS(st)) {
		__free(dos);
		/* A relative name that fits on its own can still overflow once it
		 * is resolved against the current directory; the Rtl says so with
		 * STATUS_NAME_TOO_LONG, which is the same ENAMETOOLONG case. */
		errno = st == STATUS_NO_MEMORY ? ENOMEM :
			st == STATUS_NAME_TOO_LONG ? ENAMETOOLONG : ENOENT;
		return -1;
	}
	out->buf = out->nt.Buffer;
	out->dos = dos;
	InitializeObjectAttributes(&out->oa, &out->nt, attributes, 0, 0);
	if (trailing && reject_if_not_dir(out)) return -1;
	if (reject_if_prefix_not_dir(out, nt_prefix_root(&out->nt))) return -1;
	return 0;
}

int __ntpath_at(int dirfd, const char *path, struct __ntpath *out, ULONG attributes)
{
	int absolute;

	if (!path) { errno = EFAULT; return -1; }
	absolute = path[0] == '/' || path[0] == '\\' ||
		(((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':');
	if (dirfd == AT_FDCWD || absolute) return __ntpath(path, out, attributes);

	/* Relative to a directory handle: the object manager resolves a
	 * relative name against RootDirectory, so the name is given as-is,
	 * with slashes fixed and without the DOS->NT conversion.  An empty
	 * name ("") opens the directory itself. */
	{
		struct __fd *f = __fd_get(dirfd);
		WCHAR *w;
		size_t n;
		int trailing;
		if (!f) return -1;
		if (f->type != __FD_DIR) { errno = ENOTDIR; return -1; }
		w = dos_from_posix(path, &n, &trailing);
		if (!w) return -1;
		if (n == 1 && w[0] == '.') { w[0] = 0; n = 0; trailing = 0; }
		/* UNICODE_STRING.Length is a USHORT count of bytes and
		 * MaximumLength has to hold one more code unit, so a name past
		 * 32766 code units cannot be described -- and narrowing it would
		 * wrap rather than truncate, naming some prefix of the caller's
		 * path instead of failing. */
		if (n > __US_MAX_WCHARS) {
			__free(w);
			errno = ENAMETOOLONG;
			return -1;
		}
		memset(out, 0, sizeof *out);
		out->nt.Buffer = w;
		out->nt.Length = (USHORT)(n * sizeof(WCHAR));
		out->nt.MaximumLength = (USHORT)(out->nt.Length + sizeof(WCHAR));
		out->buf = 0;      /* w is freed as dos */
		out->dos = w;
		InitializeObjectAttributes(&out->oa, &out->nt, attributes, f->h, 0);
		if (trailing && reject_if_not_dir(out)) return -1;
		/* Relative to RootDirectory: every component of this name is a
		 * path prefix component, so the walk may truncate anywhere. */
		if (reject_if_prefix_not_dir(out, 0)) return -1;
		return 0;
	}
}

void __ntpath_free(struct __ntpath *p)
{
	if (p->buf) RtlFreeHeap(__process_heap(), 0, p->buf);
	if (p->dos) __free(p->dos);
	p->buf = 0; p->dos = 0;
}

/* The DOS path of an open handle: FileNameInformation gives the path
 * below the volume's device; the volume itself is found by matching the
 * device name against each drive letter's. That is what kernel32's
 * GetFinalPathNameByHandle does too.  Here the cheaper route is taken:
 * NtQueryObject's ObjectNameInformation gives the full NT name
 * (\Device\HarddiskVolume3\dir\file), and the drive is found by asking
 * each of A: through Z: for its target.  Returns a malloc'd UTF-8 path. */
NTSTATUS NTAPI NtOpenSymbolicLinkObject(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
NTSTATUS NTAPI NtQuerySymbolicLinkObject(HANDLE, PUNICODE_STRING, PULONG);

char *__handle_path(HANDLE h)
{
	char buf[sizeof(OBJECT_NAME_INFORMATION) + 2048 * sizeof(WCHAR)];
	OBJECT_NAME_INFORMATION *oni = (OBJECT_NAME_INFORMATION *)buf;
	ULONG len = 0;
	NTSTATUS st;
	WCHAR drive[7] = { '\\', '?', '?', '\\', 'A', ':', 0 };
	WCHAR target[512];
	UNICODE_STRING us, tus;
	OBJECT_ATTRIBUTES oa;
	int c;

	st = NtQueryObject(h, ObjectNameInformation, oni, sizeof buf, &len);
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return 0; }

	/* Under Wine (and in some other cases) ObjectNameInformation comes
	 * back already in \??\C:\... form instead of \Device\HarddiskVolumeN\...;
	 * such a name is already a drive path, so just strip the \??\ prefix
	 * rather than going through the device/symlink matching below, which
	 * only knows how to match \Device\... names. */
	{
		size_t nlen = oni->Name.Length / sizeof(WCHAR);
		WCHAR *nb = oni->Name.Buffer;
		if (nlen >= 6 && nb[0] == '\\' && nb[1] == '?' && nb[2] == '?' && nb[3] == '\\' &&
		    ((nb[4] >= 'A' && nb[4] <= 'Z') || (nb[4] >= 'a' && nb[4] <= 'z')) && nb[5] == ':') {
			return __utf16_to_utf8(nb + 4, nlen - 4);
		}
	}

	RtlInitUnicodeString(&us, drive);
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, 0, 0);
	for (c = 'A'; c <= 'Z'; c++) {
		HANDLE lh;
		ULONG tl;
		drive[4] = (WCHAR)c;
		us.Length = 6 * sizeof(WCHAR);
		if (!NT_SUCCESS(NtOpenSymbolicLinkObject(&lh, 0x1 /* SYMBOLIC_LINK_QUERY */, &oa))) continue;
		tus.Buffer = target; tus.Length = 0; tus.MaximumLength = sizeof target;
		st = NtQuerySymbolicLinkObject(lh, &tus, &tl);
		NtClose(lh);
		if (!NT_SUCCESS(st)) continue;
		tl = tus.Length / sizeof(WCHAR);
		if (oni->Name.Length / sizeof(WCHAR) >= tl &&
		    !memcmp(oni->Name.Buffer, target, tl * sizeof(WCHAR)) &&
		    (oni->Name.Length / sizeof(WCHAR) == tl || oni->Name.Buffer[tl] == '\\')) {
			size_t rest = oni->Name.Length / sizeof(WCHAR) - tl;
			WCHAR *w = __malloc((rest + 3) * sizeof(WCHAR));
			char *r;
			if (!w) return 0;
			w[0] = (WCHAR)c; w[1] = ':';
			memcpy(w + 2, oni->Name.Buffer + tl, rest * sizeof(WCHAR));
			if (!rest) { w[2] = '\\'; rest = 1; }
			r = __utf16_to_utf8(w, rest + 2);
			__free(w);
			return r;
		}
	}
	/* Not on a drive letter (a pipe, a UNC path): give the NT name. */
	return __utf16_to_utf8(oni->Name.Buffer, oni->Name.Length / sizeof(WCHAR));
}
