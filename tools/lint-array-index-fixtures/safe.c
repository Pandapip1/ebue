/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

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

/* A plain incoming pointer parameter has no DynamicExtent this checker can
 * ever derive on its own -- the allocation backing it happened somewhere
 * this translation unit cannot see.  elements_withtok(token, extent_param)
 * is the same declared element-count contract include/ownership.h already
 * attaches to every argc/argv-shaped utility entry point; contractElementCount()
 * reads it as a second, independent bound.  This proves what a same-statement
 * "i < argc"-style guard already established in the source, the way
 * src/internal/util.h's real __util_*_main declarations do. */
int argv_style_loop(
	int argc,
	char **argv __attribute__((annotate("elements_withtok:null_terminated:argc"))))
{
	int i;
	int total = 0;
	for (i = 0; i < argc; i++)
		total += (int)(argv[i] != 0);
	return total;
}
