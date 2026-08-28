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
