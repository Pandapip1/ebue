/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */


#include "../../include/ownership.h"

tokdef heap_free;
tokdef shared_access l_unlimited implicit_drop;
tokdef permissive_once l_permissive implicit_drop;
tokdef permissive_view l_unlimited implicit_drop;

struct owner_box {
	void *value withhandle(heap) withtok(heap_free);
};

withhandle(heap) withtok(heap_free)
void *make_owner(void);
void inspect_owner(void *value withhandle(heap) withtok(heap_free));

withhandle(shared) withtok(shared_access)
void *make_shared(void);
void inspect_shared(void *value
    withhandle(shared) withtok(shared_access));

withhandle(permissive) withtok(permissive_once) withtok(permissive_view)
void *make_permissive(void);
void inspect_permissive(void *value
    withhandle(permissive) withtok(permissive_view));
void consume_permissive(void *value
    withhandle(permissive) consume(permissive_once));

withhandle(heap) withtok(heap_free)
void *forward_owner(void)
{
	void *owner withhandle(heap) withtok(heap_free) = make_owner();
	return owner;
}

void store_and_pass_owner(void)
{
	void *owner withhandle(heap) withtok(heap_free) = make_owner();
	struct owner_box box;
	box.value = owner;
	inspect_owner(box.value);
}

void move_linear_token(void)
{
	void *first withhandle(heap) withtok(heap_free) = make_owner();
	void *second withhandle(heap) withtok(heap_free) = first;
	inspect_owner(second);
}

void move_into_known_empty_destination(void)
{
	void *source withhandle(heap) withtok(heap_free) = make_owner();
	void *destination withhandle(heap) withtok(heap_free) = 0;
	destination = source;
	inspect_owner(destination);
}

void self_assignment_preserves_token(void)
{
	void *owner withhandle(heap) withtok(heap_free) = make_owner();
	owner = owner;
	inspect_owner(owner);
}

void borrow_without_moving_linear_token(void)
{
	void *owner withhandle(heap) withtok(heap_free) = make_owner();
	void *borrowed = owner;
	(void)borrowed;
	inspect_owner(owner);
}

void copy_duplicable_token(void)
{
	void *first withhandle(shared) withtok(shared_access) = make_shared();
	void *second withhandle(shared) withtok(shared_access) = first;
	inspect_shared(first);
	inspect_shared(second);
}

void replace_implicitly_droppable_token(void)
{
	void *source withhandle(shared) withtok(shared_access) = make_shared();
	void *destination withhandle(shared) withtok(shared_access) = make_shared();
	destination = source;
	inspect_shared(source);
	inspect_shared(destination);
}

void copy_around_permissive_linear_token(void)
{
	void *owner withhandle(permissive) withtok(permissive_once)
	    withtok(permissive_view) = make_permissive();
	void *copy withhandle(permissive) withtok(permissive_view) = owner;
	inspect_permissive(owner);
	inspect_permissive(copy);
	consume_permissive(owner);
}

withhandle(strict) withtok(heap_free)
void *make_strict_borrowed_owner(void);
void consume_strict_borrowed_owner(void *value
    withhandle(strict) withtok(heap_free));

void bounded_strict_loan(void)
{
	void *owner withhandle(strict) withtok(heap_free) =
	    make_strict_borrowed_owner();
	void *borrowed = owner;
	(void)borrowed;
	consume_strict_borrowed_owner(owner);
}
