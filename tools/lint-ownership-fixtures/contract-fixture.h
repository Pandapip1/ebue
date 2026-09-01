/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

tokdef contract_ready;
#undef tokdef

int contract_repeated_in_definition(void *object grant(contract_ready));
int contract_inherited_only(void *object consume(contract_ready));
int contract_primitive(void *object grant(contract_ready));
