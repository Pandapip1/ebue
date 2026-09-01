/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

[[ownership_adds_token(contract_ready, 1)]]
int contract_repeated_in_definition(void *object);

[[ownership_drops_token(contract_ready, 1)]]
int contract_inherited_only(void *object);

[[ownership_adds_token(contract_ready, 1)]]
int contract_primitive(void *object);
