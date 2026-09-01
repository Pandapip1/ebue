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

unsigned division_guard_admits_zero(unsigned n)
{
	while (n < 2) { /* totality-expect */
		n /= 2;
	}
	return n;
}

unsigned division_disequality_admits_zero(unsigned n)
{
	while (n != 1) { /* totality-expect */
		n /= 2;
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
void free(void *);
void *realloc(void *, __SIZE_TYPE__);

unsigned member_bound_mutated_in_body(struct vec *p)
{
	/* The bound may move on a continuing path: strict same-domain `<`
	 * still makes a TYPE_MAX backedge impossible for local unit rank i. */
	unsigned i;
	for (i = 0; i < p->n; i++) {
		p->n++;
	}
	return i;
}

unsigned member_bound_escapes_to_call(struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) {
		mutate_vec(p);
	}
	return i;
}

unsigned member_bound_across_realloc(struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) {
		(void)realloc(p->v, 16);
	}
	return i;
}

unsigned member_bound_address_taken(struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) {
		(void)&p->n;
	}
	return i;
}

struct ascent_alias_vec {
	unsigned bound;
	unsigned *alias;
};

static unsigned pointer_member_alias_step(struct ascent_alias_vec *p,
	unsigned remaining)
{
	*p->alias = p->bound + 1;
	return remaining ? 1 : 0;
}

unsigned ascent_with_pointer_member_alias(struct ascent_alias_vec *p)
{
	unsigned i = 0;
	while (i < p->bound) { /* totality-expect */
		unsigned step = pointer_member_alias_step(p, p->bound - i);
		if (!step || step > p->bound - i) return i;
		i += step;
	}
	return i;
}

unsigned ascent_allows_zero(unsigned end, unsigned step)
{
	unsigned i = 0;
	while (i < end) { /* totality-expect */
		if (step > end - i) return i;
		i += step;
	}
	return i;
}

unsigned ascent_allows_oversize(unsigned end, unsigned step)
{
	unsigned i = 0;
	while (i < end) { /* totality-expect */
		if (!step) return i;
		i += step;
	}
	return i;
}

unsigned member_bound_base_reseated(struct vec *p, struct vec *q)
{
	unsigned i;
	for (i = 0; i < p->n; i++) {
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
	for (i = 0; i < p->n; i++) {
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
	for (i = 0; i < p->n; i++) {
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

unsigned member_decrement_before_guard(struct vec *p)
{
	for (;;) { /* totality-expect */
		p->n--;
		if (p->n == 0) return p->n;
	}
}

unsigned member_guard_bypassed(struct vec *p, int bypass)
{
	for (;;) { /* totality-expect */
		if (bypass) continue;
		if (p->n == 0) return p->n;
		p->n--;
	}
}

unsigned member_guard_without_progress(struct vec *p, int skip)
{
	for (;;) { /* totality-expect */
		if (p->n == 0) return p->n;
		if (skip) continue;
		p->n--;
	}
}

int signed_nonunit_after_zero_guard(int n)
{
	for (;;) { /* totality-expect */
		if (n == 0) return n;
		n -= 2;
	}
}

int dynamic_countdown_allows_zero(int n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step > n) return n;
		n -= step;
	}
	return n;
}

unsigned paired_rank_can_stall(unsigned a, unsigned a_end, unsigned b,
	unsigned b_end, int choose, int stuck)
{
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose)
			a++;
		else if (stuck)
			continue;
		else
			b++;
	}
	return a + b;
}

unsigned paired_rank_can_retreat(unsigned a, unsigned a_end, unsigned b,
	unsigned b_end, int choose)
{
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose)
			a++;
		else {
			b++;
			a--;
		}
	}
	return a + b;
}

unsigned paired_rank_can_wrap_one_component(unsigned a, unsigned a_end,
	unsigned b, unsigned b_end, int choose)
{
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose) {
			a++;
			a++;
		} else {
			b++;
		}
	}
	return a + b;
}

static void paired_rank_sink(unsigned value)
{
	(void)value;
}

