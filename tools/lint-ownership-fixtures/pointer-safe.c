/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void free(void *);

int local_object(void)
{
	int value = 7;
	int *pointer = &value;
	return *pointer;
}

int allocated_object(void)
{
	int result;
	int *pointer = malloc(sizeof *pointer);
	if (!pointer)
		return 0;
	*pointer = 9;
	result = *pointer;
	free(pointer);
	return result;
}

int bounded_array(void)
{
	int values[3] = {1, 2, 3};
	return values[2];
}

int static_string(void)
{
	return "valid"[1];
}

/* A null-checked pointer *parameter* -- ntlibc's single most common
 * pointer shape, used pervasively for borrowed buffers, structs, and
 * caller-owned objects the callee never allocated and never frees. Once
 * nonnull is proven (the check above), this checker has no further
 * *provable* liveness fact to demand: it never observed this symbol pass
 * through its own allocator/deallocator tracking (OwnershipChecker) at
 * all, so there is no positive evidence to weigh either way, only the
 * ordinary shape of a trusted borrow from the caller. Requiring proof
 * beyond nonnull-ness here is not requiring something merely unproven,
 * it is requiring something structurally unprovable by any per-function
 * analysis: no code on the callee side can ever establish that a value
 * whose provenance crosses a call boundary was not freed by code this
 * analysis never sees. Before the checker stopped treating "not seen by
 * my own allocator tracking" as "known freed", this one shape alone
 * accounted for the majority of ntlibc.ValidPointer findings tree-wide. */
int opaque_borrow(int *pointer)
{
	if (!pointer)
		return 0;
	return *pointer;
}

/* __errno_location() is declared (include/errno.h) to always return a
 * valid pointer to the calling thread's own storage and is never
 * permitted to return NULL -- errno itself is `#define errno
 * (*__errno_location())`, so this exact shape is behind essentially
 * every `errno = ...` and `if (errno)` in the tree. Pinned here so a
 * regression in ValidPointerChecker::isAlwaysNonNull is caught locally
 * instead of silently reappearing as ~440 findings tree-wide. */
extern int *__errno_location(void);

int errno_is_always_valid(void)
{
	return *__errno_location();
}
