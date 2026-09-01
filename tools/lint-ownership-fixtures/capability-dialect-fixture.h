/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

tokdef dialect_mutex_unlocked;
tokdef dialect_mutex_locked l_unlimited;

typedef struct { void *opaque[8]; } dialect_mutex_t;

void dialect_mutex_init(dialect_mutex_t *mutex
	construct(dialect_mutex) grant(dialect_mutex_unlocked));
void dialect_mutex_lock(dialect_mutex_t *mutex
	handle(dialect_mutex) consume(dialect_mutex_unlocked)
	grant(dialect_mutex_locked));
void dialect_mutex_share_unlock(dialect_mutex_t *mutex
	handle(dialect_mutex) withtok(dialect_mutex_locked)
	grant(dialect_mutex_locked));
void dialect_mutex_unlock(dialect_mutex_t *mutex
	handle(dialect_mutex) consume(dialect_mutex_locked)
	grant(dialect_mutex_unlocked));
void dialect_mutex_destroy(dialect_mutex_t *mutex
	destroy(dialect_mutex) consume(dialect_mutex_unlocked));
