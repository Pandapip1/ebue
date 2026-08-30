/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_stdio.h -- see that header for
 * the contract each function makes.  Everything here was, until this
 * file existed, inline inside src/stdio/misc.c's renameat(); nothing
 * changed in substance, only location and the addition of a POSIX-
 * shaped return (errno already set) in place of a raw NTSTATUS.
 */
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_stdio.h"

int __plat_rename_open_old(struct __ntpath *op, __plat_handle_t *h_out,
                           unsigned long *attrs, unsigned long *tag)
{
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	FILE_ATTRIBUTE_TAG_INFORMATION oti;

	/* FILE_READ_ATTRIBUTES is requested alongside DELETE because the
	 * type check below queries FileBasicInformation on this same handle
	 * to learn whether old is a directory. DELETE alone is enough for
	 * the rename itself (FileRenameInformation's IopSetOperationAccess
	 * entry is DELETE), so adding FILE_READ_ATTRIBUTES here is purely
	 * additive and cannot newly deny the open. */
	st = NtOpenFile(&h, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &op->oa, &io,
	                FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	*attrs = 0; *tag = 0;
	if (NT_SUCCESS(NtQueryInformationFile(h, &io, &oti, sizeof oti, FileAttributeTagInformation))) {
		*attrs = oti.FileAttributes;
		*tag = oti.ReparseTag;
	}
	*h_out = h;
	return 0;
}

void __plat_query_new_attrs(struct __ntpath *np, int *exists,
                            unsigned long *attrs, unsigned long *tag)
{
	IO_STATUS_BLOCK io;
	HANDLE nh;
	NTSTATUS st;
	FILE_ATTRIBUTE_TAG_INFORMATION nti;

	st = NtOpenFile(&nh, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np->oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	*exists = NT_SUCCESS(st);
	*attrs = 0; *tag = 0;
	if (*exists) {
		if (NT_SUCCESS(NtQueryInformationFile(nh, &io, &nti, sizeof nti, FileAttributeTagInformation))) {
			*attrs = nti.FileAttributes;
			*tag = nti.ReparseTag;
		}
		NtClose(nh);
	}
}

int __plat_rename_set(__plat_handle_t h, struct __ntpath *np, int old_isdir, int new_isdir)
{
	IO_STATUS_BLOCK io;
	FILE_RENAME_INFORMATION *ri;
	NTSTATUS st;
	size_t bufsz = sizeof(FILE_RENAME_INFORMATION) + np->nt.Length;

	ri = __malloc(bufsz);
	if (!ri) { NtClose(h); errno = ENOMEM; return -1; }
	ri->Flags = FILE_RENAME_REPLACE_IF_EXISTS | FILE_RENAME_POSIX_SEMANTICS;
	/* np->oa.RootDirectory is the "resolve FileName against this
	 * directory" handle __ntpath_at() put there for a newfd-relative
	 * destination -- FILE_RENAME_INFORMATION's RootDirectory is the
	 * same mechanism as OBJECT_ATTRIBUTES'.  0 for an absolute path or
	 * AT_FDCWD, where np->nt is already a full NT path. */
	ri->RootDirectory = np->oa.RootDirectory;
	ri->FileNameLength = np->nt.Length;
	memcpy(ri->FileName, np->nt.Buffer, np->nt.Length);

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
	 * here.  Disambiguate by type, using the types established before
	 * the set was attempted. */
	if (st == STATUS_ACCESS_DENIED && new_isdir) {
		NtClose(h);
		errno = old_isdir ? ENOTEMPTY : EISDIR;
		return -1;
	}

	NtClose(h);
	if (st == STATUS_NOT_SAME_DEVICE) { errno = EXDEV; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
