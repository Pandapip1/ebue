/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "stubs.h"
#include "bad-contract.h"

void local_leak(void)
{
	(void)malloc(8);
} /* allocation-lifetime-expect: local */

void lost_reallocation(void)
{
	void *p = malloc(8);
	p = realloc(p, 16);
	if (p)
		free(p);
} /* allocation-lifetime-expect: realloc failure loses old allocation */

void conditional_return_leak(void)
{
	(void)conditional_buffer(0);
} /* allocation-lifetime-expect: null destination owns return */

void *uncontracted_return(void)
{
	return malloc(8); /* allocation-lifetime-expect: return-contract */
}

[[clang::ownership_takes(broken, 1)]]
void broken_destroy(void *);
[[clang::ownership_returns(broken)]]
void *make_broken(void);

[[clang::ownership_returns(broken)]]
void *make_broken(void)
{
	return malloc(8);
}

[[clang::ownership_takes(broken, 1)]]
void broken_destroy(void *object)
{
	(void)object;
} /* allocation-lifetime-expect: broken-freer */

void wrong_freer(void)
{
	void *object = make_broken();
	free(object);
} /* allocation-lifetime-expect: wrong-family */

void *make_inherited(void)
{
	return malloc(8);
} /* allocation-contract-expect: inherited producer attribute is an error */

/* The header-style declaration above is not enough for an in-tree body:
 * this definition must repeat ownership_takes explicitly. */
void inherited_destroy(void *object)
{
	free(object);
} /* allocation-contract-expect: inherited attribute is an error */
