/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct operation_variant_result {
	int kind;
	int normal;
	int special;
} operation_result_t;

int unguarded_normal(operation_result_t result)
{
	return result.normal; /* tagged-result-expect */
}

int unguarded_special(operation_result_t result)
{
	return result.special; /* tagged-result-expect */
}

int wrong_normal_branch(operation_result_t result)
{
	if (!result.kind)
		return 0;
	return result.normal; /* tagged-result-expect */
}

int wrong_special_branch(operation_result_t result)
{
	if (result.kind)
		return 0;
	return result.special; /* tagged-result-expect */
}

int late_check(operation_result_t result)
{
	int value = result.normal; /* tagged-result-expect */
	if (result.kind)
		return 0;
	return value;
}

struct missing_kind_variant_result {
	int normal;
	int special;
};

int malformed_result(struct missing_kind_variant_result result)
{
	return result.normal; /* tagged-result-expect */
}
