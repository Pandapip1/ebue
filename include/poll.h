/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * poll(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * poll.html.  Implemented in src/select/poll.c, sharing its
 * per-descriptor readiness probe and wait-or-sleep primitive with
 * select()/pselect() (src/select/select.c has the full design
 * writeup: the wait-vs-poll split across this library's descriptor
 * shapes, the 20ms pipe-poll interval, and exact timeout semantics --
 * poll()'s millisecond timeout follows the same rules, just converted
 * to 100ns ticks instead of going through a struct timeval/timespec).
 */
#ifndef _POLL_H
#define _POLL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

typedef unsigned long nfds_t;

struct pollfd {
	int fd;
	short events;
	short revents;
};

/* poll.html DESCRIPTION event/revent bits. Numeric values match musl's
 * (and thus glibc's) so a raw int mask written by one is meaningful to
 * the other -- nothing in POSIX assigns these bit positions, but there
 * is no reason to pick different ones. */
#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010
#define POLLNVAL   0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#ifndef POLLWRNORM
#define POLLWRNORM 0x100
#endif
#ifndef POLLWRBAND
#define POLLWRBAND 0x200
#endif

int poll (struct pollfd *, nfds_t, int);

#ifdef __cplusplus
}
#endif
#endif
