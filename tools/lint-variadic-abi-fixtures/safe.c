/* SPDX-License-Identifier: GPL-3.0-or-later */

int printf(const char *, ...);
int scanf(const char *, ...);

int matched_formats(char *text, int number, int *output) {
  printf("%s %d", text, number);
  return scanf("%d", output);
}
