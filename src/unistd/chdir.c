/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

int chdir(const char *path)
{
	WCHAR *w;
	size_t n, i;
	UNICODE_STRING us;
	NTSTATUS st;

	if (!path || !*path) { errno = ENOENT; return -1; }
	/* chdir.html ERRORS, shall fail: "[ENAMETOOLONG] The length of a
	 * component of a pathname is longer than {NAME_MAX}."  chdir does
	 * not go through src/internal/path.c's builder -- it hand-builds a
	 * UNICODE_STRING for RtlSetCurrentDirectory_U -- so it has to ask
	 * for itself, or it would be the one path-taking interface in the
	 * library without the check.  Distinct from the whole-path bound a
	 * few lines below; see __name_too_long()'s banner. */
	if (__name_too_long(path)) { errno = ENAMETOOLONG; return -1; }
	w = __utf8_to_utf16(path, &n);
	if (!w) return -1;
	for (i = 0; i < n; i++) if (w[i] == '/') w[i] = '\\';
	/* The path goes into a UNICODE_STRING, whose Length is a USHORT
	 * counting bytes; a longer path would wrap rather than truncate and
	 * we would chdir into some prefix of what was asked for. */
	if (n > __US_MAX_WCHARS) { __free(w); errno = ENAMETOOLONG; return -1; }
	us.Buffer = w;
	us.Length = (USHORT)(n * sizeof(WCHAR));
	us.MaximumLength = (USHORT)(us.Length + sizeof(WCHAR));
	st = RtlSetCurrentDirectory_U(&us);
	/* chdir.html ERRORS [ENOTDIR]: "A component of the path prefix names
	 * an existing file that is neither a directory nor a symbolic link to
	 * a directory."  RtlSetCurrentDirectory_U passes NtOpenFile's status
	 * through, so a non-directory *last* component already arrives as
	 * STATUS_NOT_A_DIRECTORY and needs nothing here; but a non-directory
	 * *prefix* component and a missing one are byte-identical --
	 * STATUS_OBJECT_PATH_NOT_FOUND for both, measured on Windows 11 Pro
	 * 22621 on NTFS -- so no status remap can tell them apart and the
	 * prefix has to be walked.  Only that one status is disambiguated, so
	 * a successful chdir() is still the one call it always was.
	 *
	 * The walk wants an NT path, which this function does not otherwise
	 * build (RtlSetCurrentDirectory_U takes the DOS form), so it is
	 * converted here, on a path that has already failed. */
	if (st == STATUS_OBJECT_PATH_NOT_FOUND) {
		UNICODE_STRING nt;
		if (NT_SUCCESS(RtlDosPathNameToNtPathName_U_WithStatus(w, &nt, 0, 0))) {
			int notdir = __nt_prefix_not_dir(&nt, 0);
			RtlFreeHeap(__process_heap(), 0, nt.Buffer);
			if (notdir) { __free(w); errno = ENOTDIR; return -1; }
		}
	}
	__free(w);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int fchdir(int fd)
{
	char *p = __handle_path(__fd_handle(fd));
	int r;
	if (!p) return -1;
	r = chdir(p);
	__free(p);
	return r;
}
