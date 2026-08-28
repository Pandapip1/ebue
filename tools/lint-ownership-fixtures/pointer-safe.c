/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void free(void *);

int local_object(void)
{
	int value = 7;
	int *pointer = &value;
	return *pointer;
}

int allocated_object(void)
{
	int result;
	int *pointer = malloc(sizeof *pointer);
	if (!pointer)
		return 0;
	*pointer = 9;
	result = *pointer;
	free(pointer);
	return result;
}

int bounded_array(void)
{
	int values[3] = {1, 2, 3};
	return values[2];
}

int static_string(void)
{
	return "valid"[1];
}
