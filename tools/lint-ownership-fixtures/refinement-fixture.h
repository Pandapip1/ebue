/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

tokdef unchecked_fd sentinel_exclude(-1);
tokdef unchecked_null sentinel_exclude(NULL) blocks_dereference;

withtok(unchecked_fd)
int acquire_fd(void);
void use_fd(int fd withouttok(unchecked_fd));

withtok(unchecked_null)
int *maybe_pointer(void);
