/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

struct owner_box {
	void *value [[ownership_holds_handle(heap), ownership_holds_token(heap_free)]];
};

[[ownership_holds_handle(heap), ownership_holds_token(heap_free)]]
void *make_owner(void);
void inspect_owner(void *value [[ownership_holds_handle(heap),
                                ownership_holds_token(heap_free)]]);

[[ownership_holds_handle(shared),
  ownership_holds_duplicable_token(shared_access)]]
void *make_shared(void);
void inspect_shared(void *value
    [[ownership_holds_handle(shared),
      ownership_holds_duplicable_token(shared_access)]]);

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

void move_linear_token(void)
{
	void *first [[ownership_holds_handle(heap),
	             ownership_holds_token(heap_free)]] = make_owner();
	void *second [[ownership_holds_handle(heap),
	              ownership_holds_token(heap_free)]] = first;
	inspect_owner(second);
}

void borrow_without_moving_linear_token(void)
{
	void *owner [[ownership_holds_handle(heap),
	             ownership_holds_token(heap_free)]] = make_owner();
	void *borrowed = owner;
	(void)borrowed;
	inspect_owner(owner);
}

void copy_duplicable_token(void)
{
	void *first [[ownership_holds_handle(shared),
	             ownership_holds_duplicable_token(shared_access)]] = make_shared();
	void *second [[ownership_holds_handle(shared),
	              ownership_holds_duplicable_token(shared_access)]] = first;
	inspect_shared(first);
	inspect_shared(second);
}
