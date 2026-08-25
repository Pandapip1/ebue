/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ioctl(): a deliberately small, honest set of requests, not a general
 * escape hatch. Exactly three are recognised:
 *
 *   - FIONREAD: bytes immediately readable. For __FD_PIPE, this reuses
 *     the exact NtQueryInformationFile(FilePipeLocalInformation)
 *     ReadDataAvailable field src/select/select.c's __fd_probe()
 *     already queries to answer "is this pipe readable" -- same call,
 *     same field, just returned as a count instead of a boolean; not
 *     duplicated logic, the identical NT mechanism. (That file also
 *     reads WriteQuotaAvailable from the same structure, gated on a
 *     one-shot capability probe because wine-9.0 and older hardcode it
 *     to 0 -- irrelevant here since FIONREAD only ever asks about the
 *     read side, but worth restating: nothing in this file leans on
 *     that field either.) For __FD_FILE, it is
 *     bytes remaining until EOF (FileStandardInformation's EndOfFile
 *     minus FilePositionInformation's CurrentByteOffset) -- a real,
 *     if less commonly needed, answer. Anything else (a console, a
 *     directory, a character device, a socket) has no meaningful
 *     "bytes immediately available" concept in this library today and
 *     gets EINVAL, not a fabricated 0.
 *   - TIOCGWINSZ: terminal size, from kernel32's
 *     GetConsoleScreenBufferInfo() (srWindow's extent -- the visible
 *     window, which is what a real terminal's "size" means to a
 *     program, not the scrollback buffer's dwSize). NTLIBC_USE_KERNEL32
 *     only, same reason as termios.c's ISIG/ICANON/ECHO: no ntdll path
 *     to console screen-buffer info exists (CONTRIBUTING.md). Without
 *     it, or on any non-console fd, ENOTTY -- the BSD-equivalent
 *     answer for "this isn't a terminal" (Linux's ioctl_tty(2) family
 *     uses ENOTTY the same way for a request that only makes sense on
 *     a tty, issued against something that is not one).
 *   - FIONBIO: toggles O_NONBLOCK on the descriptor, the same flag
 *     fcntl(F_SETFL) already flips (src/fcntl/fcntl.c). Documented
 *     honestly, not oversold: O_NONBLOCK today only changes what
 *     fcntl(F_GETFL) reports back -- src/unistd/read.c's pipe path
 *     already returns EAGAIN on an empty pipe unconditionally,
 *     regardless of this flag (pipes are effectively always
 *     non-blocking at the read() level in this library; select()/
 *     poll() are the intended way to wait on one). So FIONBIO is real
 *     in the sense that it stores the same bit fcntl() does, but does
 *     not newly enable or disable any blocking behaviour that did not
 *     already exist.
 *
 * Every other request -- there is no registry of "known but
 * unsupported" requests to silently swallow -- fails EINVAL. An
 * ioctl() that accepts an unknown request and does nothing is a trap
 * (a caller that checks the return value is fine; a caller that does
 * not gets silently wrong behaviour), so this never does that.
 */
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#ifdef NTLIBC_USE_KERNEL32
#include "kernel32.h"
#endif

static int fionread_pipe(struct __fd *f, int *out)
{
	IO_STATUS_BLOCK io;
	FILE_PIPE_LOCAL_INFORMATION pli;
	NTSTATUS st = NtQueryInformationFile(f->h, &io, &pli, sizeof pli, FilePipeLocalInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*out = (int)pli.ReadDataAvailable;
	return 0;
}

static int fionread_file(struct __fd *f, int *out)
{
	IO_STATUS_BLOCK io;
	FILE_STANDARD_INFORMATION si;
	FILE_POSITION_INFORMATION pi;
	NTSTATUS st;
	long long remain;

	st = NtQueryInformationFile(f->h, &io, &si, sizeof si, FileStandardInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	st = NtQueryInformationFile(f->h, &io, &pi, sizeof pi, FilePositionInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	remain = si.EndOfFile - pi.CurrentByteOffset;
	*out = remain > 0 ? (remain > 0x7fffffff ? 0x7fffffff : (int)remain) : 0;
	return 0;
}

int ioctl(int fd, unsigned long req, ...)
{
	struct __fd *f = __fd_get(fd);
	va_list ap;
	void *arg;

	if (!f) return -1;
	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);

	switch (req) {
	case FIONREAD: {
		int n = 0;
		if (f->type == __FD_PIPE) { if (fionread_pipe(f, &n) < 0) return -1; }
		else if (f->type == __FD_FILE) { if (fionread_file(f, &n) < 0) return -1; }
		else { errno = EINVAL; return -1; }
		*(int *)arg = n;
		return 0;
	}
	case TIOCGWINSZ: {
		if (f->type != __FD_CONSOLE) { errno = ENOTTY; return -1; }
#ifdef NTLIBC_USE_KERNEL32
		{
			struct winsize *ws = arg;
			PVOID dll, proc;
			UNICODE_STRING dllname;
			ANSI_STRING procname;
			CONSOLE_SCREEN_BUFFER_INFO info;

			RtlInitUnicodeString(&dllname, L"kernel32.dll");
			if (!NT_SUCCESS(LdrLoadDll(0, 0, &dllname, &dll))) { errno = ENOTTY; return -1; }
			procname.Buffer = "GetConsoleScreenBufferInfo";
			/* A 26-byte string literal assigned on the line above, not
			 * anything a caller supplies, so this narrowing to the
			 * ANSI_STRING's USHORT lengths cannot wrap.
			 * USHORT-safe: 26-byte string literal. */
			procname.Length = procname.MaximumLength = (USHORT)strlen(procname.Buffer);
			if (!NT_SUCCESS(LdrGetProcedureAddress(dll, &procname, 0, &proc))) { errno = ENOTTY; return -1; }
			if (!((BOOL (NTAPI *)(HANDLE, CONSOLE_SCREEN_BUFFER_INFO *))proc)(f->h, &info)) { errno = ENOTTY; return -1; }
			ws->ws_col = (unsigned short)(info.srWindow.Right - info.srWindow.Left + 1);
			ws->ws_row = (unsigned short)(info.srWindow.Bottom - info.srWindow.Top + 1);
			ws->ws_xpixel = 0;
			ws->ws_ypixel = 0;
			return 0;
		}
#else
		/* No ntdll path to console screen-buffer info exists
		 * (CONTRIBUTING.md); NTLIBC_USE_KERNEL32 is required. */
		errno = ENOTTY;
		return -1;
#endif
	}
	case FIONBIO: {
		int on = *(int *)arg;
		f->flags = on ? (f->flags | O_NONBLOCK) : (f->flags & ~(unsigned)O_NONBLOCK);
		return 0;
	}
	default:
		errno = EINVAL;
		return -1;
	}
}
