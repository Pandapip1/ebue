/* SPDX-License-Identifier: GPL-3.0-or-later */

long different_array_difference(int *left, int *right) {
  return left - right; /* pointer-provenance-expect */
}

int different_array_order(int *left, int *right) {
  return left < right; /* pointer-provenance-expect */
}

int *integer_pointer(unsigned long address) {
  return (int *)address; /* pointer-provenance-expect */
}

/* checkPostCall's strchr() alias must not bleed across unrelated
 * buffers: subtracting a strchr() result from a *different* string is
 * still two unrelated objects, not the same "needle in haystack" pair,
 * and must stay flagged. */
#include <string.h>
long different_haystack_difference(const char *s, const char *other) {
  const char *dot = strchr(s, '.');
  return dot ? dot - other : -1; /* pointer-provenance-expect */
}
