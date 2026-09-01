/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "capability-dialect-fixture.h"

void macro_lock_without_token(void)
{
	dialect_mutex_t mutex;
	dialect_mutex_lock(&mutex); /* ownership-expect: dialect-capability-missing */
}
