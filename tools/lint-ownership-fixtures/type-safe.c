/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

struct owner_box {
	void *value [[ownership_holds_handle(heap), ownership_holds_token(heap_free)]];
};

[[ownership_holds_handle(heap), ownership_holds_token(heap_free)]]
void *make_owner(void);
void inspect_owner(void *value [[ownership_holds_handle(heap),
                                ownership_holds_token(heap_free)]]);

[[ownership_holds_handle(heap), ownership_holds_token(heap_free)]]
void *forward_owner(void)
{
	void *owner [[ownership_holds_handle(heap),
	             ownership_holds_token(heap_free)]] = make_owner();
	return owner;
}

void store_and_pass_owner(void)
{
	void *owner [[ownership_holds_handle(heap),
	             ownership_holds_token(heap_free)]] = make_owner();
	struct owner_box box;
	box.value = owner;
	inspect_owner(box.value);
}
