/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

tokdef guarded_unlocked implicit_drop;
tokdef guarded_access l_unlimited implicit_drop;

typedef struct { void *opaque[8]; } guarded_t;

struct guarded_slot {
	guarded_t *value withhandle(guarded) withtok(guarded_unlocked);
};

int guarded_init(guarded_t *object
    construct(guarded) grant(guarded_unlocked));
int guarded_lock(guarded_t *object
    handle(guarded) consume(guarded_unlocked) grant(guarded_access));
int guarded_unlock(guarded_t *object
    handle(guarded) consume(guarded_access) grant(guarded_unlocked));
int guarded_destroy(guarded_t *object
    destroy(guarded) consume(guarded_unlocked));

void inspect_identity(guarded_t *object
    handle(guarded));
void inspect_protected_value(guarded_t *object
    handle(guarded) withtok(guarded_access));
void reset_while_unlocked(guarded_t *object
    handle(guarded) withouttok(guarded_access));

void explicit_handle_and_lock_authority(void)
{
	guarded_t object;
	if (guarded_init(&object) != 0)
		return;
	inspect_identity(&object);
	reset_while_unlocked(&object);
	if (guarded_lock(&object) == 0) {
		inspect_identity(&object);
		inspect_protected_value(&object);
		if (guarded_unlock(&object) != 0)
			return;
	}
	guarded_destroy(&object);
}

void dynamic_token_follows_destination_type(void)
{
	guarded_t object;
	struct guarded_slot slot;
	if (guarded_init(&object) != 0)
		return;
	slot.value = &object;
	if (guarded_lock(slot.value) == 0 && guarded_unlock(slot.value) != 0)
		return;
	guarded_destroy(slot.value);
}
