/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

tokdef heap_allocated;
tokdef mutex_unlock
	l_unlimited;
tokdef nonnull
	l_unlimited
	implicit_drop
	sentinel_exclude(NULL);
tokdef checked_fd
	sentinel_exclude(-1);

void *allocate(void) withtok(heap_allocated);
void release(void *value consume(heap_allocated));
void *locked_mutex(void) withtok(mutex_unlock);
void inspect_unlock_authority(void *value withtok(mutex_unlock));
void discard_unlock_authority(void *value consume(mutex_unlock));
void *nonnull_value(void) withtok(nonnull);
void *dialect_forward_safe(void *value withtok(heap_allocated))
	withtok(heap_allocated);
void *dialect_forward_unsafe(void *value withtok(heap_allocated))
	withtok(heap_allocated);
