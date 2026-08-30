/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * struct ntlibc_linux_sync, promoted out of src/thread/linux/
 * plat_thread.c (where it started as a private, file-scoped type) so
 * that other Linux backends -- named semaphores and stop-events
 * (src/thread/linux/plat_thread.c's own __plat_named_semaphore_*(),
 * src/signal/linux/plat_signal.c's __plat_stop_event_*()) -- can build
 * objects the SAME generic __plat_wait_one()/__plat_event_set()/
 * __plat_semaphore_post() already understand, rather than inventing a
 * second, parallel synchronization primitive. Every field and its
 * meaning is exactly what plat_thread.c's own banner already
 * documents: `futex` is the wait/wake word (a semaphore's count, or an
 * event's zero/nonzero flag), `max` is a semaphore's ceiling (unused
 * for an event), `kind` distinguishes the two so __plat_wait_one() can
 * tell a decrementing P/V wait from a manual-reset non-consuming one.
 */
#ifndef _NTLIBC_LINUX_SYNC_H
#define _NTLIBC_LINUX_SYNC_H

enum { NTLIBC_LX_SYNC_SEMAPHORE = 1, NTLIBC_LX_SYNC_EVENT = 2 };

struct ntlibc_linux_sync {
	int futex;
	int max;
	unsigned char kind;
};

#endif
