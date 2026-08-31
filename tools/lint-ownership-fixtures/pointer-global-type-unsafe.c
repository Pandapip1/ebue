/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* External linkage and the reserved spelling are still insufficient when the
 * declaration is not the canonical `struct __child *` table. */
int *__children;

int wrong_type_children_is_not_trusted(void)
{
	return *__children; /* ownership-expect */
}
