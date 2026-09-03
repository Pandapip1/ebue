/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_close(): POSIX.1-2024's close() with an explicit EINTR policy
 * (flag: POSIX_CLOSE_RESTART leaves fd open and returns EINTR on a
 * caught signal, 0 always closes it). That ambiguity can't arise here:
 * this library only delivers signals synchronously (src/signal/signal.c),
 * never into a blocked syscall, and close() is one NtClose call that
 * never returns EINTR -- so flag changes nothing observable and this is
 * just close() with an ignored argument, same as musl reaches for the
 * same reason.
 */
#include <unistd.h>

int posix_close(int fd, int flags) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	(void)flags;   /* POSIX_CLOSE_RESTART is moot here; see above. */
	return close(fd);
}
