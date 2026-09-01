/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "contract-fixture.h"

[[ownership_adds_token(contract_ready, 1)]]
int contract_repeated_in_definition(void *object)
{
	return contract_primitive(object);
}
