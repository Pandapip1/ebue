/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* The reserved spelling is not enough: a file-local imitation does not carry
 * libc's externally-published child-table invariant. */
struct __child { int pid; };
static struct __child *__children;

int file_local_children_is_not_trusted(void)
{
	return __children[0].pid; /* ownership-expect */
}
