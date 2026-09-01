/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _STRING_TOKENS_H
#define _STRING_TOKENS_H

#include <ownership.h>

/* Evidence that a NUL byte is reachable from this exact character pointer.
 * The evidence is freely copyable and may be forgotten: neither operation
 * changes the underlying bytes.  Writes which may remove the last reachable
 * NUL explicitly drop it; writes which establish one explicitly grant it. */
tokdef null_terminated
	l_unlimited
	implicit_drop
	string_literal;

#endif
