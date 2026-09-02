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
 *   - TIOCGWINSZ: terminal size. Platform-split, like termios.c's own
 *     ISIG/ICANON/ECHO: on NT, from kernel32's
 *     GetConsoleScreenBufferInfo() (srWindow's extent -- the visible
 *     window, which is what a real terminal's "size" means to a
 *     program, not the scrollback buffer's dwSize), NTLIBC_USE_KERNEL32
 *     only -- no ntdll path to console screen-buffer info exists
 *     (CONTRIBUTING.md). Without it, or on any non-console fd, ENOTTY.
 *     On Linux there is no kernel32-equivalent escape hatch to gate
 *     behind at all: it is a standard, unconditional real ioctl(2)
 *     (src/ioctl/linux/plat_ioctl.c's __plat_tiocgwinsz(), via
 *     src/internal/plat_ioctl.h) against whatever fd is given, real on
 *     any genuine tty/pty and ENOTTY -- the BSD-equivalent answer for
 *     "this isn't a terminal" -- on anything else, sourced from the
 *     kernel's own ioctl_tty(2) dispatch rather than a pre-check of fd
 *     metadata here (see that file's own comment on why: Linux folds
 *     every character device, tty and non-tty alike, into one
 *     __FD_CHAR bucket, so this file has no fd-type test that could
 *     tell a real terminal from /dev/null the way __FD_CONSOLE alone
 *     already does on NT).
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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_ioctl.h"
#ifdef NTLIBC_USE_KERNEL32
#include "kernel32.h"
#endif

static int fionread_pipe(struct __fd *f, int *out)
{
	return __plat_fionread_pipe(f->h, out);
}

static int fionread_file(struct __fd *f, int *out)
{
	long long eof, pos;
	if (__plat_file_eof_and_pos(f->h, &eof, &pos) < 0) return -1;
	if (!__file_remaining_count(eof, pos, out)) {
		errno = EOVERFLOW;
		return -1;
	}
	return 0;
}

/* arg's two unconditional dereferences below (`*(int *)arg` in the
 * FIONREAD and FIONBIO cases) are a disclosed, deliberately unmarked
 * residual: arg is not a named parameter at all, only a value pulled
 * out of ioctl()'s own trailing `...` via va_arg() -- there is no
 * parameter POSITION for `nonnull` (which only ever describes a
 * function's own fixed, named parameters) to attach to. This is the
 * same shape every variadic POSIX call with a request-dependent third
 * argument has (fcntl(), open()'s mode); trusted the same way this
 * project already trusts ioctl(2)'s own real-world convention that a
 * caller issuing a pointer-taking request (FIONREAD, FIONBIO,
 * TIOCGWINSZ) supplies a real pointer for it, not something this
 * function's own body could ever validate. */
int ioctl(int fd, unsigned long req, ...) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
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
#ifdef __linux__
		/* No fd-type pre-check here -- see this file's own banner:
		 * __plat_tiocgwinsz() issues a real ioctl(2), and the kernel's
		 * own ioctl_tty(2) dispatch already answers ENOTTY for any fd
		 * that is not a genuine terminal, which is the real, load-
		 * bearing check (a __FD_CHAR pre-filter here could only ever
		 * be a coarse approximation of that, since __FD_CHAR also
		 * covers /dev/null and friends). */
		return __plat_tiocgwinsz(f->h, (struct winsize *)arg);
#else
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
			procname.Length = procname.MaximumLength =
				sizeof "GetConsoleScreenBufferInfo" - 1;
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

// NOLINTEND(misc-include-cleaner)
