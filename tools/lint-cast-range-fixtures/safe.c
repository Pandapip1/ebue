/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef unsigned short fixture_word;
typedef unsigned int fixture_uint;

fixture_word guarded_unsigned(unsigned long long value)
{
	if (value > 65535)
		return 0;
	return (fixture_word)value;
}

int guarded_signed(long long value)
{
	if (value < -2147483647LL - 1 || value > 2147483647LL)
		return 0;
	return (int)value;
}

fixture_uint guarded_sign_change(int value)
{
	if (value < 0)
		return 0;
	return (fixture_uint)value;
}

fixture_word guarded_expression(unsigned int value)
{
	if (value > 32767)
		return 0;
	return (fixture_word)(value * 2);
}

fixture_word constant_in_range(void)
{
	return (fixture_word)65535;
}

long widening_is_intrinsically_safe(int value)
{
	return (long)value;
}

fixture_word modulo_bounds_hash(unsigned long long value)
{
	value ^= value >> 33;
	value *= 0xff51afd7ed558ccdULL;
	value ^= value >> 33;
	return (fixture_word)(value % 65521);
}

fixture_word mask_bounds_hash(unsigned long long value)
{
	value ^= value >> 29;
	value *= 0x165667b19e3779f9ULL;
	return (fixture_word)(value & 0xffff);
}

_Bool guarded_boolean(unsigned int value)
{
	if (value > 1)
		return 0;
	return (_Bool)value;
}
