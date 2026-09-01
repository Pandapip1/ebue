/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

typedef __SIZE_TYPE__ size_t;

token widget_allocated
	dynamic_storage;

/* Header-only declarations are explicit external assumptions.  If a .c
 * definition of free exists in the scanned tree, that definition must repeat
 * ownership_takes and its body is then proved. */
[[clang::ownership_returns(malloc)]]
void *malloc(size_t);
[[clang::ownership_takes(malloc, 1)]]
void free(void *);
[[ownership_reallocates(1), clang::ownership_returns(malloc)]]
void *realloc(void *, size_t);
[[ownership_returns_argument(1), clang::ownership_returns(malloc)]]
void *conditional_buffer(void *);

void *make_widget(void) withtok(widget_allocated);
void destroy_widget(void *object consume(widget_allocated));
