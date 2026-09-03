/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * vfork.html only requires behaving as if fork() were called -- the
 * parent-suspended, address-space-shared optimisation is a performance
 * hint callers may not rely on for correctness. This library's fork()
 * is already a full copy-on-write clone, so calling it here is a
 * correct, if unoptimised, vfork, the same fallback other libcs use
 * without a real vfork facility.
 */
#include <unistd.h>

pid_t vfork(void)
{
	return fork();
}
