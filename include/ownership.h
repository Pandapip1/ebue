/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _OWNERSHIP_H
#define _OWNERSHIP_H

/* An opt-in ownership dialect expressed entirely in ordinary C syntax.
 * Token policy belongs to the nominal token typedef; values and operations
 * refer to that policy by name. */
#define token typedef struct { char _tok; }
#define l_unlimited __attribute__((annotate("qual:l_unlimited")))
#define implicit_drop __attribute__((annotate("qual:implicit_drop")))
#define sentinel_exclude(value) \
	__attribute__((annotate("qual:sentinel_exclude=" #value)))
#define withtok(token_name) \
	__attribute__((annotate("withtok:" #token_name)))
#define consume(token_name) \
	__attribute__((annotate("consume:" #token_name)))

#endif
