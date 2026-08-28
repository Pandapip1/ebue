/* SPDX-License-Identifier: GPL-3.0-or-later */

typedef int sig_atomic_t;
typedef void (*handler_t)(int);
handler_t signal(int, handler_t);
long write(int, const void *, unsigned long);

static volatile sig_atomic_t observed;

static void safe_handler(int number) {
  observed = number;
  write(2, "signal\n", 7);
}

void install_safe(void) { signal(1, safe_handler); }
