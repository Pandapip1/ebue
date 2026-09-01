/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

struct node {
	struct node *next;
};

void unconditional_loop(void)
{
	for (;;) { /* totality-expect */
	}
}

unsigned wrapping_step(unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i += 2) { /* totality-expect */
	}
	return i;
}

unsigned inclusive_type_maximum(void)
{
	unsigned i;
	for (i = 0; i <= ~0u; i++) { /* totality-expect */
	}
	return i;
}

unsigned disjunctive_bound(unsigned n, int keep_running)
{
	unsigned i;
	for (i = 0; i < n || keep_running; i++) { /* totality-expect */
	}
	return i;
}

unsigned moving_bound(unsigned n)
{
	unsigned i = 0;
	while (i < n) { /* totality-expect */
		i++;
		n++;
	}
	return i;
}

unsigned escaped_rank(unsigned n)
{
	unsigned i = 0;
	unsigned *alias = &i;
	while (i < n) { /* totality-expect */
		i++;
		(*alias)--;
	}
	return i;
}

unsigned conditional_progress(unsigned n, int choose)
{
	unsigned i = 0;
	while (i < n) { /* totality-expect */
		if (choose)
			i++;
	}
	return i;
}

unsigned short_circuit_progress(unsigned n, int choose)
{
	unsigned i = 0;
	while (i < n) { /* totality-expect */
		choose && i++;
	}
	return i;
}

unsigned conditional_expression_progress(unsigned n, int choose)
{
	unsigned i = 0;
	while (i < n) { /* totality-expect */
		choose ? i++ : 0;
	}
	return i;
}

unsigned cancelled_for_increment(unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i++) { /* totality-expect */
		i--;
	}
	return i;
}

unsigned unsigned_extra_progress_can_wrap(unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i++) { /* totality-expect */
		i++;
	}
	return i;
}

unsigned division_by_one_does_not_progress(unsigned n)
{
	while (n) { /* totality-expect */
		n /= 1;
	}
	return n;
}

unsigned nonunit_countdown_can_wrap(unsigned n)
{
	while (n) { /* totality-expect */
		n -= 2;
	}
	return n;
}

unsigned mismatched_guarded_steps(unsigned n, int choose)
{
	while (n >= 3) { /* totality-expect */
		if (choose)
			n -= 3;
		else
			n -= 5;
	}
	return n;
}

unsigned cancelled_comma_increment(unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i++, i--) { /* totality-expect */
	}
	return i;
}

unsigned cancelled_condition_countdown(unsigned n)
{
	while (n-- > 0) { /* totality-expect */
		n++;
	}
	return n;
}

void floating_condition_countdown(double n)
{
	while (n-- > 0) { /* totality-expect */
	}
}

struct node *possibly_circular(struct node *node)
{
	while (node) { /* totality-expect */
		node = node->next;
	}
	return node;
}

unsigned unguarded_recursion(unsigned n)
{
	return unguarded_recursion(n - 1); /* totality-expect */
}

const unsigned char *unguarded_pointer_recursion(const unsigned char *p)
{
	return unguarded_pointer_recursion(p + 1); /* totality-expect */
}

struct vec {
	unsigned n;
	unsigned *v;
};

void mutate_vec(struct vec *p);

unsigned member_bound_mutated_in_body(struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) { /* totality-expect */
		p->n++;
	}
	return i;
}

unsigned member_bound_escapes_to_call(struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) { /* totality-expect */
		mutate_vec(p);
	}
	return i;
}

unsigned member_bound_base_reseated(struct vec *p, struct vec *q)
{
	unsigned i;
	for (i = 0; i < p->n; i++) { /* totality-expect */
		p = q;
	}
	return i;
}

unsigned mismatched_member_rank(struct vec *tested, struct vec *changed)
{
	while (tested->n) { /* totality-expect */
		changed->n--;
	}
	return tested->n;
}

unsigned wrapping_pointer_step(const unsigned char *p, unsigned step)
{
	while (*p) { /* totality-expect */
		p += step + 1;
	}
	return *p;
}

unsigned narrow_sentinel_index_wrap(const unsigned char *p)
{
	unsigned char i = 0;
	while (p[i]) { /* totality-expect */
		i++;
	}
	return i;
}

