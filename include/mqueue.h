/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _MQUEUE_H
#define _MQUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_mode_t
#include <bits/alltypes.h>

struct sigevent;
struct timespec;

typedef int mqd_t;

struct mq_attr {
	long mq_flags;
	long mq_maxmsg;
	long mq_msgsize;
	long mq_curmsgs;
	long __reserved[4];
};

mqd_t mq_open(const char *, int, ...);
int mq_close(mqd_t);
int mq_unlink(const char *);
int mq_getattr(mqd_t, struct mq_attr *);
int mq_setattr(mqd_t, const struct mq_attr *__restrict,
	struct mq_attr *__restrict);
int mq_notify(mqd_t, const struct sigevent *);
int mq_send(mqd_t, const char *, size_t, unsigned);
int mq_timedsend(mqd_t, const char *, size_t, unsigned,
	const struct timespec *);
ssize_t mq_receive(mqd_t, char *, size_t, unsigned *);
ssize_t mq_timedreceive(mqd_t, char *__restrict, size_t,
	unsigned *__restrict, const struct timespec *__restrict);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
