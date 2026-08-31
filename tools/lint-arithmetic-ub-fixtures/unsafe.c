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
