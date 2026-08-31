/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

[[clang::ownership_takes(inherited, 1)]]
void inherited_destroy(void *);
[[clang::ownership_returns(inherited)]]
void *make_inherited(void);
