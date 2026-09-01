/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
#include "../../include/ownership.h"

tokdef fixture_readable_span l_unlimited implicit_drop extent_at_least zero_vacuous;
tokdef fixture_writable_span l_unlimited implicit_drop extent_at_least zero_vacuous;
tokdef fixture_disjoint_span l_unlimited implicit_drop disjoint_extent zero_vacuous;

void *memcpy(void *destination withtok(fixture_writable_span(length))
	withtok(fixture_disjoint_span(source, length)),
	const void *source withtok(fixture_readable_span(length)), size_t length);
void *memset(void *destination withtok(fixture_writable_span(length)), int,
	size_t length);
withtok(fixture_writable_span(length))
void *__malloc(size_t length);
void *opaque_allocator(size_t length);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
void consume_bytes(const void *source withtok(fixture_readable_span(length)),
	size_t length);
void establish_writable(
	void *buffer grant(fixture_writable_span(length)), size_t length);
void __ownership_writable_span(
	void *buffer grant(fixture_writable_span(length)), size_t length);
void __ownership_readable_span(
	const void *buffer grant(fixture_readable_span(length)), size_t length);
void __ownership_disjoint_span(
	void *first grant(fixture_disjoint_span(second, length)),
	const void *second, size_t length);
int maybe_establish_writable(
	void *buffer grant(fixture_writable_span(length)), size_t length);

void unproved_writable_grant(
	void *buffer grant(fixture_writable_span(length)), size_t length)
{
	(void)buffer;
	(void)length;
} /* memory-contract-expect */

void proof_invalidated_by_reassignment(
	void *buffer grant(fixture_writable_span(length)), size_t length)
{
	establish_writable(buffer, length);
	buffer = 0;
} /* memory-contract-expect */

void failed_grant_is_not_available(char *buffer, size_t length)
{
	if (maybe_establish_writable(buffer, length) == 0)
		return;
	memset(buffer, 0, length); /* memory-contract-expect */
}

void unproved_disjoint_grant(
	void *first grant(fixture_disjoint_span(second, length)),
	const void *second, size_t length)
{
	(void)first;
	(void)second;
	(void)length;
} /* memory-contract-expect */

void contracted_copy(char *out withtok(fixture_writable_span(length)),
	const char *in withtok(fixture_readable_span(length)), size_t length);

static void contracted_fill(
	char *out withtok(fixture_writable_span(length)), size_t length)
{
	memset(out, 0, length);
}

void insufficient_contracted_suffix(
	char *out withtok(fixture_writable_span(capacity)), size_t capacity,
	size_t offset, size_t length)
{
	if (offset <= capacity && length > capacity - offset)
		memset(out + offset, 0, length); /* memory-contract-expect */
}

struct fixture_record {
	int first;
	int second;
};

void overfill_typed_object(struct fixture_record *record)
{
	memset(record, 0, sizeof *record + 1); /* memory-contract-expect */
}

void movable_path_axiom(char *buffer, size_t length, int use_local)
{
	char local[8];
	if (use_local) {
		if (length > sizeof local)
			return;
		buffer = local;
	}
	__ownership_writable_span(buffer, length); /* memory-contract-expect */
}

void violate_contracts(char *text)
{
	char source[4], destination[4];
	(void)text;
	contracted_copy(destination, source, 8); /* memory-contract-expect */
}

/* Only this caller violates the contract.  Once diagnosed, the assumed
 * exact-region span must prevent a duplicate report in contracted_fill's
 * inlined body, even though destination + 2 is an interior region. */
void violate_inline_contract(void)
{
	char destination[4];
	contracted_fill(destination + 2, 3); /* memory-contract-expect */
}

void oversized(void)
{
	char source[4], destination[4];
	memcpy(destination, source, 8); /* memory-contract-expect */
}

void opaque(void *buffer, size_t length)
{
	memset(buffer, 0, length); /* memory-contract-expect */
}

/* Manual proof calls are migration scaffolding.  Once the allocation's
 * dynamic extent already proves the same span, retaining the axiom must be
 * diagnosed so implementation bodies converge on inferred contracts. */
void redundant_heap_axiom(size_t length)
{
	char *buffer = __malloc(length);
	if (!buffer) return;
	__ownership_writable_span(buffer, length); /* memory-contract-expect */
	memset(buffer, 0, length);
}

void redundant_static_axioms(void)
{
	char source[4], destination[4];
	__ownership_readable_span(source, sizeof source); /* memory-contract-expect */
	__ownership_disjoint_span(destination, source, sizeof source); /* memory-contract-expect */
}

void overlapping(void)
{
	char buffer[16];
	memcpy(buffer + 1, buffer, 8); /* memory-contract-expect */
}

/* A genuinely too-small __malloc'd allocation must still be caught once
 * this tree's own allocator family gets a real (as opposed to placeholder)
 * dynamic extent -- the same regression guard 8a56a66 pinned for the
 * sibling ValidPointerChecker's own analogous fix. */
void too_small_heap_allocation(const char *s)
{
	char *d = __malloc(4);
	memcpy(d, s, 8); /* memory-contract-expect */
}

/* A suggestive allocator name is not a contract. */
void undeclared_allocator_extent(size_t length)
{
	char *buffer = opaque_allocator(length);
	if (!buffer) return;
	memset(buffer, 0, length); /* memory-contract-expect */
}

/* Sharing an affine root does not prove the larger expression is larger:
 * unsigned addition wraps, so n == SIZE_MAX allocates zero bytes here. */
void wrapped_allocator_extent(size_t n)
{
	if (n != (size_t)-1) return;
	char *d = __malloc(n + 1);
	if (!d) return;
	memset(d, 0, n); /* memory-contract-expect */
}

/* strnlen(s, n)'s contract is looser than strlen(s)'s: if it walked all
 * n bytes without finding a terminator, only those n bytes (not n + 1)
 * are known-safe to read back from s -- reading one byte past that is
 * NOT proven, unlike the strlen()-derived case in safe.c's dup_all(). */
void too_much_from_strnlen(const char *s, size_t n)
{
	size_t l = strnlen(s, n);
	char *d = __malloc(l + 1);
	if (!d) return;
	memcpy(d, s, l + 1); /* memory-contract-expect */
	d[0] = 0;
}

/* Advancing the source without shortening the requested span still reaches
 * one byte beyond what strnlen established. */
void too_much_from_strnlen_suffix(const char *s, size_t n)
{
	size_t l = strnlen(s, n);
	if (l == 0) return;
	consume_bytes(s + 1, l); /* memory-contract-expect */
}

void unchecked_strlen_difference(const char *s, const char *tail)
{
	size_t whole = strlen(s);
	size_t removed = strlen(tail);
	consume_bytes(s, whole - removed); /* memory-contract-expect */
}
