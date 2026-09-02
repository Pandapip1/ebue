/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
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


/* A strto* end pointer shares provenance only with that call's own input. */
#include <stdlib.h>
long conversion_end_from_different_input(const char *s, const char *other) {
  char *end;
  (void)strtol(s, &end, 10);
  return end - other; /* pointer-provenance-expect */
}

/* Reassigning end after conversion must discard the conversion contract. */
long overwritten_conversion_end(const char *s, const char *other) {
  char *end;
  (void)strtol(s, &end, 10);
  end = (char *)other;
  return end - s; /* pointer-provenance-expect */
}

/* An arbitrary output-pointer function has no strto* provenance contract. */
void parse_number(const char *, char **);
long untrusted_end_output(const char *s) {
  char *end;
  parse_number(s, &end);
  return end - s; /* pointer-provenance-expect */
}

/* Named kernel/loader ABI exceptions require both their audited source file
 * and function name; merely reusing a name elsewhere grants nothing. */
void *shmat(unsigned long address) {
  return (void *)address; /* pointer-provenance-expect */
}
void *raw_brk(unsigned long address) {
  return (void *)address; /* pointer-provenance-expect */
}
void *load_object(unsigned long address) {
  return (void *)address; /* pointer-provenance-expect */
}
