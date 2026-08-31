/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __handle_path() -- moved verbatim out of src/internal/path.c (see
 * src/internal/nt/path.c's own banner for the fuller story of why that
 * file split in two). This half, unlike the rest of path.c, has a
 * genuinely portable POSIX-shaped meaning: "the path of an already-open
 * descriptor", which src/stat/chmod.c's fchmod() (EACCES retry: reopen
 * by name with FILE_WRITE_EA rights), src/unistd/chdir.c's fchdir(),
 * src/process/exec.c (re-exec by path) and src/stdlib/realpath.c all
 * call unconditionally from the portable front door -- never gated
 * behind an nt/ directory the way __ntpath()'s own callers are. That
 * unconditional call is exactly what broke the native-Linux OPTS build
 * (shm_open/shm_unlink/mmap and everything transitively reaching
 * fchmod()'s EACCES path pulled in this file's NtQueryObject/
 * NtOpenSymbolicLinkObject/NtQuerySymbolicLinkObject/NtClose/
 * RtlInitUnicodeString/__peb/RtlFreeHeap chain even though none of
 * those three test programs ever hits an EACCES retry -- the linker
 * cannot know that, only that fchmod() might call it), so it now has a
 * real Linux counterpart (src/internal/linux/handle_path.c) instead.
 *
 * The DOS path of an open handle: FileNameInformation gives the path
 * below the volume's device; the volume itself is found by matching the
 * device name against each drive letter's. That is what kernel32's
 * GetFinalPathNameByHandle does too.  Here the cheaper route is taken:
 * NtQueryObject's ObjectNameInformation gives the full NT name
 * (\Device\HarddiskVolume3\dir\file), and the drive is found by asking
 * each of A: through Z: for its target.  Returns a malloc'd UTF-8 path.
 */
#include <string.h>
#include "libc.h"

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
	 * only knows how to match \Device\... names.
	 *
	 * nb[0..5] below is a disclosed, deliberately unmarked residual: nb
	 * is oni->Name.Buffer, a struct field's own value populated by the
	 * NtQueryObject() call just above -- a fact about that call's own
	 * output, several statements removed from h (this function's only
	 * parameter), the same "value derived from an external syscall's
	 * own output struct, not expressible via nonnull on any parameter"
	 * class this tree's own src/signal/linux/plat_signal.c
	 * open_shared_stop_event() comment already established for a
	 * different subsystem. Every access is short-circuit-guarded by
	 * `nlen >= 6` first, and a successful NtQueryObject(
	 * ObjectNameInformation) always populates a UNICODE_STRING whose
	 * Buffer is non-NULL whenever Length > 0 (documented NT behavior,
	 * not this tree's own guard). */
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