unsigned paired_rank_nested_repeated_progress(unsigned a, unsigned a_end,
	unsigned b, unsigned b_end, int choose)
{
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose)
			paired_rank_sink((a++, a++));
		else
			b++;
	}
	return a + b;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunevaluated-expression"
unsigned paired_rank_unevaluated_progress(unsigned a, unsigned a_end,
	unsigned b, unsigned b_end, int choose)
{
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose)
			(void)sizeof(a++);
		else
			b++;
	}
	return a + b;
}
#pragma clang diagnostic pop

unsigned paired_rank_builtin_unevaluated_progress(unsigned a,
	unsigned a_end, unsigned b, unsigned b_end, int choose)
{
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose)
			(void)__builtin_constant_p(a++);
		else
			b++;
	}
	return a + b;
}

unsigned paired_rank_condition_resets_component(unsigned a, unsigned a_end,
	unsigned b, unsigned b_end, int choose)
{
	unsigned *alias = &a;
	while (a < a_end && b < b_end && ((*alias = 0), 1)) { /* totality-expect */
		if (choose)
			a++;
		else
			b++;
	}
	return a + b;
}

unsigned paired_rank_transitive_alias_reset(unsigned a, unsigned a_end,
	unsigned b, unsigned b_end, int choose)
{
	unsigned *alias = &a;
	unsigned **indirect = &alias;
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose)
			a++;
		else
			b++;
		**indirect = 0;
	}
	return a + b;
}

unsigned paired_rank_disjunctive_bound(unsigned a, unsigned a_end,
	unsigned b, unsigned b_end, int choose)
{
	while (a < a_end || b < b_end) { /* totality-expect */
		if (choose)
			a++;
		else
			b++;
	}
	return a + b;
}

unsigned paired_rank_affine_wrap(unsigned char a, unsigned char a_end,
	unsigned char b, unsigned char b_end, int choose)
{
	int offset = -1;
	while (a + offset < a_end && b < b_end) { /* totality-expect */
		if (choose)
			a++;
		else
			b++;
	}
	return (unsigned)a + b;
}

unsigned paired_rank_mutable_bound(unsigned a, unsigned a_end, unsigned b,
	unsigned b_end, int choose)
{
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose) {
			a++;
			a_end++;
		} else {
			b++;
		}
	}
	return a + b;
}

static unsigned paired_global_end;
static unsigned *paired_global_end_alias = &paired_global_end;

unsigned paired_rank_global_bound_alias(unsigned a, unsigned b,
	unsigned b_end, int choose)
{
	while (a < paired_global_end && b < b_end) { /* totality-expect */
		if (choose)
			a++;
		else
			b++;
		*paired_global_end_alias = a + 1;
	}
	return a + b;
}

unsigned paired_rank_local_bound_transitive_alias(unsigned a,
	unsigned a_end, unsigned b, unsigned b_end, int choose)
{
	unsigned *alias = &a_end;
	unsigned **indirect = &alias;
	while (a < a_end && b < b_end) { /* totality-expect */
		if (choose)
			a++;
		else
			b++;
		**indirect = a + 1;
	}
	return a + b;
}

int dynamic_countdown_allows_negative(int n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step == 0 || step > n) return n;
		n -= step;
	}
	return n;
}

int dynamic_countdown_allows_oversize(int n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step <= 0) return n;
		n -= step;
	}
	return n;
}

int dynamic_countdown_missing_branch(int n, int skip)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step <= 0 || step > n) return n;
		if (skip) continue;
		n -= step;
	}
	return n;
}

int dynamic_countdown_conditional_update(int n, int update)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step <= 0 || step > n) return n;
		if (update) n -= step;
	}
	return n;
}

void opaque_rank_mutation(int *n);

int dynamic_countdown_escaped_rank(int n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step <= 0 || step > n) return n;
		opaque_rank_mutation(&n);
		n -= step;
	}
	return n;
}

unsigned dynamic_cast_without_positive_guard(unsigned n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if ((unsigned)step > n) return n;
		n -= (unsigned)step;
	}
	return n;
}

