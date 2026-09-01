/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "dialect-fixture.h"

/* ownership-contract-expect: dialect return attribute must be repeated. */
/* ownership-contract-expect: dialect parameter attribute must be repeated. */
void *dialect_forward_unsafe(void *value)
{
	return value;
}

void consume_missing_dialect_token(void)
{
	void *plain = 0;
	release(plain); /* ownership-expect: dialect-consume */
}

void move_dialect_token_twice(void)
{
	void *first withtok(heap_allocated) = allocate();
	void *second withtok(heap_allocated) = first;
	void *third withtok(heap_allocated) = first; /* ownership-expect: dialect-move */
	(void)second;
	(void)third;
}
