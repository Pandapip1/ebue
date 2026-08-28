/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

int uninitialized_local(void) {
  int value;
  return value; /* initialization-expect */
}

int uninitialized_field(void) {
  struct pair {
    int first;
    int second;
  } value;
  value.first = 1;
  return value.second; /* initialization-expect */
}