unsigned char dynamic_narrowing_cast(unsigned char n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step <= 0 || (unsigned char)step > n) return n;
		n -= (unsigned char)step;
	}
	return n;
}

int dynamic_step_changed_after_guard(int n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step <= 0 || step > n) return n;
		step = opaque_predicate();
		n -= step;
	}
	return n;
}

int dynamic_rank_changed_after_guard(int n)
{
	while (n > 0) { /* totality-expect */
		int step = opaque_predicate();
		if (step <= 0 || step > n) return n;
		n += opaque_predicate();
		n -= step;
	}
	return n;
}

unsigned member_zero_branch_can_fall_through(struct vec *p, int stop)
{
	for (;;) { /* totality-expect */
		if (p->n == 0 && stop) return p->n;
		p->n--;
	}
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

struct restricted_byte_cursor {
	unsigned char next;
};

extern void opaque_cursor_call(struct restricted_byte_cursor *);

unsigned char nonrestricted_member_call_not_receiving_base(
	struct restricted_byte_cursor *cursor, unsigned char total)
{
	while (cursor->next < total) { /* totality-expect */
		cursor->next++;
		opaque_mutation();
	}
	return cursor->next;
}

unsigned char restricted_member_direct_alias_reset(
	struct restricted_byte_cursor *restrict cursor, unsigned char total)
{
	struct restricted_byte_cursor *alias = cursor;
	while (cursor->next < total) { /* totality-expect */
		cursor->next++;
		alias->next = 0;
	}
	return cursor->next;
}

unsigned char restricted_member_indirect_alias_reset(
	struct restricted_byte_cursor *restrict cursor, unsigned char total)
{
	struct restricted_byte_cursor *alias = cursor;
	struct restricted_byte_cursor **indirect = &alias;
	while (cursor->next < total) { /* totality-expect */
		cursor->next++;
		(*indirect)->next = 0;
	}
	return cursor->next;
}

unsigned char restricted_member_call_receives_base(
	struct restricted_byte_cursor *restrict cursor, unsigned char total)
{
	while (cursor->next < total) { /* totality-expect */
		cursor->next++;
		opaque_cursor_call(cursor);
	}
	return cursor->next;
}

unsigned char restricted_member_call_receives_alias(
	struct restricted_byte_cursor *restrict cursor, unsigned char total)
{
	struct restricted_byte_cursor *alias = cursor;
	while (cursor->next < total) { /* totality-expect */
		cursor->next++;
		opaque_cursor_call(alias);
	}
	return cursor->next;
}

unsigned char restricted_member_bound_changes(
	struct restricted_byte_cursor *restrict cursor, unsigned char total)
{
	while (cursor->next < total) { /* totality-expect */
		cursor->next++;
		total++;
	}
	return cursor->next;
}

unsigned char restricted_member_can_wrap_at_maximum(
	struct restricted_byte_cursor *restrict cursor, unsigned char total)
{
	while (cursor->next <= total) { /* totality-expect */
		cursor->next++;
		opaque_mutation();
	}
	return cursor->next;
}

unsigned char restricted_member_early_continue(
	struct restricted_byte_cursor *restrict cursor, unsigned char total,
	int skip)
{
	while (cursor->next < total) { /* totality-expect */
		if (skip) continue;
		cursor->next++;
		opaque_mutation();
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
	for (i = 0; i < file_bound; i++) {
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

unsigned char byte_rank_wider_bound(__SIZE_TYPE__ bound)
{
	unsigned char i;
	for (i = 0; i < bound; i++) { /* totality-expect */
	}
	return i;
}

unsigned inclusive_unit_rank_can_wrap(unsigned bound)
{
	unsigned i;
	for (i = 0; i <= bound; i++) { /* totality-expect */
	}
	return i;
}

unsigned inclusive_negative_constant_wraps(void)
{
	unsigned i;
	for (i = 0; i <= -1; i++) { /* totality-expect */
	}
	return i;
}

unsigned inclusive_runtime_signed_bound(int bound)
{
	unsigned i;
	for (i = 0; i <= bound; i++) { /* totality-expect */
	}
	return i;
}

unsigned reversed_inclusive_runtime_signed_bound(int bound)
{
	unsigned i;
	for (i = 0; bound >= i; i++) { /* totality-expect */
	}
	return i;
}

unsigned inclusive_narrow_signed_bound(signed char bound)
{
	unsigned i;
	for (i = 0; i <= bound; i++) { /* totality-expect */
	}
	return i;
}

unsigned repeated_unit_increment(unsigned bound)
{
	unsigned i;
	for (i = 0; i < bound; i++, i++) { /* totality-expect */
	}
	return i;
}

unsigned continuing_rank_reset(unsigned bound, int reset)
{
	unsigned i;
	for (i = 0; i < bound; i++) { /* totality-expect */
		if (reset)
			i = 0;
	}
	return i;
}

void callback_changes_rank(unsigned *rank);

unsigned callback_rank_mutation(unsigned bound)
{
	unsigned i;
	for (i = 0; i < bound; i++) { /* totality-expect */
		callback_changes_rank(&i);
	}
	return i;
}

unsigned reused_rank_in_continuing_inner_loop(unsigned bound, unsigned inner,
	int reuse)
{
	unsigned i;
	for (i = 0; i < bound; i++) { /* totality-expect */
		if (reuse)
			for (i = 0; i < inner; i++) {
			}
	}
	return i;
}

unsigned char byte_rank_mixed_signed_bound(int bound)
{
	unsigned char i;
	for (i = 0; i < bound; i++) { /* totality-expect */
	}
	return i;
}

unsigned short narrow_rank_runtime_wide_bound(unsigned bound)
{
	unsigned short i;
	for (i = 0; i < bound; i++) { /* totality-expect */
	}
	return i;
}

int signed_rank_unsigned_domain(unsigned bound)
{
	int i;
	/* A signed overflow leaves defined C execution, so this pre-existing
	 * signed-rank proof does not need the unsigned same-domain lemma. */
	for (i = 0; i < bound; i++) {
	}
	return i;
}

unsigned nonunit_upper_bound(unsigned bound)
{
	unsigned i;
	for (i = 0; i < bound; i += 2) { /* totality-expect */
	}
	return i;
}

static unsigned global_unit_rank;

unsigned nonlocal_unit_rank(unsigned bound)
{
	for (global_unit_rank = 0; global_unit_rank < bound; /* totality-expect */
	     global_unit_rank++) {
		opaque_mutation();
	}
	return global_unit_rank;
}

unsigned nested_goto_skips_exit(unsigned bound, unsigned inner, int reuse)
{
	unsigned i;
	for (i = 0; i < bound; i++) { /* totality-expect */
		if (reuse) {
			for (i = 0; i < inner; i++)
				goto keep_going;
			return i;
		}
keep_going:
		;
	}
	return i;
}

unsigned statement_expression_skips_progress(unsigned bound, int reset)
{
	unsigned i;
	for (i = 0; i < bound; i++) { /* totality-expect */
		(void)({
			if (reset) {
				i = 0;
				continue;
			}
			0;
		});
	}
	return i;
}

unsigned interval_one_branch_unchanged(unsigned lo, unsigned hi, int left)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (left)
			hi = mid;
	}
	return lo;
}

unsigned interval_one_branch_grows(unsigned lo, unsigned hi, int left)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (left)
			hi = mid;
		else
			lo = mid + 2;
	}
	return lo;
}

