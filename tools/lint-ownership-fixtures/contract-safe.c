/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "contract-fixture.h"

int contract_repeated_in_definition(void *object grant(contract_ready))
{
	return contract_primitive(object);
}
