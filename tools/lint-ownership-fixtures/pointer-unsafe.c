/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "allocator-fixture.h"

long getline(char **, size_t *, void *);

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

/* The getline summary is conditional on a nonnegative return.  Its failure
 * branch must retain the original, possibly-null output pointer rather than
 * leaking the success branch's nonnull fact across both outcomes. */
int failed_line_input_does_not_validate_the_buffer(void *stream)
    __attribute__((nonnull(1)));
int failed_line_input_does_not_validate_the_buffer(void *stream)
{
	char *line = 0;
	size_t capacity = 0;
	long length = getline(&line, &capacity, stream);
	if (length >= 0)
		return 0;
	return *line; /* ownership-expect: pointer-null */
}

/* The adversarial twin of pointer-safe.c's doubled_extent_via_
 * multiplication_index: the terminator is written one byte PAST the
 * doubled extent (`2 * n + 1` against an allocation of only `n + n + 1`
 * bytes, i.e. valid indices 0..2n), a genuinely out-of-bounds access.
 * Both the ad hoc prover and the new z3ExtentProvenInBounds fallback
 * must still report this -- the fallback proving a real, different
 * shape (the sibling fixture) must never loosen this one: Z3 correctly
 * finds a counterexample (any n) rather than proving sufficiency. */
char *doubled_extent_off_by_one_via_multiplication_index(size_t n)
{
	char *d = __malloc(n + n + 1);
	if (!d) return 0;
	d[2 * n + 1] = 0; /* ownership-expect: pointer-extent-z3 */
	return d;
}
