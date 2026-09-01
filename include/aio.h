/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _AIO_H
#define _AIO_H

#include <features.h>
#include <signal.h>
#include <bits/sigevent.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_off_t
#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_time_t
#define __NEED_struct_timespec
#include <bits/alltypes.h>

struct aiocb {
	int aio_fildes;
	volatile void *aio_buf;
	size_t aio_nbytes;
	int aio_reqprio;
	struct sigevent aio_sigevent;
	int aio_lio_opcode;
	off_t aio_offset;
	/* Private request identity.  Applications initialize an aiocb to zero
	 * and must not alter it while an operation is outstanding, so keeping
	 * the identity here makes aio_error()/aio_return() an exact lookup
	 * without imposing a second, pointer-keyed allocation. */
	void *__nt_request;
};

#define AIO_CANCELED    0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE     2

#define LIO_READ   0
#define LIO_WRITE  1
#define LIO_NOP    2

#define LIO_WAIT   0
#define LIO_NOWAIT 1

int aio_cancel(int, struct aiocb *);
int aio_error(const struct aiocb *);
int aio_fsync(int, struct aiocb *);
int aio_read(struct aiocb *);
ssize_t aio_return(struct aiocb *);
int aio_suspend(const struct aiocb *const [], int, const struct timespec *);
int aio_write(struct aiocb *);
/* list is required: src/thread/aio.c's own lio_listio() subscripts it
 * directly (`list[i]`) whenever count >= 1, with no NULL check of its
 * own. event is left unmarked -- it is genuinely optional per
 * lio_listio.html ("If sig is NULL, then no signal is queued"), and
 * `(event && !valid_event(event))` there is a real, load-bearing check
 * of exactly that. */
int lio_listio(int, struct aiocb *const [], int, struct sigevent *)
    __attribute__((nonnull(2)));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
