/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

int unchecked_division(int value, int divisor)
{
	return value / divisor; /* arithmetic-ub-expect */
}

int unchecked_division_overflow(int value, int divisor)
{
	if (divisor == 0)
		return 0;
	return value / divisor; /* arithmetic-ub-expect */
}

unsigned unchecked_remainder(unsigned value, unsigned divisor)
{
	value %= divisor; /* arithmetic-ub-expect */
	return value;
}

unsigned unchecked_unsigned_shift(unsigned value, unsigned count)
{
	return value << count; /* arithmetic-ub-expect */
}

unsigned unchecked_signed_shift(unsigned value, int count)
{
	return value >> count; /* arithmetic-ub-expect */
}

unsigned upper_bound_only(unsigned value, int count)
{
	if (count >= 32)
		return 0;
	return value << count; /* arithmetic-ub-expect */
}

unsigned lower_bound_only(unsigned value, int count)
{
	if (count < 0)
		return 0;
	return value >> count; /* arithmetic-ub-expect */
}

int unchecked_addition(int left, int right)
{
	return left + right; /* arithmetic-ub-expect */
}

int unchecked_subtraction(int left, int right)
{
	return left - right; /* arithmetic-ub-expect */
}

int unchecked_multiplication(int left, int right)
{
	return left * right; /* arithmetic-ub-expect */
}

int overflowing_positive_multiplication(int left)
{
	if (left <= 1073741823)
		return 0;
	return left * 2; /* arithmetic-ub-expect */
}

int overflowing_positive_negative_multiplication(int left)
{
	if (left <= 1073741824)
		return 0;
	return left * -2; /* arithmetic-ub-expect */
}

int overflowing_negative_positive_multiplication(int left)
{
	if (left >= -1073741824)
		return 0;
	return left * 2; /* arithmetic-ub-expect */
}

int overflowing_negative_multiplication(int left)
{
	if (left >= -1073741823)
		return 0;
	return left * -2; /* arithmetic-ub-expect */
}

int unchecked_negation(int value)
{
	return -value; /* arithmetic-ub-expect */
}

int unchecked_increment(int value)
{
	return ++value; /* arithmetic-ub-expect */
}

int unchecked_signed_left_shift(int value, unsigned count)
{
	if (count >= 32)
		return 0;
	return value << count; /* arithmetic-ub-expect */
}

/* Regression pin for symbolInterval()'s BO_Rem case: same materialized-
 * local shape as safe.c's rem_materialized_then_shift(), but missing
 * that fixture's `if (!b) return v;` guard, so b's provable range is
 * [0, 31] rather than [1, 31] -- b == 0 makes `32 - b` a shift count of
 * 32, out of range for a 32-bit value. symbolInterval() must not
 * over-claim safety just because the shape now looks familiar. */
unsigned rem_materialized_then_shift_unguarded(unsigned v, int k)
{
	int b;
	if (k <= 0)
		return v;
	b = k % 32;
	return v >> (32 - b); /* arithmetic-ub-expect */
}

/* Regression pin for symbolInterval() reaching DivisorChecker: same
 * shape as safe.c's rem_materialized_then_divide(), but missing that
 * fixture's `+ 1`, so `d` is a plain `k % 100` -- provably [0, 99],
 * which includes zero. */
unsigned rem_materialized_then_divide_unguarded(unsigned value, unsigned k)
{
	unsigned d;
	d = k % 100;
	return value % d; /* arithmetic-ub-expect */
}

int positive_subtrahend_without_order(int value, int amount)
{
	if (amount <= 0)
		return 0;
	return value - amount; /* arithmetic-ub-expect */
}

int negative_subtrahend_without_order(int value, int amount)
{
	if (amount >= 0)
		return 0;
	return value - amount; /* arithmetic-ub-expect */
}

static int invalidated_total;
extern void mutate_arithmetic_state(void *);

int ordered_global_subtraction_across_unknown_call(int removed, void *pointer)
{
	if (removed < 0 || removed > invalidated_total)
		return 0;
	mutate_arithmetic_state(pointer);
	return invalidated_total - removed; /* arithmetic-ub-expect */
}

