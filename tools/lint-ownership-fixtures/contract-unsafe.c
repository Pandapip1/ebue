/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "contract-fixture.h"

/* ownership-contract-expect: inherited-only */
int contract_inherited_only(void *object)
{
	(void)object;
	return 0; /* ownership-expect: token-drop-proof */
}
