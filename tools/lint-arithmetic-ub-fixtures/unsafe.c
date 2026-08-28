int unchecked_division(int value, int divisor)
{
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
