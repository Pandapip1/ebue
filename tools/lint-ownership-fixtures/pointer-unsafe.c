/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void free(void *);

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
