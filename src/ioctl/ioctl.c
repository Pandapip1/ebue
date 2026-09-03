/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ioctl(): a deliberately small, honest set of requests, not a general
 * escape hatch. Exactly three are recognised:
 *
 *   - FIONREAD: for __FD_PIPE, NtQueryInformationFile's ReadDataAvailable
 *     (the same field src/select/select.c's __fd_probe() queries, as a
 *     count instead of a boolean); for __FD_FILE, EndOfFile minus the
 *     current position. Anything else gets EINVAL, not a fabricated 0.
 *   - TIOCGWINSZ: on NT, kernel32's GetConsoleScreenBufferInfo() (gated
 *     on NTLIBC_USE_KERNEL32; no ntdll path exists), ENOTTY without it
 *     or on a non-console fd. On Linux it's a real ioctl(2)
 *     (src/ioctl/linux/plat_ioctl.c), with no fd-type pre-check since
 *     the kernel's own dispatch already answers ENOTTY for a non-tty.
 *   - FIONBIO: toggles O_NONBLOCK, the same flag fcntl(F_SETFL) flips.
 *     Pipes already return EAGAIN unconditionally on an empty read
 *     (src/unistd/read.c), so this stores the bit but enables no new
 *     blocking behavior.
 *
 * Every other request fails EINVAL — there is no silent no-op fallback
 * for an unknown request.
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

/* arg's unconditional dereferences below trust ioctl(2)'s own convention
 * that a caller issuing a pointer-taking request supplies a real pointer;
 * arg comes from va_arg() so there's no fixed parameter for `nonnull`
 * to attach to. */
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
