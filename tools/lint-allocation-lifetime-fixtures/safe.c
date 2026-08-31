/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "stubs.h"

void local_release(void)
{
	void *p = malloc(16);
	free(p);
}

void conditional_reallocation(void)
{
	void *old = malloc(8);
	void *replacement = realloc(old, 16);
	if (replacement)
		free(replacement);
	else
		free(old);
}

void destroy_widget(void *)
	__attribute__((ownership_takes(widget, 1)));
void *make_widget(void)
	__attribute__((ownership_returns(widget)));

void *make_widget(void)
{
	return malloc(16);
}

void destroy_widget(void *widget)
	__attribute__((ownership_takes(widget, 1)))
{
	free(widget);
}

void use_contract(void)
{
	void *widget = make_widget();
	destroy_widget(widget);
}
