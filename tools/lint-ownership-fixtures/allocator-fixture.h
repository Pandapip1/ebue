/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

typedef __SIZE_TYPE__ size_t;

tokdef heap_allocated dynamic_storage;
#undef tokdef

withtok(heap_allocated)
void *malloc(size_t);
withtok(heap_allocated)
void *calloc(size_t, size_t);
withtok(heap_allocated)
void *__malloc(size_t);
withtok(heap_allocated)
void *realloc(void *consume_if_nonnull_return(heap_allocated), size_t);
void free(void *consume(heap_allocated));
