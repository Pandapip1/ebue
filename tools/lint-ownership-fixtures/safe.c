/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);

void direct_owner(void)
{
	void *p = malloc(8);
	free(p);
}

void alias_borrow(void)
{
	void *owner = calloc(2, 8);
	void *borrow = owner;
	free(borrow);
}

void nullable_owner(int choose)
{
	void *p = choose ? malloc(8) : 0;
	free(p);
}

void null_release(void)
{
	free(0);
}

void conditional_transfer(void)
{
	void *owner = malloc(8);
	void *replacement = realloc(owner, 16);
	if (replacement)
		free(replacement);
	else
		free(owner);
}
