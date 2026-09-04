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

static const char *mixed_cursor_return(const char *p, const char *other,
                                       int choose) {
  return choose ? p + 1 : other;
}
long mixed_cursor_origin(const char *p, const char *other, int choose) {
  return mixed_cursor_return(p, other, choose) - p; /* pointer-provenance-expect */
}

static const char *reset_cursor(const char *p, const char *other) {
  p = other;
  return p;
}
long reset_cursor_origin(const char *p, const char *other) {
  return reset_cursor(p, other) - p; /* pointer-provenance-expect */
}

static void replace_cursor(const char **p, const char *other) { *p = other; }
static const char *aliased_cursor(const char *p, const char *other) {
  replace_cursor(&p, other);
  return p;
}
long aliased_cursor_origin(const char *p, const char *other) {
  return aliased_cursor(p, other) - p; /* pointer-provenance-expect */
}

static const char *integer_cursor(const char *p, unsigned long delta) {
  unsigned long bits = (unsigned long)p;
  bits += delta;
  return (const char *)bits;
}
long integer_cursor_origin(const char *p, unsigned long delta) {
  return integer_cursor(p, delta) - p; /* pointer-provenance-expect */
}

const char *external_cursor(const char *p);
long external_cursor_origin(const char *p) {
  return external_cursor(p) - p; /* pointer-provenance-expect */
}

typedef const char *(*cursor_fn)(const char *);
long indirect_cursor_origin(cursor_fn fn, const char *p) {
  return fn(p) - p; /* pointer-provenance-expect */
}

#define returns_element_of(registry) \
  __attribute__((annotate("ntlibc_relation_returns_element_of:" #registry)))
#define parameter_element_of(index, registry) \
  __attribute__((annotate("ntlibc_relation_parameter_element_of:" #index ":" #registry)))

static int *contract_registry;
static int *other_registry;

static int *wrong_registry_return(unsigned i)
    returns_element_of(contract_registry);
static int *wrong_registry_return(unsigned i) {
  return &other_registry[i]; /* pointer-provenance-expect */
}
long exercise_wrong_registry_return(unsigned i) {
  return wrong_registry_return(i) != 0;
}

static long contract_consumer(int *p)
    parameter_element_of(0, contract_registry);
static long contract_consumer(int *p) { return p - contract_registry; }

long wrong_registry_argument(unsigned i) {
  return contract_consumer(&other_registry[i]); /* pointer-provenance-expect */
}

static int *contract_lookup(unsigned i) returns_element_of(contract_registry);
static int *contract_lookup(unsigned i) { return &contract_registry[i]; }

long rebound_registry_argument(unsigned i, int *replacement) {
  int *p = contract_lookup(i);
  contract_registry = replacement;
  return contract_consumer(p); /* pointer-provenance-expect */
}

static int *reset_registry;
static long reset_consumer(int *p, int *other)
    parameter_element_of(0, reset_registry);
static long reset_consumer(int *p, int *other) {
  p = other;
  return p - reset_registry; /* pointer-provenance-expect */
}
long exercise_reset_consumer(unsigned i, int *other) {
  return reset_consumer(&reset_registry[i], other);
}

static int *escaped_registry;
static long escaped_consumer(int *p)
    parameter_element_of(0, escaped_registry);
static long escaped_consumer(int *p) {
  return p - escaped_registry; /* pointer-provenance-expect */
}
int **escape_registry_storage(void) { return &escaped_registry; }
long exercise_escaped_consumer(unsigned i) {
  return escaped_consumer(&other_registry[i]);
}

static int *address_taken_registry;
static long address_taken_consumer(int *p)
    parameter_element_of(0, address_taken_registry);
static long address_taken_consumer(int *p) {
  return p - address_taken_registry; /* pointer-provenance-expect */
}
typedef long (*registry_consumer_fn)(int *);
registry_consumer_fn expose_consumer(void) { return address_taken_consumer; }
long exercise_address_taken_consumer(unsigned i) {
  return address_taken_consumer(&other_registry[i]);
}

/* unsafe_assume_valid_pointer(expr) -- same inlined marker as safe.c's
 * copy (fixtures compile with no include path); see that file's
 * marked_unprovable_cast() for proof the marker actually suppresses
 * the one cast it wraps. */
#ifdef __clang_analyzer__
#define unsafe_assume_valid_pointer(expr) \
  (__extension__({ \
    __typeof__(expr) __ntlibc_unsafe_ptr__ \
      __attribute__((annotate("ntlibc_unsafe_assume_valid_pointer"))) \
      = (expr); \
    __ntlibc_unsafe_ptr__; \
  }))
#else
#define unsafe_assume_valid_pointer(expr) (expr)
#endif

/* The marker resolves only the one cast it is applied to. An unmarked,
 * otherwise-identical integer-to-pointer conversion of the very same
 * parameter, in the very same function, right next to a marked one,
 * must still be flagged -- proving this is not a blanket loosening of
 * the checker for the whole function, the translation unit, or even
 * the one source variable the marked cast happens to be assigned to. */
void *unmarked_cast_still_flagged(unsigned long value) {
  void *marked = unsafe_assume_valid_pointer((void *)value);
  void *unmarked = (void *)value; /* pointer-provenance-expect */
  return marked ? marked : unmarked;
}
