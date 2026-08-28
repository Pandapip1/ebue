/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An anonymous pipe is a named pipe with a name nobody else will guess,
 * which is exactly how kernel32's CreatePipe makes one.  The read end is
 * the server side, created with NtCreateNamedPipeFile; the write end is
 * an ordinary NtOpenFile of the same name.  Both are synchronous and
 * byte-stream, so read and write on them behave as they do on a file.
 */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

/* Creates the two handles of an anonymous pipe: the read end is the
 * server side (NtCreateNamedPipeFile), the write end an NtOpenFile of
 * the same name -- so a pipe write fd is always the CLIENT end, and its
 * write direction is the pipe's inbound direction.  Split out of pipe2()
 * so src/select/select.c's one-shot WriteQuotaAvailable capability probe
 * can make a pipe of its own without allocating fds.  `inherit` asks for
 * OBJ_INHERIT, i.e. the absence of O_CLOEXEC. */
NTSTATUS __pipe_handles(HANDLE *rp, HANDLE *wp, int inherit)
{
	static unsigned serial;
	WCHAR name[64];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	LARGE_INTEGER timeout = -1200000000LL;  /* 120s, the default */
	HANDLE r, w;
	NTSTATUS st;
	unsigned pid = (unsigned)(ULONG_PTR)__teb()->ClientId.UniqueProcess;
	unsigned n;
	const char pfx[] = "\\Device\\NamedPipe\\ntlibc.";
	int i = 0;

	for (; pfx[i]; i++) name[i] = (unsigned char)pfx[i];
	/* "<pid>.<serial>" in hex */
	for (n = 8; n > 0;) { n--; name[i++] = (unsigned char)"0123456789abcdef"[(pid >> (n * 4)) & 15]; }
	name[i++] = '.';
	serial++;
	for (n = 8; n > 0;) { n--; name[i++] = (unsigned char)"0123456789abcdef"[(serial >> (n * 4)) & 15]; }
	name[i] = 0;
	us.Buffer = name;
	/* Nothing here is caller-supplied: the name is the 25-character
	 * prefix, 8 hex digits of pid, a dot and 8 hex digits of serial --
	 * 42 code units, fixed.  It fits `name` and is three orders of
	 * magnitude below what the USHORT Length holds, so this narrowing
	 * cannot wrap.  sizearith-safe: fixed 42-code-unit name. */
	us.Length = (USHORT)(i * sizeof(WCHAR));
	/* sizearith-safe: same fixed name, one WCHAR longer. */
	us.MaximumLength = (USHORT)(us.Length + sizeof(WCHAR));
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | (inherit ? OBJ_INHERIT : 0), 0, 0);

	st = NtCreateNamedPipeFile(&r, GENERIC_READ | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &oa, &io,
	                           FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
	                           FILE_SYNCHRONOUS_IO_NONALERT,
	                           FILE_PIPE_BYTE_STREAM_TYPE, FILE_PIPE_BYTE_STREAM_MODE,
	                           FILE_PIPE_QUEUE_OPERATION, 1, 65536, 65536, &timeout);
	if (!NT_SUCCESS(st)) return st;

	st = NtOpenFile(&w, GENERIC_WRITE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &io,
	                FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (!NT_SUCCESS(st)) { NtClose(r); return st; }

	*rp = r;
	*wp = w;
	return STATUS_SUCCESS;
}

int pipe2(int fds[2], int flags)
{
	HANDLE r, w;
	int rfd, wfd;
	NTSTATUS st = __pipe_handles(&r, &w, !(flags & O_CLOEXEC));

	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	rfd = __fd_install(r, O_RDONLY | (flags & (O_CLOEXEC | O_NONBLOCK)), __FD_PIPE);
	if (rfd < 0) { NtClose(r); NtClose(w); return -1; }
	wfd = __fd_install(w, O_WRONLY | (flags & (O_CLOEXEC | O_NONBLOCK)), __FD_PIPE);
	if (wfd < 0) { close(rfd); NtClose(w); return -1; }
	fds[0] = rfd;
	fds[1] = wfd;
	return 0;
}

int pipe(int fds[2])
{
	return pipe2(fds, 0);
}
