/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * setsockopt()/getsockopt():
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * setsockopt.html (both functions are on that one page).
 * "ENOPROTOOPT...The protocol does not support the option specified"
 * is the answer for everything not genuinely supportable here.
 *
 * What is genuinely supported, and why the rest is not:
 *
 *   - SO_REUSEADDR: recorded in struct __fd's `pad` byte
 *     (src/internal/afd.h's AFD_ST_REUSEADDR) and consumed by bind()
 *     (src/socket/bind.c) to pick AFD_SHARE_REUSE over
 *     AFD_SHARE_UNIQUE, exactly the option ReactOS's WSPBind
 *     (dll/win32/msafd/misc/dllmain.c) exposes the same way.  Setting
 *     it after bind() has already run has no effect -- AFD's share
 *     type is fixed at bind time -- same as Linux's own SO_REUSEADDR.
 *   - SO_TYPE (getsockopt only): SOCK_STREAM or SOCK_DGRAM, from the
 *     __SOCK_ST_DGRAM/AFD_ST_DGRAM bit socket()/socketpair() set at
 *     creation time (src/internal/plat_socket.h's banner) -- the only
 *     two types this project's socket() ever creates.
 *   - SO_ERROR (getsockopt only): this project does not implement
 *     non-blocking connect (see connect.c's banner), so there is no
 *     deferred/pending error to report; always 0, and (per
 *     setsockopt.html) reading it never clears anything because there
 *     is nothing pending to clear.
 *   - SO_SNDBUF/SO_RCVBUF (getsockopt only, added 2026-09-01 for
 *     SOCK_DGRAM): __plat_socket_getsndbuf()/__plat_socket_getrcvbuf()
 *     (src/internal/plat_socket.h) answer this per backend -- see that
 *     header's own comment for why the Linux answer is a real kernel
 *     value and the NT answer is a documented stand-in.  Added because
 *     third_party/ltp's aio_test.h setup_aio(), the shared fixture
 *     behind aio_cancel/2-1..7-1 and lio_listio/2-1, calls
 *     getsockopt(SO_SNDBUF) right after socketpair() and treats
 *     anything but success as a fatal setup failure (PTS_UNRESOLVED,
 *     not a pass/fail verdict) -- SOCK_DGRAM alone was not enough to
 *     get those cases running without this too.
 *
 * SO_LINGER, SO_RCVTIMEO/SO_SNDTIMEO, SO_KEEPALIVE, SO_BROADCAST,
 * SO_OOBINLINE and the rest all need either AFD_INFO queries/sets this
 * project does not implement or have no honest AFD-side answer without
 * them -- ENOPROTOOPT, not a silent no-op, so a caller relying on one
 * finds out rather than being told it worked.  setsockopt() itself gets
 * no SO_SNDBUF/SO_RCVBUF case for the same reason: this project cannot
 * honestly claim to have changed a buffer size neither backend actually
 * negotiates (Linux: no setsockopt(2) call is made; NT: there is
 * nothing to set -- see plat_socket.h).
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "afd.h"
#include "plat_socket.h"
#include "ownership_stubs.h"

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	int v;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (level != SOL_SOCKET) { errno = ENOPROTOOPT; return -1; }

	switch (optname) {
	case SO_REUSEADDR:
		if (!optval || optlen < (socklen_t)sizeof(int)) { errno = EINVAL; return -1; }
		__ownership_readable_span(optval, sizeof(v));
		memcpy(&v, optval, sizeof(v));
		if (v) f->pad |= AFD_ST_REUSEADDR; else f->pad &= ~AFD_ST_REUSEADDR;
		return 0;
	default:
		errno = ENOPROTOOPT;
		return -1;
	}
}

int getsockopt(int fd, int level, int optname, void *__restrict optval, socklen_t *__restrict optlen) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	int v;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (level != SOL_SOCKET) { errno = ENOPROTOOPT; return -1; }
	if (!optval || !optlen) { errno = EFAULT; return -1; }

	switch (optname) {
	case SO_REUSEADDR: v = (f->pad & AFD_ST_REUSEADDR) ? 1 : 0; break;
	case SO_TYPE:       v = (f->pad & AFD_ST_DGRAM) ? SOCK_DGRAM : SOCK_STREAM; break;
	case SO_ERROR:       v = 0; break;
	case SO_SNDBUF:
		v = __plat_socket_getsndbuf(f->h);
		if (v < 0) return -1; /* errno already set by the backend */
		break;
	case SO_RCVBUF:
		v = __plat_socket_getrcvbuf(f->h);
		if (v < 0) return -1;
		break;
	default:
		errno = ENOPROTOOPT;
		return -1;
	}

	{
		socklen_t n = *optlen < (socklen_t)sizeof(v) ? *optlen : (socklen_t)sizeof(v);
		__ownership_writable_span(optval, n);
		memcpy(optval, &v, n);
		*optlen = sizeof(v);
	}
	return 0;
}

// NOLINTEND(misc-include-cleaner)
