/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ualarm(): include/unistd.h's own declaration carries an "undefined-ok"
 * marker for exactly the same NT-specific reason getitimer()/setitimer()
 * used to (src/unistd/sleep.c's alarm() timer is an APC that only runs
 * during an alertable wait, so a repeating request coalesces). That
 * marker stays true of, and only checked against, the NT build.
 *
 * ualarm.html's own DESCRIPTION says outright what this function is:
 * "The ualarm() function shall set a timer to generate a SIGALRM signal
 * after the number of microseconds specified by useconds. ... If the
 * interval argument is non-zero, a SIGALRM signal shall be generated
 * every interval microseconds after the first" -- ITIMER_REAL, in
 * different units. So, per this task's own instruction, this is built
 * directly on this library's own new setitimer(ITIMER_REAL, ...)
 * (src/time/linux/plat_itimer.c) rather than a second raw mechanism:
 * there is nothing ualarm() needs that setitimer() does not already
 * provide, real repeating delivery included.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <sys/time.h>

unsigned ualarm(unsigned useconds, unsigned interval)
{
	struct itimerval new, old;

	/* ualarm.html's own units are whole microseconds; struct timeval's
	 * tv_usec must stay within [0, 999999], so seconds and microseconds
	 * are split out explicitly rather than handing 10^6+ straight to
	 * tv_usec -- setitimer() itself would reject that (itimerval_valid(),
	 * src/time/linux/plat_itimer.c) and silently truncating here would
	 * misreport what was actually armed. */
	new.it_value.tv_sec = (time_t)(useconds / 1000000U);
	new.it_value.tv_usec = (suseconds_t)(useconds % 1000000U);
	new.it_interval.tv_sec = (time_t)(interval / 1000000U);
	new.it_interval.tv_usec = (suseconds_t)(interval % 1000000U);

	/* ualarm.html RETURN VALUE: "the number of microseconds until any
	 * previously scheduled alarm was due to be delivered, or 0 if there
	 * was no previously scheduled alarm." ualarm() itself defines no
	 * failure return at all (unlike alarm(), it has no [ERRORS] section
	 * either) -- a setitimer() failure has nothing meaningful to fold
	 * into an unsigned microsecond count, so it is treated the same way
	 * this library's own alarm() already treats a failed arm (src/
	 * unistd/sleep.c's own comment): silently, as "no alarm scheduled". */
	if (setitimer(ITIMER_REAL, &new, &old) < 0) return 0;
	if (old.it_value.tv_sec < 0) return 0;
	/* Clamped rather than allowed to wrap: a previous it_value beyond
	 * UINT_MAX microseconds (~71.5 minutes) cannot be represented in
	 * ualarm()'s own unsigned-microseconds return at all, and reporting
	 * the wrapped low bits would be a wrong answer, not an honest one. */
	if (old.it_value.tv_sec > (time_t)(0xFFFFFFFFU / 1000000U)) return 0xFFFFFFFFU;
	return (unsigned)old.it_value.tv_sec * 1000000U + (unsigned)old.it_value.tv_usec;
}

// NOLINTEND(misc-include-cleaner)
