/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct operation_variant_result {
	int kind;
	int normal;
	int special;
} operation_result_t;

int consume_normal(operation_result_t result)
{
	if (result.kind)
		return 0;
	return result.normal;
}

int consume_special(operation_result_t result)
{
	if (!result.kind)
		return 0;
	return result.special;
}

int consume_both(operation_result_t result)
{
	if (result.kind)
		return result.special;
	return result.normal;
}

operation_result_t construct_normal(int value)
{
	operation_result_t result;
	result.kind = 0;
	result.normal = value;
	result.special = 0;
	return result;
}

operation_result_t construct_special(int code)
{
	operation_result_t result;
	result.kind = -1;
	result.normal = 0;
	result.special = code;
	return result;
}

int consume_switch(operation_result_t result)
{
	switch (result.kind) {
	case 0:
		return result.normal;
	default:
		return result.special;
	}
}
