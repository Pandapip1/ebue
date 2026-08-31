/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "stubs.h"

void local_release(void)
{
	void *p = malloc(16);
	free(p);
}

void *nullable_producer(void)
	__attribute__((ownership_returns(malloc)));

void *nullable_producer(void)
{
	void *p = malloc(16);
	if (!p)
		return 0;
	return p;
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

void repeated_reallocation(void)
{
	void *first = malloc(8);
	void *second = realloc(first, 16);
	if (!second) {
		free(first);
		return;
	}
	void *third = realloc(second, 32);
	if (!third) {
		free(second);
		return;
	}
	free(third);
}

void *conditional_buffer(void *buffer)
	__attribute__((ownership_returns(malloc),
	               annotate("ntlibc.returns-if-null:1")))
{
	return buffer ? buffer : malloc(8);
}

void conditional_return(void)
{
	char supplied[8];
	(void)conditional_buffer(supplied);
	free(conditional_buffer(0));
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
