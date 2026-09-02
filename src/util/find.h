/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UTIL_FIND_H
#define UTIL_FIND_H

#include <allocation_tokens.h>
#include <ownership.h>

tokdef find_expression_allocated
	dynamic_storage
	implemented_by(heap_allocated);

#endif
