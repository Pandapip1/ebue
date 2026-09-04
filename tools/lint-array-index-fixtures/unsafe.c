/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

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

/* An elements_withtok(token, argc) contract bounds argv to argc elements,
 * not argc+1 -- the same off-by-one an unannotated array would still be
 * caught for.  contractElementCount()'s second proof route must reject
 * this exactly as the primary DynamicExtent route already would, proving
 * the new contract-reading path does not loosen what still counts as
 * unproven. */
int argv_off_by_one(
	int argc,
	char **argv __attribute__((annotate("elements_withtok:null_terminated:argc"))))
{
	return (int)(argv[argc] != 0); /* array-index-expect */
}

/* The contract names argc as the bound for argv; an index compared only
 * against an unrelated parameter must stay unproven even though argv now
 * carries a declared element count. */
int argv_unrelated_bound(
	int argc, int limit,
	char **argv __attribute__((annotate("elements_withtok:null_terminated:argc"))))
{
	int i;
	int total = 0;
	for (i = 0; i < limit; i++)
		total += (int)(argv[i] != 0); /* array-index-expect */
	return total;
}
