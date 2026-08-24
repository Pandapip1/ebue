/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* <sched.h> -- see
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sched.h.html
 *
 * Only sched_yield() is provided.  Everything else this header is
 * specified to declare -- struct sched_param, the SCHED_FIFO/SCHED_RR/
 * SCHED_SPORADIC/SCHED_OTHER policies, and sched_getparam/sched_setparam/
 * sched_getscheduler/sched_setscheduler/sched_get_priority_max/
 * sched_get_priority_min/sched_rr_get_interval -- belongs to the
 * _POSIX_PRIORITY_SCHEDULING option group, which ntlibc does not claim
 * (include/unistd.h defines no _POSIX_PRIORITY_SCHEDULING and
 * src/unistd/sysconf.c answers no _SC_PRIORITY_SCHEDULING) and which
 * NT cannot honestly support: NT thread scheduling has priorities but no policy
 * distinction at all, so SCHED_FIFO and SCHED_RR would be the same
 * thing under two names, and sched_setscheduler() could only ever
 * report ENOTSUP.  Declaring them so they could return an error is
 * worse than not declaring them: a configure probe that finds the
 * symbol concludes the option group is present.  Basedefs permits
 * exactly this -- those declarations sit inside the standard's own
 * `[PS]` margin markers.
 *
 * sched_yield() itself is POSIX base, mandatory for conformance, and
 * is *not* part of that option group. */

#ifndef _SCHED_H
#define _SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif
