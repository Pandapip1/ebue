/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* Hard links and symbolic links.  Hard links are FileLinkInformation.
 * Symbolic links need SeCreateSymbolicLinkPrivilege or developer mode on
 * Windows, so symlink tries and reports EPERM when it cannot; readlink
 * reads both NTFS symlinks and junctions. */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include "libc.h"

/* Offset of the union inside a REPARSE_DATA_BUFFER (8 on the wire, the
 * ReparseTag/ReparseDataLength/Reserved header that ReparseDataLength
 * does not count), and the offset of the symlink variant's PathBuffer
 * from the start of that union (12 on the wire).  Both are taken from
 * the struct as the compiler laid it out, because that is what the
 * writes below go through. */
#define RDB_HDR offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer)
#define SL_HDR  (offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) - RDB_HDR)

typedef struct _FILE_LINK_INFORMATION {
	BOOLEAN ReplaceIfExists;
	HANDLE RootDirectory;
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_LINK_INFORMATION;

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	FILE_LINK_INFORMATION *li;
	size_t sz;
	(void)flags;

	if (__ntpath_at(olddirfd, oldpath, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	if (__ntpath_at(newdirfd, newpath, &np, OBJ_CASE_INSENSITIVE) < 0) { NtClose(h); return -1; }
	sz = sizeof *li + np.nt.Length;
	li = __malloc(sz);
	if (!li) { NtClose(h); __ntpath_free(&np); return -1; }
	li->ReplaceIfExists = 0;
	li->RootDirectory = np.oa.RootDirectory;
	li->FileNameLength = np.nt.Length;
	memcpy(li->FileName, np.nt.Buffer, np.nt.Length);
	st = NtSetInformationFile(h, &io, li, (ULONG)sz, FileLinkInformation);
	__free(li);
	__ntpath_free(&np);
	NtClose(h);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int link(const char *a, const char *b) { return linkat(AT_FDCWD, a, AT_FDCWD, b, 0); }

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsz)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	char rb[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	REPARSE_DATA_BUFFER *r = (REPARSE_DATA_BUFFER *)rb;
	FILE_ATTRIBUTE_TAG_INFORMATION ti;
	const WCHAR *name;
	size_t nlen, i;
	WCHAR *tmp;
	int n;

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	/* [EINVAL] "The path argument names a file that is not a symbolic
	 * link".  Whether it is one is a file attribute, and asking for the
	 * attribute answers that question directly.  Deciding it from
	 * FSCTL_GET_REPARSE_POINT's status instead only works on a volume
	 * whose driver implements the FSCTL at all: one that does not (FAT,
	 * and several redirectors) refuses the request outright --
	 * STATUS_INVALID_DEVICE_REQUEST, STATUS_NOT_SUPPORTED -- rather than
	 * with STATUS_NOT_A_REPARSE_POINT, and every plain file on such a
	 * volume then reported that refusal's errno in place of EINVAL. */
	st = NtQueryInformationFile(h, &io, &ti, sizeof ti, FileAttributeTagInformation);
	if (NT_SUCCESS(st) && !(ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		NtClose(h);
		errno = EINVAL;
		return -1;
	}
	st = NtFsControlFile(h, 0, 0, 0, &io, FSCTL_GET_REPARSE_POINT, 0, 0, r, sizeof rb);
	NtClose(h);
	if (st == STATUS_NOT_A_REPARSE_POINT) { errno = EINVAL; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* PathBuffer + byteOffset/sizeof(WCHAR): converting an NT byte offset
	 * into a WCHAR element index before it is added to a WCHAR*, which
	 * pointer arithmetic then scales by sizeof(WCHAR) itself -- the
	 * correct idiom, not the double-scaling the check is looking for. */
	if (r->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
		name = r->SymbolicLinkReparseBuffer.PathBuffer + r->SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
		nlen = r->SymbolicLinkReparseBuffer.PrintNameLength / sizeof(WCHAR);
		if (!nlen) {
			name = r->SymbolicLinkReparseBuffer.PathBuffer + r->SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
			nlen = r->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
		}
	} else if (r->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
		name = r->MountPointReparseBuffer.PathBuffer + r->MountPointReparseBuffer.PrintNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
		nlen = r->MountPointReparseBuffer.PrintNameLength / sizeof(WCHAR);
		if (!nlen) {
			name = r->MountPointReparseBuffer.PathBuffer + r->MountPointReparseBuffer.SubstituteNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
			nlen = r->MountPointReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
		}
	} else if (r->ReparseTag == IO_REPARSE_TAG_LX_SYMLINK) {
		/* WSL symlink: a version dword followed by a UTF-8 target. */
		const char *t = (const char *)r->GenericReparseBuffer.DataBuffer + 4;
		size_t tl = r->ReparseDataLength - 4;
		if (tl > bufsz) tl = bufsz;
		memcpy(buf, t, tl);
		return (ssize_t)tl;
	} else {
		errno = EINVAL;
		return -1;
	}
	/* Strip a \??\ prefix and turn backslashes into slashes. */
	if (nlen >= 4 && name[0] == '\\' && name[1] == '?' && name[2] == '?' && name[3] == '\\') { name += 4; nlen -= 4; }
	tmp = __malloc((nlen + 1) * sizeof(WCHAR));
	if (!tmp) return -1;
	for (i = 0; i < nlen; i++) tmp[i] = name[i] == '\\' ? '/' : name[i];
	{
		char *u = __utf16_to_utf8(tmp, nlen);
		__free(tmp);
		if (!u) return -1;
		n = (int)strlen(u);
		if ((size_t)n > bufsz) n = (int)bufsz;
		memcpy(buf, u, n);
		__free(u);
	}
	return n;
}

ssize_t readlink(const char *path, char *buf, size_t bufsz) { return readlinkat(AT_FDCWD, path, buf, bufsz); }

int symlinkat(const char *target, int newdirfd, const char *linkpath)
{
	/* Creating a reparse point needs the link to exist first, then
	 * FSCTL_SET_REPARSE_POINT with a SYMLINK buffer.  Whether it is a
	 * file or directory link depends on the target, which may not exist:
	 * guess file unless the target is a directory now. */
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	WCHAR *wt;
	size_t tl, i, sz, off;
	REPARSE_DATA_BUFFER *r;
	int isdir = 0, relative;
	FILE_NETWORK_OPEN_INFORMATION ni;
	struct __ntpath tp;

	relative = !(target[0] == '/' || target[0] == '\\' || (target[0] && target[1] == ':'));
	if (__ntpath(target, &tp, OBJ_CASE_INSENSITIVE) == 0) {
		if (NT_SUCCESS(NtQueryFullAttributesFile(&tp.oa, &ni)) && (ni.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) isdir = 1;
		__ntpath_free(&tp);
	}

	if (__ntpath_at(newdirfd, linkpath, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtCreateFile(&h, FILE_WRITE_ATTRIBUTES | DELETE | SYNCHRONIZE, &np.oa, &io, 0, FILE_ATTRIBUTE_NORMAL,
	                  FILE_SHARE_VALID_FLAGS, FILE_CREATE,
	                  FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | (isdir ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE), 0, 0);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	wt = __utf8_to_utf16(target, &tl);
	if (!wt) { NtClose(h); return -1; }
	for (i = 0; i < tl; i++) if (wt[i] == '/') wt[i] = '\\';
	off = relative ? 0 : 4;
	/* Every length in a REPARSE_DATA_BUFFER is a USHORT counting bytes,
	 * and ReparseDataLength -- the largest of them -- covers the target
	 * twice, once as the substitute name and once as the print name.  A
	 * target long enough to overflow it would wrap rather than truncate,
	 * and the link would be created pointing somewhere else entirely, so
	 * the bound is checked before any of them is narrowed.
	 *
	 * SL_HDR/RDB_HDR come from offsetof rather than from the wire
	 * layout's 12 and 8: PathBuffer's distance from the start of the
	 * struct is whatever the compiler in use puts it at, and writing
	 * through the struct needs the allocation sized to that, not to the
	 * on-the-wire figure.  They agree on the NT target (ULONG is 32-bit
	 * there, so RDB_HDR is 8 and SL_HDR is 12); where ULONG is wider
	 * the wire figures are too small and the second memcpy below ran a
	 * WCHAR past the end of the buffer. */
	if (SL_HDR + (off + 2 * tl) * sizeof(WCHAR) > 0xffffu) {
		FILE_DISPOSITION_INFORMATION d = { 1 };
		NtSetInformationFile(h, &io, &d, sizeof d, FileDispositionInformation);
		NtClose(h);
		__free(wt);
		errno = ENAMETOOLONG;
		return -1;
	}
	sz = RDB_HDR + SL_HDR + (off + 2 * tl) * sizeof(WCHAR);
	r = __malloc(sz);
	if (!r) { __free(wt); NtClose(h); return -1; }
	memset(r, 0, sz);
	r->ReparseTag = IO_REPARSE_TAG_SYMLINK;
	r->SymbolicLinkReparseBuffer.Flags = relative ? SYMLINK_FLAG_RELATIVE : 0;
	{
		WCHAR *pb = r->SymbolicLinkReparseBuffer.PathBuffer;
		if (!relative) { pb[0] = '\\'; pb[1] = '?'; pb[2] = '?'; pb[3] = '\\'; }
		memcpy(pb + off, wt, tl * sizeof(WCHAR));
		r->SymbolicLinkReparseBuffer.SubstituteNameOffset = 0;
		r->SymbolicLinkReparseBuffer.SubstituteNameLength = (USHORT)((off + tl) * sizeof(WCHAR));
		memcpy(pb + off + tl, wt, tl * sizeof(WCHAR));
		r->SymbolicLinkReparseBuffer.PrintNameOffset = (USHORT)((off + tl) * sizeof(WCHAR));
		r->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)(tl * sizeof(WCHAR));
		r->ReparseDataLength = (USHORT)(SL_HDR + (off + 2 * tl) * sizeof(WCHAR));
	}
	st = NtFsControlFile(h, 0, 0, 0, &io, FSCTL_SET_REPARSE_POINT, r, r->ReparseDataLength + (ULONG)RDB_HDR, 0, 0);
	__free(r);
	__free(wt);
	if (!NT_SUCCESS(st)) {
		FILE_DISPOSITION_INFORMATION d = { 1 };
		NtSetInformationFile(h, &io, &d, sizeof d, FileDispositionInformation);
		NtClose(h);
		if (st == STATUS_PRIVILEGE_NOT_HELD || st == STATUS_ACCESS_DENIED) { errno = EPERM; return -1; }
		return __set_errno_status(st);
	}
	NtClose(h);
	return 0;
}

int symlink(const char *target, const char *linkpath) { return symlinkat(target, AT_FDCWD, linkpath); }
