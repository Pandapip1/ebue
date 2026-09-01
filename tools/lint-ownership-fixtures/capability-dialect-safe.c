/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "capability-dialect-fixture.h"

void macro_declared_token_cycle(void)
{
	dialect_mutex_t mutex;
	dialect_mutex_init(&mutex);
	dialect_mutex_lock(&mutex);
	dialect_mutex_share_unlock(&mutex);
	dialect_mutex_unlock(&mutex);
	dialect_mutex_destroy(&mutex);
}
