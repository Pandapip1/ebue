/* SPDX-License-Identifier: GPL-3.0-or-later */

long same_array_difference(int *items) { return &items[3] - &items[1]; }

int same_array_order(int *items) { return items < items + 1; }

void *null_pointer(void) { return (void *)0; }

/* A compile-time-constant integer cast to a pointer type is a
 * deliberate, source-visible sentinel (NT's own pseudo-handle
 * convention, SIG_DFL/SIG_IGN/SIG_ERR, MAP_FAILED, and the invalid
 * nl_catd/iconv_t/sem_t markers all take this shape) -- see
 * PointerProvenanceChecker.cpp's isConstantSentinel(). */
void *constant_sentinel(void) { return (void *)(long)-1; }

/* Pointer -> integer -> (mask/offset) -> pointer, the alignment idiom
 * used by posix_memalign()/align16()/mman.c's page-range clamps --
 * see derivesFromPointer(). */
void *alignment_roundtrip(void *p) {
  return (void *)(((unsigned long)p + 15) & ~15UL);
}

/* A conditional expression choosing between two such round trips (the
 * shape mman.c's `lo = a > b ? a : b`-style range clamp takes once its
 * operands are cast through uintptr_t). */
void *alignment_roundtrip_conditional(void *a, void *b) {
  unsigned long ia = (unsigned long)a, ib = (unsigned long)b;
  return (void *)(ia > ib ? ia : ib);
}

/* strchr()'s return shares provenance with its first argument by
 * contract (a "needle in haystack" function -- see checkPostCall());
 * the two are the same object even though the call is opaque to this
 * checker (no strchr() definition is visible to a --analyze pass over
 * one file). */
#include <string.h>
long needle_in_haystack(const char *s) {
  const char *dot = strchr(s, '.');
  return dot ? dot - s : -1;
}
