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

/* A fixed-size static array with an explicit bounds guard immediately
 * before the write. The guard proves the index in bounds directly; the
 * generic byte-extent machinery below (getDynamicExtentWithOffset)
 * turns that same fact into a compound "extent_of_slots_in_bytes minus
 * index*sizeof(*slots)" expression the constraint solver generally
 * cannot simplify back down to the plain "index < CAP" comparison it
 * started as, so this pattern -- among the most common in systems code:
 * a static table plus a bounds-checked counter -- was reported as
 * unproven even though the bound was checked one line above.
 * arrayIndexProvenInBounds() asks the solver the exact question the
 * guard itself answered instead. Deliberately `unsigned`: an
 * *unconstrained signed* counter needs a separate, real proof that it
 * cannot be negative too (a whole-file invariant -- "only ever
 * incremented from a zero-initialized static, never decremented past
 * it" -- that no single function can see, and getting it wrong would
 * hide a genuine buffer-underflow shape), so arrayIndexProvenInBounds()
 * only fires unconditionally for `unsigned`, where "not negative" is
 * true by type and only the upper bound needs checking; a signed
 * counter still requires provable non-negativity, exactly like
 * src/exit/exit.c's own `static int nhandlers` remains unresolved by
 * this fix (see the commit message for the fuller accounting). */
static void (*slots[8])(void);
static unsigned nslots;

int bounded_table_push(void (*f)(void))
{
	if (nslots >= 8)
		return -1;
	slots[nslots++] = f;
	return 0;
}

/* __teb() and the global __peb it bootstraps (src/internal/libc.h) are
 * NT's own OS-guaranteed-present per-thread/per-process control blocks:
 * every live thread has a TEB (read via a two-instruction segment-
 * register access, not something application code can ever observe as
 * absent), and __peb is set from it, unconditionally, before anything
 * else in the program runs (crt/crt1.c's __libc_start_main), never
 * reassigned or cleared afterward. Pinned here so a regression in
 * ValidPointerChecker::isAlwaysNonNull/isAlwaysNonNullGlobal is caught
 * locally instead of silently reappearing as ~14 findings tree-wide
 * (dlfcn's __peb->ImageBaseAddress, every NT malloc/free/realloc's
 * __peb->ProcessHeap, ...). */
typedef struct { void *ImageBaseAddress; } *PPEB_FIXTURE;
extern PPEB_FIXTURE __teb(void);
PPEB_FIXTURE __peb;

/* Each tests its own mechanism in isolation: teb_is_always_valid never
 * touches __peb, so it cannot pass merely because assigning __peb from
 * __teb()'s already-proven-nonnull return would locally taint __peb too
 * -- and peb_is_always_valid dereferences __peb with no preceding
 * assignment or check anywhere in the function, so it can only pass via
 * isAlwaysNonNullGlobal recognising the global's own identity. */
void *teb_is_always_valid(void)
{
	return __teb()->ImageBaseAddress;
}

void *peb_is_always_valid(void)
{
	return __peb->ImageBaseAddress;
}

/* GCC/Clang's `nonnull` attribute is the C ecosystem's own standard way
 * to say a pointer parameter is required, not optional -- real compilers
 * already diagnose a provably-NULL argument at the call site under
 * -Wnonnull. Trusting it here (ValidPointerChecker::checkBeginFunction)
 * means an ordinary parameter dereferenced with no in-function guard is
 * no longer unconditionally flagged once its own header truthfully
 * states the function's real contract -- unlike a blanket relaxation of
 * every unchecked parameter (which would also silence pointer-unsafe.c's
 * nullable_pointer, a genuine unguarded-dereference shape this checker
 * must keep catching), this only trusts parameters this project has
 * itself explicitly annotated. */
int nonnull_attribute_is_trusted(int *pointer) __attribute__((nonnull(1)));
int nonnull_attribute_is_trusted(int *pointer)
{
	return *pointer;
}
