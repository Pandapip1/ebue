/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void free(void *);
void *__malloc(size_t);

int nullable_pointer(int *pointer)
{
	return *pointer; /* ownership-expect: pointer-null */
}

int out_of_bounds(void)
{
	int values[2] = {1, 2};
	return values[2]; /* ownership-expect: pointer-extent */
}

int misaligned(void)
{
	char storage[sizeof(int) + 1];
	int *pointer = (int *)(void *)(storage + 1);
	return *pointer; /* ownership-expect: pointer-alignment */
}

int consumed_storage(void)
{
	int *pointer = malloc(sizeof *pointer);
	if (!pointer)
		return 0;
	free(pointer);
	return *pointer; /* ownership-expect: pointer-consumed */
}

/* `nonnull(1)` covers only the first parameter; the second is exactly as
 * unguarded as nullable_pointer above and must still be flagged --
 * checkBeginFunction (OwnershipChecker.cpp) must not over-generalize a
 * single-argument nonnull attribute into trusting every pointer
 * parameter of the function. */
int nonnull_attribute_does_not_cover_every_param(int *checked, int *unchecked)
    __attribute__((nonnull(1)));
int nonnull_attribute_does_not_cover_every_param(int *checked, int *unchecked)
{
	(void)*checked;
	return *unchecked; /* ownership-expect: pointer-null */
}

/* OwnershipChecker::allocationSizeInBytes gives __malloc's own result a
 * real tracked extent (see pointer-safe.c's heap_allocation_extent_is_
 * trusted), and ValidPointerChecker's fixed-offset leniency in the real-
 * extent branch is deliberately asymmetric: it only trusts a fixed
 * offset when the real extent leaves sufficiency merely UNPROVEN, never
 * when the real extent makes sufficiency PROVABLY IMPOSSIBLE. Here the
 * allocation is 4 bytes (sizeof(int)) and `b` sits at a fixed offset of
 * 4 bytes into an 8-byte struct -- a genuinely too-small allocation,
 * concretely resolvable, that must still be caught through the exact
 * same fixed-offset path the leniency above exists for. */
struct pair { int a; int b; };

int too_small_heap_allocation_via_fixed_offset(void)
{
	struct pair *p = __malloc(sizeof(int));
	if (!p) return 0;
	return p->b; /* ownership-expect: pointer-extent */
}
