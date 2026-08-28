int checked_division(int value, int divisor)
{
	if (divisor == 0)
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
