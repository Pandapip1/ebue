/* C library internals intentionally use the implementation-reserved namespace. */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Allocation-lifetime contracts for tools/lint.sh's `ownership` stage
 * (`alloclife` remains a compatibility alias).
 * A producer and its unique freer share a family identifier:
 *
 *   object *make_object(void) NTLIBC_RETURNS_OWNERSHIP(object_family);
 *   void destroy_object(object *)
 *       NTLIBC_TAKES_OWNERSHIP(object_family, 1);
 *
 * The checker does not trust NTLIBC_TAKES_OWNERSHIP by itself.  It seeds
 * the designated parameter as a live allocation on entry to the freer and
 * path-sensitively requires the body to consume it on every return path.
 * Likewise, NTLIBC_RETURNS_OWNERSHIP only permits the allocation actually
 * returned by that function to cross its boundary; unrelated live
 * allocations remain leaks.  The report postprocessor also requires every
 * returned family to have exactly one taking function, which turns the
 * shared family identifier into an unambiguous producer-to-freer link.
 * A takes declaration with no definition in the scanned source set is an
 * explicit external-stub assumption.  If a definition does exist, that
 * definition must repeat NTLIBC_TAKES_OWNERSHIP itself (merely inheriting the
 * header attribute is a contract error), after which its body is proved.
 *
 * These attributes exist only in this one Clang analysis.  They disappear
 * for tcc, gcc, normal Clang builds, and every other lint stage, so they
 * change neither the ABI nor the compiler surface of the shipped library.
 */
#ifndef _NTLIBC_ALLOCATION_ANNOTATIONS_H
#define _NTLIBC_ALLOCATION_ANNOTATIONS_H

#include <features.h>

#define NTLIBC_RETURNS_OWNERSHIP(family) \
	__NTLIBC_RETURNS_OWNERSHIP(family)
#define NTLIBC_TAKES_OWNERSHIP(family, ...) \
	__NTLIBC_TAKES_OWNERSHIP(family, __VA_ARGS__)
#define NTLIBC_REALLOCATES(family, argument) \
	__NTLIBC_REALLOCATES(family, argument)
#define NTLIBC_RETURNS_OWNERSHIP_IF_NULL(family, argument) \
	__NTLIBC_RETURNS_OWNERSHIP_IF_NULL(family, argument)

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