unsigned narrow_sentinel_direct_mutation(unsigned char *p)
{
	unsigned char i = 0;
	while (p[i]) { /* totality-expect */
		/* With an initially valid 256-byte string whose last byte is NUL,
		 * this erases the terminator before it is observed. */
		p[255] = 1;
		i++;
	}
	return i;
}

unsigned narrow_sentinel_alias_mutation(unsigned char *p)
{
	unsigned char *alias = p;
	unsigned char i = 0;
	while (p[i]) { /* totality-expect */
		alias[255] = 1;
		i++;
	}
	return i;
}

void mutate_sentinel(unsigned char *p);

unsigned narrow_sentinel_call_mutation(unsigned char *p)
{
	unsigned char i = 0;
	while (p[i]) { /* totality-expect */
		mutate_sentinel(p);
		i++;
	}
	return i;
}

unsigned narrow_unsigned_strict_bound(unsigned long n)
{
	unsigned char i = 0;
	while (i < n) { /* totality-expect */
		i++;
	}
	return i;
}

unsigned aliased_dereferenced_bound(unsigned *bound, unsigned *alias)
{
	unsigned i = 0;
	while (i < *bound) { /* totality-expect */
		i++;
		(*alias)++;
	}
	return i;
}

unsigned volatile_dereferenced_bound(volatile unsigned *bound)
{
	unsigned i = 0;
	while (i < *bound) { /* totality-expect */
		i++;
	}
	return i;
}

unsigned volatile_member_rank(volatile struct vec *p)
{
	while (p->n) { /* totality-expect */
		p->n--;
	}
	return p->n;
}

unsigned volatile_member_base(struct vec * volatile p)
{
	while (p->n) { /* totality-expect */
		p->n--;
	}
	return p->n;
}

unsigned volatile_member_bound(volatile struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) { /* totality-expect */
	}
	return i;
}

void opaque_mutation(void);
int opaque_predicate(void);

int signed_division_fixed_point(int n)
{
	while (n > -100) { /* totality-expect */
		n /= 10;
	}
	return n;
}

unsigned division_zero_fixed_point(unsigned n, const unsigned char *table)
{
	while (table[n]) { /* totality-expect */
		n /= 2;
	}
	return n;
}

unsigned shift_zero_fixed_point(unsigned n, const unsigned char *table)
{
	while (table[n]) { /* totality-expect */
		n >>= 1;
	}
	return n;
}

unsigned member_bound_across_opaque_call(struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) { /* totality-expect */
		opaque_mutation();
	}
	return i;
}

unsigned member_rank_across_opaque_call(struct vec *p)
{
	while (p->n) { /* totality-expect */
		opaque_mutation();
		p->n--;
	}
	return p->n;
}

unsigned member_rank_call_before_continue(struct vec *p, int call)
{
	while (p->n) { /* totality-expect */
		p->n--;
		if (call) {
			opaque_mutation();
			continue;
		}
	}
	return p->n;
}

struct byte_cursor {
	unsigned char next;
};

unsigned char member_byte_rank_call_before_continue(struct byte_cursor *cursor,
	unsigned char total, int call)
{
	while (cursor->next < total) { /* totality-expect */
		cursor->next++;
		if (call) {
			opaque_mutation();
			continue;
		}
	}
	return cursor->next;
}

unsigned member_rank_with_condition_call(struct vec *p)
{
	while (p->n && opaque_predicate()) { /* totality-expect */
		p->n--;
	}
	return p->n;
}

static unsigned file_bound = 8;
static unsigned file_remaining = 8;
static unsigned *escaped_bound;

unsigned file_bound_across_opaque_call(void)
{
	unsigned i;
	for (i = 0; i < file_bound; i++) { /* totality-expect */
		opaque_mutation();
	}
	return i;
}

unsigned file_rank_across_opaque_call(void)
{
	while (file_remaining) { /* totality-expect */
		opaque_mutation();
		file_remaining--;
	}
	return file_remaining;
}

unsigned escaped_scalar_bound_across_opaque_call(unsigned bound)
{
	unsigned i = 0;
	escaped_bound = &bound;
	while (i < bound) { /* totality-expect */
		opaque_mutation();
		i++;
	}
	return i;
}
