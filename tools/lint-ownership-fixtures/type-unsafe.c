/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

struct owner_box {
	void *value [[ownership_holds_handle(heap), ownership_holds_token(heap_free)]];
};

[[ownership_holds_handle(heap), ownership_holds_token(heap_free)]]
void *make_owner(void);
void *make_plain(void);
void inspect_owner(void *value [[ownership_holds_handle(heap),
                                ownership_holds_token(heap_free)]]);

void manufacture_token(void)
{
	void *owner /* ownership-expect: type-manufacture */
	    [[ownership_holds_handle(heap),
	      ownership_holds_token(heap_free)]] = make_plain();
	(void)owner;
}

void borrow_without_transferring_token(void)
{
	void *plain = make_owner();
	(void)plain;
}

void store_wrong_type(void)
{
	struct owner_box box;
	void *plain = make_plain();
	box.value = plain; /* ownership-expect: type-field */
}

void pass_wrong_type(void)
{
	void *plain = make_plain();
	inspect_owner(plain); /* ownership-expect: type-parameter */
}

[[ownership_holds_handle(heap), ownership_holds_token(heap_free)]]
void *return_wrong_type(void)
{
	return make_plain(); /* ownership-expect: type-return */
}

void use_after_linear_move(void)
{
	void *first [[ownership_holds_handle(heap),
	             ownership_holds_token(heap_free)]] = make_owner();
	void *second [[ownership_holds_handle(heap),
	              ownership_holds_token(heap_free)]] = first;
	inspect_owner(first); /* ownership-expect: token-moved */
	(void)second;
}

void move_linear_token_twice(void)
{
	void *first [[ownership_holds_handle(heap),
	             ownership_holds_token(heap_free)]] = make_owner();
	void *second [[ownership_holds_handle(heap),
	              ownership_holds_token(heap_free)]] = first;
	void *third /* ownership-expect: token-moved */
	    [[ownership_holds_handle(heap),
	      ownership_holds_token(heap_free)]] = first;
	(void)second;
	(void)third;
}
