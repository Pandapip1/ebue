/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct { void *opaque[8]; } guarded_t;

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
int guarded_destroy(guarded_t *object
    [[ownership_destroys(guarded),
      ownership_drops_token(guarded_unlocked)]]);

void inspect_identity(guarded_t *object
    [[ownership_requires_handle(guarded)]]);
void inspect_protected_value(guarded_t *object
    [[ownership_requires_handle(guarded),
      ownership_requires_token(guarded_access)]]);
void reset_while_unlocked(guarded_t *object
    [[ownership_requires_handle(guarded),
      ownership_requires_absent_token(guarded_access)]]);

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
