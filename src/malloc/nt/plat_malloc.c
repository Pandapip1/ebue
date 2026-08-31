/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * malloc on top of the NT heap.
 *
 * Every process has a heap ntdll made for it before the first instruction
 * of the program ran, and ntdll's own allocator is a serious one: size
 * classes, a low-fragmentation front end, coalescing, guard pages under
 * the debugger.  Writing another on top of NtAllocateVirtualMemory would
 * be more code for a worse result, so this is RtlAllocateHeap on the
 * process heap.  The heap lives in the address space, so it survives
 * fork the same way every other piece of memory does.
 *
 * RtlAllocateHeap(0) returns a usable pointer, which is what malloc(0)
 * is allowed to do.  Alignment is 8 on i386 and 16 on x86_64, which is
 * what the heap gives -- src/malloc/malloc.c's posix_memalign() over-
 * allocates for anything larger and remembers the real block itself,
 * entirely above this interface.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include "plat_malloc.h"
#include "libc.h"

void *__plat_alloc(size_t n, int zero)
{
	return RtlAllocateHeap(__process_heap(), zero ? HEAP_ZERO_MEMORY : 0, n);
}

void *__plat_realloc(void *p, size_t n)
{
	return RtlReAllocateHeap(__process_heap(), 0, p, n);
}

size_t __plat_alloc_size(void *p)
{
	return RtlSizeHeap(__process_heap(), 0, p);
}

__attribute__((ownership_takes(plat_heap, 1)))
void __plat_dealloc(void *p)
{
	RtlFreeHeap(__process_heap(), 0, p);
}

// NOLINTEND(misc-include-cleaner)
