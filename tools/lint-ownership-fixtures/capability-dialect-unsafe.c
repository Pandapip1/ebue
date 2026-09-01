/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "capability-dialect-fixture.h"

void macro_lock_without_token(void)
{
	dialect_mutex_t mutex;
	dialect_mutex_lock(&mutex); /* ownership-expect: dialect-capability-missing */
}

void use_string_without_evidence(void)
{
	char text[8];
	dialect_use_string(text); /* ownership-expect: dialect-string-missing */
}

void use_string_after_invalidation(void)
{
	char text[8];
	dialect_mark_terminated(text);
	dialect_invalidate_string(text);
	dialect_use_string(text); /* ownership-expect: dialect-string-dropped */
}

void dialect_bad_clear_string(char *text drop(dialect_terminated))
{
	(void)text;
} /* ownership-expect: dialect-string-drop-proof */
