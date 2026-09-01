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

void terminated_suffix_does_not_prove_prefix(void)
{
	char text[8];
	dialect_mark_terminated(text + 4);
	dialect_use_string(text); /* ownership-expect: dialect-string-exact-region */
}

void parameterized_token_length_mismatch(void)
{
	char text[8];
	dialect_mark_span(text, 4);
	dialect_use_span(text, 8); /* ownership-expect: dialect-span-length */
}

void relational_token_pointer_mismatch(void)
{
	char left[8], first[8], second[8];
	dialect_mark_disjoint(left, first, sizeof left);
	dialect_use_disjoint(left, second, sizeof left); /* ownership-expect: dialect-span-relation */
}

void dialect_bad_clear_span(
	void *data drop(dialect_span(length)), size_t length)
{
	(void)data;
	(void)length;
} /* ownership-expect: dialect-parameterized-drop-proof */

void dialect_bad_clear_string(char *text drop(dialect_terminated))
{
	(void)text;
} /* ownership-expect: dialect-string-drop-proof */