unsigned interval_overflowing_midpoint(unsigned lo, unsigned hi, int left)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = (lo + hi) / 2;
		if (left)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

void callback_changes_bounds(unsigned *lo, unsigned *hi);

unsigned interval_callback_mutation(unsigned lo, unsigned hi, int left)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		callback_changes_bounds(&lo, &hi);
		if (left)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

unsigned interval_alias_reset(unsigned lo, unsigned hi, int left)
{
	unsigned *alias = &lo;
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (left)
			hi = mid;
		else
			lo = mid + 1;
		*alias = 0;
	}
	return lo;
}

unsigned interval_continue_before_update(unsigned lo, unsigned hi, int skip)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (skip)
			continue;
		lo = mid + 1;
	}
	return lo;
}

unsigned interval_goto_skips_update(unsigned lo, unsigned hi, int skip)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (skip)
			goto next;
		lo = mid + 1;
next:
		;
	}
	return lo;
}

unsigned interval_low_can_stall(unsigned lo, unsigned hi, int left)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (left)
			hi = mid;
		else
			lo = mid;
	}
	return lo;
}

unsigned interval_high_can_stall(unsigned lo, unsigned hi, int left)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (left)
			hi = mid + 1;
		else
			lo = mid + 1;
	}
	return lo;
}

