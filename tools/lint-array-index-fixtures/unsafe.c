int unguarded_unsigned(unsigned int index)
{
	int values[4] = { 1, 2, 3, 4 };
	return values[index]; /* array-index-expect */
}

int unguarded_signed(int index)
{
	int values[4] = { 1, 2, 3, 4 };
	return values[index]; /* array-index-expect */
}

int off_by_one(unsigned int index)
{
	int values[4] = { 1, 2, 3, 4 };
	if (index > 4)
		return 0;
	return values[index]; /* array-index-expect */
}

int late_guard(unsigned int index)
{
	int values[4] = { 1, 2, 3, 4 };
	int result = values[index]; /* array-index-expect */
	if (index >= 4)
		return 0;
	return result;
}

int negative_only_guard(int index)
{
	int values[4] = { 1, 2, 3, 4 };
	if (index < 0)
		return 0;
	return values[index]; /* array-index-expect */
}
