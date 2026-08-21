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

static WCHAR *dos_from_posix(const char *path, size_t *wlen)
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
	/* Strip a trailing slash: "dir/" must mean "dir" to NtCreateFile. */
	while (n > 1 && w[n-1] == '\\' && !(n == 3 && w[1] == ':')) w[--n] = 0;
	if (wlen) *wlen = n;
	return w;
}

int __ntpath(const char *path, struct __ntpath *out, ULONG attributes)
{
	WCHAR *dos;
	size_t n;
	NTSTATUS st;

	if (!path) { errno = EFAULT; return -1; }
	if (!*path) { errno = ENOENT; return -1; }

	dos = dos_from_posix(path, &n);
	if (!dos) return -1;

	memset(out, 0, sizeof *out);
	st = RtlDosPathNameToNtPathName_U_WithStatus(dos, &out->nt, 0, 0);
	if (!NT_SUCCESS(st)) {
		__free(dos);
		errno = st == STATUS_NO_MEMORY ? ENOMEM : ENOENT;
		return -1;
	}
	out->buf = out->nt.Buffer;
	out->dos = dos;
	InitializeObjectAttributes(&out->oa, &out->nt, attributes, 0, 0);
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
		if (!f) return -1;
		if (f->type != __FD_DIR) { errno = ENOTDIR; return -1; }
		w = dos_from_posix(path, &n);
		if (!w) return -1;
		if (n == 1 && w[0] == '.') { w[0] = 0; n = 0; }
		memset(out, 0, sizeof *out);
		out->nt.Buffer = w;
		out->nt.Length = (USHORT)(n * sizeof(WCHAR));
		out->nt.MaximumLength = out->nt.Length + sizeof(WCHAR);
		out->buf = 0;      /* w is freed as dos */
		out->dos = w;
		InitializeObjectAttributes(&out->oa, &out->nt, attributes, f->h, 0);
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
typedef struct { UNICODE_STRING Name; WCHAR Buffer[1]; } OBJECT_NAME_INFORMATION;
NTSTATUS NTAPI NtQueryObject(HANDLE, ULONG, PVOID, ULONG, PULONG);
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

	st = NtQueryObject(h, 1 /* ObjectNameInformation */, oni, sizeof buf, &len);
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
