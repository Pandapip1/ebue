/* SPDX-License-Identifier: GPL-3.0-or-later */

/* See safe.c for why these are local stub prototypes rather than real
 * <time.h>/<string.h> includes. */
struct tm { int tm_year; };
struct tm *gmtime(const long *);
char *strtok(char *, const char *);
struct tm *fake_gmtime(const long *);
struct tm *fake_localtime(const long *);
int external_sink(struct tm *);

int self_invalidation(long *t1, long *t2) {
  struct tm *p1 = gmtime(t1);
  gmtime(t2);
  return p1->tm_year; /* reentrancy-expect */
}

int strtok_reentrancy(char *s1, char *s2, const char *sep) {
  char *tok1 = strtok(s1, sep);
  strtok(s2, sep);
  return *tok1; /* reentrancy-expect */
}

int sibling_invalidation(long *t1, long *t2) {
  struct tm *p1 = fake_gmtime(t1);
  fake_localtime(t2);
  return p1->tm_year; /* reentrancy-expect */
}

int copy_propagation(long *t1, long *t2) {
  struct tm *p1 = gmtime(t1);
  struct tm *p2 = p1;
  gmtime(t2);
  return p2->tm_year; /* reentrancy-expect */
}

int passed_onward(long *t1, long *t2) {
  struct tm *p1 = gmtime(t1);
  gmtime(t2);
  return external_sink(p1); /* reentrancy-expect */
}
