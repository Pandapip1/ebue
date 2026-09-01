/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */


tokdef broken_allocated
	dynamic_storage;
tokdef inherited_allocated
	dynamic_storage;

void inherited_destroy(void *consume(inherited_allocated));
withtok(inherited_allocated)
void *make_inherited(void);

void broken_destroy(void *consume(broken_allocated));
withtok(broken_allocated)
void *make_broken(void);
