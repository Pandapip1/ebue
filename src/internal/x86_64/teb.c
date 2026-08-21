/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "libc.h"
PTEB __teb(void)
{
	PTEB t;
	__asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(t));
	return t;
}