unsigned interval_midpoint_mutated(unsigned lo, unsigned hi, int left)
{
	while (lo < hi) { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		mid++;
		if (left)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

unsigned halving_wrong_divisor(unsigned n, int left)
{
	while (n) { /* totality-expect */
		unsigned half = n / 3;
		if (left)
			n = half;
		else
			n -= half + 1;
	}
	return n;
}

unsigned halving_repeated_update(unsigned n, int left)
{
	while (n) { /* totality-expect */
		unsigned half = n / 2;
		if (left)
			n = half;
		else
			n -= half + 1;
		n = half;
	}
	return n;
}

unsigned interval_do_while_first_iteration(unsigned lo, unsigned hi, int left)
{
	do { /* totality-expect */
		unsigned mid = lo + (hi - lo) / 2;
		if (left)
			hi = mid;
		else
			lo = mid + 1;
	} while (lo < hi);
	return lo;
}

void mutate_affine_value(__SIZE_TYPE__ *);

__SIZE_TYPE__ affine_overflow_fixed_point(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i <= n) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
	}
	return i;
}

__SIZE_TYPE__ affine_without_half_guard(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
	}
	return i;
}

__SIZE_TYPE__ affine_inclusive_half_guard(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i <= n / 2) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
	}
	return i;
}

__SIZE_TYPE__ affine_do_while_not_pretested(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	do { /* totality-expect */
		child = 2 * i + 1;
		i = child;
	} while (i < n / 2);
	return i;
}

__SIZE_TYPE__ affine_rank_reset(__SIZE_TYPE__ i, __SIZE_TYPE__ n, int reset)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
		if (reset)
			i = 0;
	}
	return i;
}

__SIZE_TYPE__ escaped_affine_rank(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
		mutate_affine_value(&i);
	}
	return i;
}

__SIZE_TYPE__ affine_child_reset(__SIZE_TYPE__ i, __SIZE_TYPE__ n,
	int reset)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		if (reset)
			child = 0;
		i = child;
	}
	return i;
}

__SIZE_TYPE__ escaped_affine_child(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		mutate_affine_value(&child);
		i = child;
	}
	return i;
}

__SIZE_TYPE__ mutable_affine_bound(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
		n++;
	}
	return i;
}

__SIZE_TYPE__ escaped_affine_bound(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
		mutate_affine_value(&n);
	}
	return i;
}

__SIZE_TYPE__ callback_mutates_affine_rank(__SIZE_TYPE__ i,
	__SIZE_TYPE__ n, void (*callback)(__SIZE_TYPE__ *))
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
		callback(&i);
	}
	return i;
}

__SIZE_TYPE__ alternate_affine_multiplier(__SIZE_TYPE__ i,
	__SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 3 * i + 1;
		i = child;
	}
	return i;
}

__SIZE_TYPE__ alternate_affine_addend(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 2;
		i = child;
	}
	return i;
}

unsigned short promoted_affine_type(unsigned short i, unsigned short n)
{
	unsigned short child;
	while (i < n / 2) { /* totality-expect */
		child = (unsigned short)(2 * i + 1);
		i = child;
	}
	return i;
}

__SIZE_TYPE__ repeated_affine_update(__SIZE_TYPE__ i, __SIZE_TYPE__ n)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		child = 2 * i + 1;
		i = child;
	}
	return i;
}

long signed_affine_negative_domain(long i, long n)
{
	long child;
	while (i < n / 2) { /* totality-expect */
		child = 2 * i + 1;
		i = child;
	}
	return i;
}

