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
