/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#define __NEED_mode_t
#include <bits/alltypes.h>

struct timespec;

typedef struct {
	void *__handle;
	unsigned int __magic;
	unsigned int __named;
} sem_t;

#define SEM_FAILED ((sem_t *)-1)
#define SEM_VALUE_MAX 2147483647

int sem_init(sem_t *, int, unsigned int);
int sem_destroy(sem_t *);
sem_t *sem_open(const char *, int, ...);
int sem_close(sem_t *);
int sem_unlink(const char *);
int sem_wait(sem_t *);
int sem_trywait(sem_t *);
int sem_timedwait(sem_t *, const struct timespec *);
int sem_post(sem_t *);
int sem_getvalue(sem_t *, int *);

#ifdef __cplusplus
}
#endif
#endif
