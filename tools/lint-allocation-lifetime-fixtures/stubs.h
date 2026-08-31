/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;

/* Header-only declarations are explicit external assumptions.  If a .c
 * definition of free exists in the scanned tree, that definition must repeat
 * ownership_takes and its body is then proved. */
void *malloc(size_t) __attribute__((ownership_returns(malloc)));
void free(void *) __attribute__((ownership_takes(malloc, 1)));
void *realloc(void *, size_t)
	__attribute__((ownership_returns(malloc),
	               annotate("ntlibc.reallocates:1")));
void *conditional_buffer(void *)
	__attribute__((ownership_returns(malloc),
	               annotate("ntlibc.returns-if-null:1")));
