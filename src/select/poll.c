/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * poll(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * poll.html.  Shares select.c's per-descriptor readiness probe
 * (__fd_probe()) and wait/sleep primitive (__fd_wait_or_delay()) --
 * see that file's banner for the wait-vs-poll design, the 20ms
 * pipe-poll interval and why, and the EINTR-never-happens note (same
 * reasoning applies here unchanged: this library's signal delivery is
 * synchronous only). The outer loop here is not shared with select's:
 * it walks a caller-supplied struct pollfd array directly rather than
 * an nfds-sized fd_set bit range, which is a different enough shape
 * that forcing them through one loop would have cost more code than
 * it saved.
 *
 * revents per DESCRIPTION: POLLIN/POLLRDNORM mirror this library's
 * "readable" probe, POLLOUT/POLLWRNORM mirror "writable"; POLLPRI/
 * POLLRDBAND/POLLWRBAND (priority/band data) have no analogue on any
 * of this library's descriptor shapes and are never set, which is
 * conformant -- POSIX only requires reporting conditions that exist.
 * POLLHUP is set whenever __fd_probe() reports the peer end gone (its
 * *hup out-param): a broken/disconnected pipe, or a socket AFD reports
 * closed/aborted/disconnected -- and also a socket whose probe ioctl
 * itself failed, which __fd_probe() deliberately treats as "ready and
 * hung up" rather than "never ready" (see its comment there).  POLLERR
 * is never set here since none of the shapes select()/poll() cover
 * today can report a device error without first reporting EOF/broken
 * via read()/write() itself.
 * POLLNVAL marks an fd that is not open, and -- per DESCRIPTION --
 * counts toward the return value the same as any other revents hit;
 * a negative fd is the one case revents is left untouched at 0 and
 * does not count, since DESCRIPTION says such an entry is ignored
 * outright.
 *
 * The `timeout` millisecond parameter follows exactly the null/zero
 * distinction select.c documents for struct timeval, just shifted
 * one level: -1 is "block indefinitely" (poll.html: "a negative value
 * ... shall cause poll() to block until a requested event occurs or
 * until the call is interrupted"), 0 polls without blocking ("shall
 * return immediately"), and a positive value bounds the wait,
 * converted here from milliseconds to the same 100ns tick unit
 * select.c's core already works in.
 */
#include <poll.h>
#include <errno.h>
#include "libc.h"
#include "plat_select.h"

#define POLL_INTERVAL_TICKS 200000LL  /* 20ms; see select.c's file banner */

int poll(struct pollfd *pfds, nfds_t nfds, int timeout)
{
	long long remaining;
	int infinite, total;

	infinite = timeout < 0;
	remaining = infinite ? 0 : (long long)timeout * 10000LL;  /* ms -> 100ns ticks */

	for (;;) {
		__plat_handle_t console_h[FD_MAX];
		int console_idx[FD_MAX];
		int ncons = 0, have_poll = 0;
		nfds_t i;

		total = 0;
		for (i = 0; i < nfds; i++) {
			struct pollfd *p = &pfds[i];
			struct __fd *f;
			int cr, cw, hup;

			p->revents = 0;
			if (p->fd < 0) continue;  /* DESCRIPTION: ignored outright */

			f = __fd_get(p->fd);
			if (!f) { p->revents = POLLNVAL; total++; continue; }

			if (f->type == __FD_PIPE || f->type == __FD_SOCKET) {
				/* The two shapes with a real instantaneous
				 * answer, and no waitable NT object behind
				 * either -- so both are re-probed on the
				 * POLL_INTERVAL_TICKS timer.  Same routing as
				 * select.c's poll_pass(); see that file's
				 * banner for why it is by probeability rather
				 * than by one named type. */
				have_poll = 1;
				__fd_probe(f, &cr, &cw, &hup);
				if (hup) p->revents |= POLLHUP;
				if (cr) p->revents |= (short)(p->events & (POLLIN | POLLRDNORM));
				if (cw && !hup) p->revents |= (short)(p->events & (POLLOUT | POLLWRNORM));
			} else if (f->type == __FD_CONSOLE) {
				p->revents |= (short)(p->events & (POLLOUT | POLLWRNORM));  /* output: always ready */
				if ((p->events & (POLLIN | POLLRDNORM)) && ncons < FD_MAX) {
					console_h[ncons] = f->h;
					console_idx[ncons] = (int)i;
					ncons++;
				}
			} else {
				/* __FD_FILE/__FD_DIR/__FD_CHAR/__FD_UNKNOWN:
				 * always ready, same as select(). The right
				 * answer for these shapes, not a fallback --
				 * see __fd_probe()'s default case. */
				p->revents |= (short)(p->events & (POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM));
			}
			if (p->revents) total++;
		}

		/* Zero-timeout peek at every still-pending console read. */
		{
			int k;
			for (k = 0; k < ncons; k++) {
				if (__plat_wait_ready(console_h[k])) {
					struct pollfd *p = &pfds[console_idx[k]];
					if (!p->revents) total++;
					p->revents |= (short)(p->events & (POLLIN | POLLRDNORM));
				}
			}
		}

		if (total > 0) break;
		if (!infinite && remaining == 0) break;  /* timeout == 0: poll, nothing ready */

		{
			long long wait_ticks;
			int wait_infinite;

			if (infinite) {
				wait_infinite = !have_poll;
				wait_ticks = have_poll ? POLL_INTERVAL_TICKS : 0;
			} else {
				wait_infinite = 0;
				wait_ticks = have_poll && remaining > POLL_INTERVAL_TICKS ? POLL_INTERVAL_TICKS : remaining;
			}
			__fd_wait_or_delay(console_h, ncons, wait_ticks, wait_infinite);
			if (!infinite) remaining -= wait_ticks;
		}
	}

	return total;
}