static int malicious_free_total = 1;
__attribute__((annotate("ntlibc_arith_scalar_noop")))
void __free(void *pointer)
{
	(void)pointer; /* arithmetic-ub-expect */
	malicious_free_total = (-2147483647 - 1);
}

int annotated_but_mutating_free_is_not_summarized(int removed, void *pointer)
{
	if (removed < 0 || removed > malicious_free_total)
		return 0;
	__free(pointer);
	return malicious_free_total - removed; /* arithmetic-ub-expect */
}

struct unchecked_member_countdown {
	int count;
};

int unguarded_member_subtraction(struct unchecked_member_countdown *state)
{
	return state->count - 1; /* arithmetic-ub-expect */
}

void unguarded_member_decrement(struct unchecked_member_countdown *state)
{
	state->count--; /* arithmetic-ub-expect */
}

struct truthy_member_countdown {
	int count;
	int values[4];
};

void signed_truthiness_does_not_prove_positive(
	struct truthy_member_countdown *state)
{
	while (state->count && !state->values[state->count - 1]) /* arithmetic-ub-expect */
		state->count--; /* arithmetic-ub-expect */
}

#define fixture_arith_range(minimum, maximum) \
	__attribute__((annotate("ntlibc_arith_range:" #minimum ":" #maximum)))
#define fixture_nonzero_field_on_success(argument, field) \
	__attribute__((annotate("ntlibc_arith_nonzero_field_on_success:" \
		#argument ":" #field)))

static unsigned contracted_divisor(unsigned value,
	int divisor fixture_arith_range(2, 36))
{
	return value % (unsigned)divisor;
}

unsigned violated_range_contract(unsigned value)
{
	return contracted_divisor(value, 0); /* arithmetic-ub-expect */
}

static unsigned escaped_range_contract(unsigned value,
	int divisor fixture_arith_range(2, 36))
{
	return value % (unsigned)divisor; /* arithmetic-ub-expect */
}

static unsigned (*escaped_range_contract_pointer)(unsigned, int) =
	escaped_range_contract;

unsigned violated_escaped_range_contract(unsigned value)
{
	return escaped_range_contract_pointer(value, 0);
}

static int nonpositive_divisor_interval(int value,
	int divisor fixture_arith_range(-1, 36))
{
	int digit = value % divisor; /* arithmetic-ub-expect */
	return digit;
}

static int negative_divisor_interval(int value,
	int divisor fixture_arith_range(-36, -1))
{
	int digit = value % divisor; /* arithmetic-ub-expect */
	return digit + 2147483647; /* arithmetic-ub-expect */
}

int call_negative_divisor_interval(int value, int divisor)
{
	if (divisor < -36 || divisor > -1)
		return 0;
	return negative_divisor_interval(value, divisor);
}

int call_nonpositive_divisor_interval(int value, int divisor)
{
	if (divisor < -1 || divisor > 36)
		return 0;
	return nonpositive_divisor_interval(value, divisor);
}

struct broken_bucket_table { unsigned count; };

static int broken_bucket_count(struct broken_bucket_table *table)
	fixture_nonzero_field_on_success(0, count);

static int broken_bucket_count(struct broken_bucket_table *table)
{
	(void)table;
	return 1; /* arithmetic-ub-expect */
}

static int explicit_zero_bucket_count(struct broken_bucket_table *table)
	fixture_nonzero_field_on_success(0, count);

static int explicit_zero_bucket_count(struct broken_bucket_table *table)
{
	table->count = 0;
	return 1; /* arithmetic-ub-expect */
}

static int initially_good_bucket_count(struct broken_bucket_table *table)
	fixture_nonzero_field_on_success(0, count);

static int initially_good_bucket_count(struct broken_bucket_table *table)
{
	table->count = 16;
	return 1;
}

unsigned mutation_after_successful_summary(unsigned value,
	struct broken_bucket_table *table)
{
	if (!initially_good_bucket_count(table))
		return 0;
	table->count = 0;
	return value % table->count; /* arithmetic-ub-expect */
}

extern void invalidate_bucket_table(struct broken_bucket_table *table);

static int unknown_bucket_count(struct broken_bucket_table *table)
	fixture_nonzero_field_on_success(0, count);

static int unknown_bucket_count(struct broken_bucket_table *table)
{
	invalidate_bucket_table(table);
	return 1; /* arithmetic-ub-expect */
}
