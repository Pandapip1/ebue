/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * struct __spawn_action -- the one recorded file action.
 *
 * <spawn.h> leaves this struct incomplete (posix_spawn_file_actions_t
 * holds only a pointer to an array of them), so it is private to
 * src/process/: spawn_file_actions.c appends them and posix_spawn.c
 * replays them.  A private header rather than src/internal/libc.h
 * because nothing outside this directory has any business knowing the
 * shape -- the same argument src/wordexp/internal.h makes for itself.
 */
#ifndef NTLIBC_SPAWN_INTERNAL_H
#define NTLIBC_SPAWN_INTERNAL_H

#include <spawn.h>

/* __SPAWN_CLOSE and __SPAWN_OPEN join this when addclose()/addopen()
 * do. */
enum { __SPAWN_DUP2 = 1 };

struct __spawn_action {
	int kind;
	int fd;         /* __SPAWN_DUP2: the *source*, POSIX's `fildes` */
	int newfd;      /* __SPAWN_DUP2: fd is duplicated onto newfd */
};

#endif
