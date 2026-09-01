/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

int checked_division(int value, int divisor)
{
	if (divisor == 0 || value == (-2147483647 - 1))
		return 0;
	return value / divisor;
}

unsigned checked_remainder(unsigned value, unsigned divisor)
{
	if (!divisor)
		return 0;
	value %= divisor;
	return value;
}

unsigned constant_division(unsigned value)
{
	return value / 10;
}

unsigned checked_unsigned_shift(unsigned value, unsigned count)
{
	if (count >= 32)
		return 0;
	return value << count;
}

unsigned checked_signed_count(unsigned value, int count)
{
	if (count < 0 || count >= 32)
		return 0;
	return value >> count;
}

unsigned constant_shift(unsigned value)
{
	value <<= 7;
	return value;
}

int checked_addition(int value)
{
	if (value > 2147483647 - 7)
		return 0;
	return value + 7;
}

int checked_two_operand_addition(int left, int right)
{
	if (right > 0 && left > 2147483647 - right)
		return 0;
	if (right < 0 && left < (-2147483647 - 1) - right)
		return 0;
	return left + right;
}

int checked_positive_multiplication(int left, int right)
{
	if (left < 0 || right <= 0 || left > 2147483647 / right)
		return 0;
	return left * right;
}

int checked_positive_negative_multiplication(int left, int right)
{
	if (left <= 0 || right >= 0)
		return 0;
	if (right == -1)
		return -left;
	if (left > (-2147483647 - 1) / right)
		return 0;
	return left * right;
}

int checked_negative_positive_multiplication(int left, int right)
{
	if (left >= 0 || right <= 0 || left < (-2147483647 - 1) / right)
		return 0;
	return left * right;
}

int checked_negative_multiplication(int left, int right)
{
	if (left >= 0 || right >= 0 || left < 2147483647 / right)
		return 0;
	return left * right;
}

int checked_subtraction(int value)
{
	if (value < (-2147483647 - 1) + 7)
		return 0;
	return value - 7;
}

int checked_negation(int value)
{
	if (value == (-2147483647 - 1))
		return 0;
	return -value;
}

int checked_loop_increment(void)
{
	int i;
	for (i = 0; i < 3; i++)
		;
	return i;
}

/* Pins symbolInterval()'s BO_Rem decomposition of a value materialized
 * into a local and reread past the point where a source-level walk can
 * see the '%' that narrowed it -- src/stdlib/strtod.c's bn_shl() shape
 * (`int b = k % 32; ...; carry = v >> (32 - b);`), reread three lines
 * later rather than used inline. */
unsigned rem_materialized_then_shift(unsigned v, int k)
{
	int b;
	if (k <= 0)
		return v;
	b = k % 32;
	if (!b)
		return v;
	return v >> (32 - b);
}

/* Pins symbolInterval()'s BO_Add/BO_Sub/BO_Mul decomposition of an affine
 * chain materialized into a local -- src/stdio/printf.c's fmt_a() shape
 * (`int shift = (13 - prec) * 4;`, prec bounded [0,12] by two literal
 * guards immediately above, `shift` reread four times after). 64 bits
 * wide because (13 - prec) * 4 reaches 52, which only a >=53-bit shifted
 * value keeps in range -- the same reason the real code shifts a
 * uint64_t mantissa, not a 32-bit one. */
unsigned long long affine_materialized_then_shift(unsigned long long man,
						   int prec)
{
	int shift;
	if (prec < 0 || prec >= 13)
		return man;
	shift = (13 - prec) * 4;
	return man >> shift;
}

/* Pins the same symbolInterval() BO_Rem decomposition reaching
 * DivisorChecker (not just SignedArithmeticChecker/ShiftCountChecker):
 * `d` is materialized from `k % 100` and reread as a divisor two lines
 * later, plus a `+ 1` (BO_Add) the same decomposition must see through
 * to prove `d` is never zero. */
unsigned rem_materialized_then_divide(unsigned value, unsigned k)
{
	unsigned d;
	d = k % 100 + 1;
	return value % d;
}

/* Pointer subtraction belongs to provenance/object-bound analysis, not to
 * generic signed integer arithmetic. */
long pointer_difference_is_not_integer_arithmetic(int *left, int *right)
{
	return left - right;
}
