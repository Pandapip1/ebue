/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct { void *opaque[8]; } guarded_t;

struct guarded_slot {
	guarded_t *value [[ownership_holds_handle(guarded),
	                  ownership_holds_token(guarded_unlocked)]];
};

int guarded_init(guarded_t *object
    [[ownership_constructs(guarded),
      ownership_adds_token(guarded_unlocked)]]);
int guarded_lock(guarded_t *object
    [[ownership_requires_handle(guarded),
      ownership_drops_token(guarded_unlocked),
      ownership_adds_duplicable_token(guarded_access)]]);
int guarded_unlock(guarded_t *object
    [[ownership_requires_handle(guarded),
      ownership_drops_token(guarded_access),
      ownership_adds_token(guarded_unlocked)]]);

void inspect_protected_value(guarded_t *object
    [[ownership_requires_handle(guarded),
      ownership_requires_token(guarded_access)]]);
void reset_while_unlocked(guarded_t *object
    [[ownership_requires_handle(guarded),
      ownership_requires_absent_token(guarded_access)]]);

void access_without_lock_authority(void)
{
	guarded_t object;
	if (guarded_init(&object) != 0)
		return;
	inspect_protected_value(&object); /* ownership-expect: controlled-unlocked */
}

void unlocked_operation_while_locked(void)
{
	guarded_t object;
	if (guarded_init(&object) != 0)
		return;
	if (guarded_lock(&object) == 0)
		reset_while_unlocked(&object); /* ownership-expect: controlled-locked */
}

void lock_twice_without_unlocking(void)
{
	guarded_t object;
	if (guarded_init(&object) != 0)
		return;
	if (guarded_lock(&object) == 0)
		guarded_lock(&object); /* ownership-expect: controlled-lock-twice */
}

void unlock_without_lock_authority(void)
{
	guarded_t object;
	if (guarded_init(&object) == 0)
		guarded_unlock(&object); /* ownership-expect: controlled-unlock */
}

void destination_cannot_manufacture_dynamic_token(void)
{
	guarded_t object;
	struct guarded_slot slot;
	slot.value = &object; /* ownership-expect: dynamic-manufacture */
}
