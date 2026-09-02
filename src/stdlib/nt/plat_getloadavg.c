/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getloadavg() -- NT backend. Undefined-ok, in the same documented
 * sense include/unistd.h's daemon()/vhangup()/sethostname() comments
 * already use for this platform: NT genuinely has no analogue of a
 * Unix run-queue load average anywhere in its process/performance
 * model (the closest real signal, NtQuerySystemInformation's
 * SystemProcessorPerformanceInformation, reports per-processor busy
 * time, not a decaying run-queue-length average -- a different
 * quantity, not the same one under a different name), so there is
 * nothing honest to compute here.
 *
 * This always fails (-1, matching real getloadavg(3)'s own "on
 * failure, -1 is returned" for exactly this "the load average was
 * unobtainable" case), and src/util/atd.c's own header documents,
 * loudly rather than silently, what that means for batch(1p) jobs on
 * this platform: they run as soon as atd notices them, with no load
 * gate at all -- not a fabricated load reading standing in for a
 * signal this OS does not have.
 */
#include <stdlib.h>

int getloadavg(double *a, int n)
{
	(void)a; (void)n;
	return -1;
}