__SIZE_TYPE__ affine_early_continue(__SIZE_TYPE__ i, __SIZE_TYPE__ n,
	int skip)
{
	__SIZE_TYPE__ child;
	while (i < n / 2) { /* totality-expect */
		if (skip)
			continue;
		child = 2 * i + 1;
		i = child;
	}
	return i;
}

unsigned geometric_missing_overflow_guard(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		cap *= 2;
	}
	return cap;
}

unsigned geometric_guard_too_high(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2 + 1)
			return cap;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_reversed_guard(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap <= (unsigned)-1 / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_zero_rank(unsigned need)
{
	unsigned cap = 0;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}

int geometric_negative_signed_rank(int need)
{
	int cap = -1;
	while (cap < need) { /* totality-expect */
		if (cap > __INT_MAX__ / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}

signed char geometric_narrow_signed_rank(signed char initial,
	signed char need)
{
	signed char cap = initial;
	if (cap < 8)
		cap = 8;
	while (cap < need) { /* totality-expect */
		if (cap > __SCHAR_MAX__ / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}

void geometric_change(unsigned *p);

unsigned geometric_bound_callback(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		geometric_change(&need);
		cap *= 2;
	}
	return cap;
}

unsigned geometric_rank_callback(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		geometric_change(&cap);
		cap *= 2;
	}
	return cap;
}

unsigned geometric_bound_reset(unsigned initial, unsigned need, int reset)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		if (reset)
			need = (unsigned)-1;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_rank_reset(unsigned initial, unsigned need, int reset)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		if (reset)
			cap = 1;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_continue_bypass(unsigned initial, unsigned need, int skip)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (skip)
			continue;
		if (cap > (unsigned)-1 / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_repeated_doubling(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		cap *= 2;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_saturating_formula(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		cap = cap > (unsigned)-1 / 2 ? (unsigned)-1 : cap * 2;
	}
	return cap;
}

unsigned geometric_stalling_formula(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		cap *= 1;
	}
	return cap;
}

unsigned geometric_guard_does_not_exit(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			cap = 1;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_guard_after_doubling(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		cap *= 2;
		if (cap > (unsigned)-1 / 2)
			return cap;
	}
	return cap;
}

unsigned geometric_conditional_doubling(unsigned initial, unsigned need,
	int grow)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		if (grow)
			cap *= 2;
	}
	return cap;
}

unsigned geometric_nested_doubling(unsigned initial, unsigned need, int grow)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		while (grow) /* totality-expect */
			cap *= 2;
	}
	return cap;
}

static unsigned geometric_global_need;

unsigned geometric_global_bound_call(unsigned initial)
{
	unsigned cap = initial ? initial : 8;
	while (cap < geometric_global_need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		opaque_mutation();
		cap *= 2;
	}
	return cap;
}

unsigned geometric_do_while(unsigned initial, unsigned need)
{
	unsigned cap = initial ? initial : 8;
	do { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		cap *= 2;
	} while (cap < need);
	return cap;
}

unsigned geometric_truncated_initializer(unsigned initial, unsigned need,
	unsigned long long wide)
{
	unsigned cap = initial ? initial : wide;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_volatile_initializer(volatile unsigned initial,
	unsigned need)
{
	unsigned cap = initial ? initial : 8;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}

unsigned geometric_transitive_bound_alias(unsigned initial, unsigned need,
	int reset)
{
	unsigned cap = initial ? initial : 8;
	unsigned *first = &need;
	unsigned *second = first;
	while (cap < need) { /* totality-expect */
		if (cap > (unsigned)-1 / 2)
			return cap;
		if (reset)
			*second = (unsigned)-1;
		cap *= 2;
	}
	return cap;
}

int geometric_mixed_signed_initializer(int initial, int need)
{
	int cap = initial;
	if (cap < (unsigned)-1)
		cap = 8;
	while (cap < need) { /* totality-expect */
		if (cap > __INT_MAX__ / 2)
			return cap;
		cap *= 2;
	}
	return cap;
}
