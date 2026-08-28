/* SPDX-License-Identifier: GPL-3.0-or-later */

int initialized_local(void) {
  int value;
  value = 7;
  return value;
}

int initialized_field(void) {
  struct pair {
    int first;
    int second;
  } value = {1, 2};
  return value.second;
}
