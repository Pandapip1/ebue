/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * vfork(): POSIX only requires that it behave as if fork() were called
 * (https://pubs.opengroup.org/onlinepubs/9699919799/functions/vfork.html);
 * the parent-suspended, address-space-shared optimisation is a
 * performance hint a caller may not rely on for correctness.  This
 * library's fork() (src/process/fork.c, via RtlCloneUserProcess) is
 * already a full copy-on-write clone, so calling it here is a correct,
 * if unoptimised, vfork -- the same fallback other libcs use on a target
 * without a real vfork facility.  Nothing above needed changing to add
 * this; it only calls the existing, already-declared fork().
 */
#include <unistd.h>

pid_t vfork(void)
{
	return fork();
}
