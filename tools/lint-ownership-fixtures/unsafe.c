/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t) __attribute__((ownership_returns(malloc)));
void *realloc(void *, size_t)
	__attribute__((ownership_returns(malloc),
	               annotate("ntlibc.reallocates:1")));
void free(void *) __attribute__((ownership_takes(malloc, 1)));

void release_stack(void)
{
	int local;
	free(&local); /* ownership-expect: unproved-release */
}

void release_literal(void)
{
	free("borrowed"); /* ownership-expect: unproved-release */
}

void consume_twice(void)
{
	void *owner = malloc(8);
	free(owner);
	free(owner); /* ownership-expect: consumed */
}

int use_borrow_after_consume(void)
{
	int *owner = malloc(sizeof *owner);
	if (!owner)
		return 0;
	int *borrow = owner;
	free(owner);
	return *borrow; /* ownership-expect: consumed-borrow */
}
