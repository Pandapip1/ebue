/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_close(): POSIX.1-2024's close() with an explicit EINTR policy.
 *
 * https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_close.html
 * lets flag be POSIX_CLOSE_RESTART (fd left open, -1/EINTR returned, on a
 * caught signal) or 0 (fd is always closed even if a signal interrupted
 * it; -1/EINTR is never returned).  That distinction exists because on a
 * system with real asynchronous signal delivery, a close() blocked in
 * the kernel can be interrupted mid-syscall by a caught signal, leaving
 * it genuinely ambiguous whether the descriptor got closed.
 *
 * That ambiguity cannot happen here: this library never delivers a
 * signal asynchronously into a blocked syscall in the first place (see
 * src/signal/signal.c's header comment -- delivery is synchronous only,
 * driven by raise()/kill()/a hardware exception, none of which unwinds
 * an in-progress NT close), and close() itself (src/unistd/close.c) is
 * one NtClose call that does not return EINTR.  So flag changes nothing
 * observable, and posix_close is just close() with an extra, ignored
 * argument -- the same conclusion musl reached for the same reason on a
 * kernel that also does not restart close() on the reads that matter
 * here (src/unistd/posix_close.c: "return close(fd);", flag unused).
 *
 * include/unistd.h declares this unconditionally (matching musl's own
 * unistd.h, which does not gate posix_close behind any feature-test
 * macro either, despite it being newer than any _POSIX_VERSION either
 * library claims) rather than adding a new POSIX.1-2024 feature-test
 * tier to features.h for one function.
 */
#include <unistd.h>

int posix_close(int fd, int flags) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	(void)flags;   /* POSIX_CLOSE_RESTART is moot here; see above. */
	return close(fd);
}
