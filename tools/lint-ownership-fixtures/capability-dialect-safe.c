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

void explicit_string_evidence_cycle(void)
{
	char text[8];
	dialect_mark_terminated(text);
	dialect_use_string(text);
	dialect_clear_string(text);
}

void string_literal_creates_evidence(void)
{
	char initialized[] = "initialized";
	dialect_use_string("literal");
	dialect_use_string(initialized);
}

void dialect_clear_string(char *text drop(dialect_terminated))
{
	dialect_invalidate_string(text);
}
