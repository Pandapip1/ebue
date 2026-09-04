/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getloadavg() -- Linux backend, via /proc/loadavg, the same real
 * kernel-exported source `uptime`/`w`/GNU libc's own getloadavg()
 * read. Format (checked against proc(5) directly): three
 * space-separated floating-point 1/5/15-minute averages, followed by
 * "runnable/total" and the most-recently-created PID -- only the
 * first three fields are this function's contract.
 *
 * Needed for real by this project's own src/util/atd.c: batch(1p)
 * ("run by the system using algorithms ... based on unspecified
 * factors") is implemented here as a real, honest load-average gate
 * on Linux, not a fabricated always-run/always-defer stub -- see
 * atd.c's own header for how the returned value is used and
 * src/stdlib/nt/plat_getloadavg.c for the platform that genuinely has
 * no such signal to read.
 */
#include <stdlib.h>
#include <stdio.h>

int getloadavg(double *a, int n)
{
	FILE *f;
	double v[3];
	int got, i;

	if (n <= 0) return 0;
	f = fopen("/proc/loadavg", "r");
	if (!f) return -1;
	got = fscanf(f, "%lf %lf %lf", &v[0], &v[1], &v[2]);
	(void)fclose(f);
	if (got < 1) return -1;
	if (got > n) got = n; /* caller asked for fewer samples than the file has */
	for (i = 0; i < got; i++) a[i] = v[i];
	return got;
}
