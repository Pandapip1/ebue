/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

void inherited_destroy(void *)
	__attribute__((ownership_takes(inherited, 1)));
void *make_inherited(void)
	__attribute__((ownership_returns(inherited)));
