/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <inttypes.h>

int abs(int a) { return a > 0 ? a : -a; }
long labs(long a) { return a > 0 ? a : -a; }
long long llabs(long long a) { return a > 0 ? a : -a; }
intmax_t imaxabs(intmax_t a) { return a > 0 ? a : -a; }
