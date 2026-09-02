/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _ALLOCATION_TOKENS_H
#define _ALLOCATION_TOKENS_H

#include <ownership.h>

/* The allocator implementation graph is declared together so every
 * translation unit that sees a public boundary can validate the complete
 * nominal graph.  Each function boundary consumes only its direct edge:
 * public/CRT allocation -> platform allocator -> native RTL heap on NT.
 * Linux's platform allocator returns interior chunks from its own slabs, so
 * it is a terminal nominal family rather than a same-object morphism to the
 * page mappings that back those slabs. */
tokdef rtl_heap_allocated
	dynamic_storage;
tokdef platform_heap_allocated
	dynamic_storage
#if !defined(__linux__)
	implemented_by(rtl_heap_allocated);
#else
	;
#endif
tokdef internal_heap_allocated
	dynamic_storage
	implemented_by(platform_heap_allocated);
tokdef heap_allocated
	dynamic_storage
	implemented_by(platform_heap_allocated);

#endif
