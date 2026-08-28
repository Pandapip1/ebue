/* SPDX-License-Identifier: GPL-3.0-or-later */

int printf(const char *, ...);
int scanf(const char *, ...);

void wrong_printf_type(void) {
  printf("%s", 3); /* variadic-abi-expect */
}

void nonliteral_format(const char *format) {
  printf(format, 3); /* variadic-abi-expect */
}

void wrong_scanf_target(char *target) {
  scanf("%d", target); /* variadic-abi-expect */
}
