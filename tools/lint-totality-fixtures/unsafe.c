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
