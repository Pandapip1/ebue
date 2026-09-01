/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "dialect-fixture.h"

void *dialect_forward_safe(void *value withtok(heap_allocated))
	withtok(heap_allocated)
{
	return value;
}

void move_linear_dialect_token(void)
{
	void *first withtok(heap_allocated) = allocate();
	void *second withtok(heap_allocated) = first;
	release(second);
}

void copy_unlimited_dialect_token(void)
{
	void *first withtok(mutex_unlock) = locked_mutex();
	void *second withtok(mutex_unlock) = first;
	inspect_unlock_authority(first);
	inspect_unlock_authority(second);
	discard_unlock_authority(first);
	discard_unlock_authority(second);
}

void null_carries_no_token(void)
{
	void *value withtok(nonnull) = 0;
	(void)value;
}

void implicit_drop_is_token_policy(void)
{
	void *value withtok(nonnull) = nonnull_value();
	(void)value;
}

void excluded_sentinel_carries_no_token(void)
{
	int value withtok(checked_fd) = -1;
	(void)value;
}
