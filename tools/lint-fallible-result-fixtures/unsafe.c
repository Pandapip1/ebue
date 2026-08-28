/* SPDX-License-Identifier: GPL-3.0-or-later */

int close(int);
int unlink(const char *);

void discarded_close(int fd) {
  close(fd); /* fallible-result-expect */
}

int discarded_comma(const char *path) {
  return (unlink(path), 0); /* fallible-result-expect */
}
