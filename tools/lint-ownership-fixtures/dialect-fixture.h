/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

token heap_allocated;
token mutex_unlock
	l_unlimited;
token nonnull
	l_unlimited
	implicit_drop
	sentinel_exclude(NULL);

void *allocate(void) withtok(heap_allocated);
void release(void *value consume(heap_allocated));
void *locked_mutex(void) withtok(mutex_unlock);
void inspect_unlock_authority(void *value withtok(mutex_unlock));
void *dialect_forward_safe(void *value withtok(heap_allocated))
	withtok(heap_allocated);
void *dialect_forward_unsafe(void *value withtok(heap_allocated))
	withtok(heap_allocated);
