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

void immutable_string_literal_table_creates_evidence(unsigned i)
{
	static const char *const table[] = { "first", "second" };

	if (i < sizeof table / sizeof table[0])
		dialect_use_string(table[i]);
}

extern const char *runtime_string;

void literal_at_constant_index_creates_evidence(void)
{
	const char *const table[] = { "literal", runtime_string };

	dialect_use_string(table[0]);
}

void immutable_string_literal_member_table_creates_evidence(unsigned i)
{
	struct entry { const char *name; int value; };
	static const struct entry table[] = {
		{ "first", 1 }, { "second", 2 }
	};

	if (i < sizeof table / sizeof table[0])
		dialect_use_string(table[i].name);
}

void dialect_clear_string(char *text drop(dialect_terminated))
{
	dialect_invalidate_string(text);
}

char *dialect_copy_string(char *text grant(dialect_terminated))
{
	dialect_mark_terminated(text);
	return text;
}

void pointer_success_grants_evidence(void)
{
	char text[8];
	char *result = dialect_copy_string(text);
	if (result) {
		dialect_use_string(text);
		dialect_use_string(result);
	}
}

void parameterized_token_instances_match(void)
{
	char left[8], right[8];
	dialect_mark_span(left, sizeof left);
	dialect_use_span(left, sizeof left);
	dialect_mark_disjoint(left, right, sizeof left);
	dialect_use_disjoint(left, right, sizeof left);
}

void dialect_clear_span(void *data drop(dialect_span(length)), size_t length)
{
	dialect_invalidate_span(data, length);
}
