/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _BITS_SIGEVENT_H
#define _BITS_SIGEVENT_H

/* Shared by <signal.h> and <aio.h>.  AIO embeds struct sigevent rather
 * than merely pointing at it, so the type must remain available even when
 * a strict language mode suppresses the POSIX additions to <signal.h>. */
union sigval {
	int sival_int;
	void *sival_ptr;
};

struct sigevent {
	union sigval sigev_value;
	int sigev_signo;
	int sigev_notify;
	void (*sigev_notify_function)(union sigval);
	void *sigev_notify_attributes;
};

#define SIGEV_SIGNAL 0
#define SIGEV_NONE 1
#define SIGEV_THREAD 2

#endif
