/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

typedef __SIZE_TYPE__ size_t;

tokdef widget_allocated
	dynamic_storage;
tokdef heap_allocated
	dynamic_storage;
#undef tokdef

/* Header-only declarations are explicit external assumptions.  If a .c
 * definition of free exists in the scanned tree, that definition must repeat
 * consume contract and its body is then proved. */
withtok(heap_allocated)
void *malloc(size_t);
void free(void *consume(heap_allocated));
withtok(heap_allocated)
void *realloc(void *consume_if_nonnull_return(heap_allocated), size_t);
withtok(heap_allocated)
void *conditional_buffer(void *withtok(heap_allocated));

void *make_widget(void) withtok(widget_allocated);
void destroy_widget(void *object consume(widget_allocated));
