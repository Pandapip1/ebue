unsigned strict_bound(unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i++) {
	}
	return i;
}

unsigned conjunctive_bound(unsigned n, int stop)
{
	unsigned i;
	for (i = 0; i < n && !stop; ++i) {
	}
	return i;
}

unsigned inclusive_constant_bound(void)
{
	unsigned i;
	for (i = 0; i <= 60; i++) {
	}
	return i;
}

int inclusive_countdown(void)
{
	int i;
	for (i = 8; i >= 0; i--) {
	}
	return i;
}

unsigned sentinel_pointer(const unsigned char *p)
{
	const unsigned char *start = p;
	while (*p != 0)
		p++;
	return (unsigned)(p - start);
}

unsigned sentinel_index(const unsigned char *p)
{
	unsigned i = 0;
	while (p[i])
		i = i + 1;
	return i;
}

unsigned countdown(unsigned n)
{
	while (n)
		--n;
	return n;
}

unsigned progress_on_every_backedge(unsigned n, int choose)
{
	unsigned i = 0;
	while (i < n) {
		if (choose) {
			i++;
			continue;
		}
		i++;
	}
	return i;
}

unsigned progress_or_exit(unsigned n, int stop)
{
	unsigned i = 0;
	while (i < n) {
		if (stop)
			break;
		i++;
	}
	return i;
}

unsigned progress_in_both_expression_arms(unsigned n, int choose)
{
	unsigned i = 0;
	while (i < n)
		choose ? i++ : ++i;
	return i;
}

unsigned guarded_recursion(unsigned n)
{
	if (n)
		return guarded_recursion(n - 1);
	return 0;
}

unsigned guarded_else_recursion(unsigned n)
{
	if (!n)
		return 0;
	else
		return guarded_else_recursion(n - 1);
}
