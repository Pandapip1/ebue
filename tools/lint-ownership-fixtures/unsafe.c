/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "allocator-fixture.h"

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
