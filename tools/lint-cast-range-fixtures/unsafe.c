typedef unsigned short fixture_word;
typedef unsigned int fixture_uint;

fixture_word unguarded_unsigned(unsigned long long value)
{
	return (fixture_word)value; /* cast-range-expect */
}

int unguarded_signed(long long value)
{
	return (int)value; /* cast-range-expect */
}

fixture_uint unguarded_sign_change(int value)
{
	return (fixture_uint)value; /* cast-range-expect */
}

fixture_word late_guard(unsigned int value)
{
	fixture_word result = (fixture_word)value; /* cast-range-expect */
	if (value > 65535)
		return 0;
	return result;
}

fixture_word insufficient_guard(unsigned int value)
{
	if (value > 65536)
		return 0;
	return (fixture_word)value; /* cast-range-expect */
}

fixture_word constant_out_of_range(void)
{
	return (fixture_word)65536; /* cast-range-expect */
}

_Bool unguarded_boolean(unsigned int value)
{
	return (_Bool)value; /* cast-range-expect */
}
