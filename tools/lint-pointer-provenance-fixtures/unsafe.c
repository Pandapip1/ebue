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
