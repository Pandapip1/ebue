/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct FILE FILE;

int close(int);
int unlink(const char *);
long write(int, const void *, unsigned long);
int fflush(FILE *);

void discarded_close(int fd) {
  close(fd); /* fallible-result-expect */
}

int discarded_comma(const char *path) {
  return (unlink(path), 0); /* fallible-result-expect */
}

void discarded_unbraced_if(int fd, const char *buf, unsigned long n) {
  if (n > 0)
    write(fd, buf, n); /* fallible-result-expect */
}

void discarded_unbraced_while(FILE *f) {
  while (f)
    fflush(f); /* fallible-result-expect */
}

void discarded_unbraced_for_body(int fd, const char *buf, unsigned long n) {
  for (unsigned long i = 0; i < 1; i++)
    write(fd, buf, n); /* fallible-result-expect */
}

void discarded_unbraced_for_inc(int fd) {
  for (int i = 0; i < 1; close(fd)) /* fallible-result-expect */
    i++;
}
