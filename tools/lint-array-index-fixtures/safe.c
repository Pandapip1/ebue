int fixed_constant(void)
{
	int values[4] = { 1, 2, 3, 4 };
	return values[3];
}

int guarded_unsigned(unsigned int index)
{
	int values[4] = { 1, 2, 3, 4 };
	if (index >= 4)
		return 0;
	return values[index];
}

int guarded_signed(int index)
{
	int values[4] = { 1, 2, 3, 4 };
	if (index < 0 || index >= 4)
		return 0;
	return values[index];
}

int bounded_loop(void)
{
	int values[4] = { 1, 2, 3, 4 };
	int total = 0;
	unsigned int index;
	for (index = 0; index < 4; index++)
		total += values[index];
	return total;
}
