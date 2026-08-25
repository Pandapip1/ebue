/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* <sched.h> -- see
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sched.h.html
 *
 * sched_yield() and struct sched_param are provided.  The rest of what
 * this header is specified to declare -- the SCHED_FIFO/SCHED_RR/
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
 * STRUCT SCHED_PARAM IS THE EXCEPTION, and this banner used to list it
 * among the option-group members above.  Its sentence -- "The <sched.h>
 * header shall define the sched_param structure ... shall include at
 * least the following member: int sched_priority" -- carries NO margin
 * marker on the page, while the sentences either side of it do ([PS] on
 * pid_t, [SS|TSP] on time_t and on the sporadic-server members added to
 * the same struct).  So the struct is POSIX base and unconditional even
 * though every policy and function that USES it is optional.  Read off
 * the rendered page rather than inferred from the company it keeps.
 *
 * That is the shape of the mistake worth remembering: a blanket
 * justification that is right about eight of its nine subjects reads as
 * more authoritative than a list would, and hides the ninth.
 *
 * Exposing it costs nothing and implies nothing.  The type already
 * existed in bits/alltypes.h -- whose comment anticipated exactly this,
 * saying it lives there "so that <sched.h> can start defining it ...
 * the day that option group is claimed" -- and <spawn.h> already
 * reaches it the same way.  Declaring the struct is not declaring the
 * option group: no policy constant and no function is added here, and
 * the reasoning above for declining those stands untouched.
 *
 * sched_yield() itself is POSIX base, mandatory for conformance, and
 * is *not* part of that option group. */

#ifndef _SCHED_H
#define _SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_struct_sched_param

#include <bits/alltypes.h>

int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif
