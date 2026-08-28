/* SPDX-License-Identifier: GPL-3.0-or-later */

long same_array_difference(int *items) { return &items[3] - &items[1]; }

int same_array_order(int *items) { return items < items + 1; }

void *null_pointer(void) { return (void *)0; }
