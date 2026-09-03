/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* <spawn.h> -- the _POSIX_SPAWN option.
 *
 * What each flag actually does (src/process/posix_spawn.c has the full
 * accounting):
 *   - file actions (addopen/addclose/adddup2) are fully honoured, in
 *     order added;
 *   - POSIX_SPAWN_SETSIGDEF, POSIX_SPAWN_RESETIDS and
 *     POSIX_SPAWN_USEVFORK are satisfied by construction on NT;
 *   - POSIX_SPAWN_SETSIGMASK is honoured only for an *empty* mask,
 *     POSIX_SPAWN_SETPGROUP only for this platform's one process group,
 *     and POSIX_SPAWN_SETSCHEDPARAM/SETSCHEDULER not at all;
 *   - anything not honoured makes posix_spawn() *fail*, rather than
 *     being silently dropped.
 *
 * struct sched_param is defined here, not in <sched.h>, because
 * ntlibc's <sched.h> deliberately declares nothing from the
 * _POSIX_PRIORITY_SCHEDULING option group (a configure probe that
 * finds sched_setscheduler() would conclude it's present, which NT
 * can't honestly support) -- but posix_spawnattr_setschedparam() still
 * needs the complete type.
 */

#ifndef _SPAWN_H
#define _SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <string_tokens.h>

#define __NEED_mode_t
#define __NEED_pid_t
#define __NEED_sigset_t
#define __NEED_struct_sched_param
#include <bits/alltypes.h>

/* Values match glibc's and the mingw-w64/musl families', so an object
 * file compiled against one of those headers and linked here does not
 * silently mean a different flag.  POSIX fixes the names, not the
 * values. */
#define POSIX_SPAWN_RESETIDS       0x01
#define POSIX_SPAWN_SETPGROUP      0x02
#define POSIX_SPAWN_SETSIGDEF      0x04
#define POSIX_SPAWN_SETSIGMASK     0x08
#define POSIX_SPAWN_SETSCHEDPARAM  0x10
#define POSIX_SPAWN_SETSCHEDULER   0x20
/* Not POSIX: a GNU extension hinting the implementation may use
 * vfork(). __spawn() never copies the parent's address space, so it's
 * satisfied by construction here. */
#define POSIX_SPAWN_USEVFORK       0x40

/* Both objects are opaque: POSIX specifies no member and no
 * initialisation other than the _init() call, so every member here is
 * in the implementation's namespace and may change shape. */

/* struct __spawn_action stays incomplete on purpose -- it is private to
 * src/process/spawn_file_actions.c. */
typedef struct {
	int __len;                        /* actions recorded */
	int __cap;                        /* entries __actions has room for */
	struct __spawn_action *__actions;
} posix_spawn_file_actions_t;

typedef struct {
	short __flags;
	pid_t __pgroup;
	sigset_t __sigdefault;
	sigset_t __sigmask;
	int __policy;
	struct sched_param __param;
} posix_spawnattr_t;

/* POSIX writes the argv/envp parameters as `char *const [restrict]`.
 * That spelling is C99-only -- a qualifier inside array brackets is not
 * C++, and tools/hdr-hygiene.sh compiles every extern "C" header as C++
 * too -- so the identical adjusted type is written out as a pointer
 * instead. */
int posix_spawn(pid_t *__restrict, const char *__restrict,
	const posix_spawn_file_actions_t *,
	const posix_spawnattr_t *__restrict,
	char *const *__restrict, char *const *__restrict);
int posix_spawnp(pid_t *__restrict, const char *__restrict,
	const posix_spawn_file_actions_t *,
	const posix_spawnattr_t *__restrict,
	char *const *__restrict, char *const *__restrict);

/* The add*() functions are deliberately NOT marked for fa: each only
 * forwards it into fa_push(), never dereferencing it directly. */
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *) __attribute__((nonnull(1)));
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *) __attribute__((nonnull(1)));
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *__restrict,
	int, const char *__restrict withtok(null_terminated), int, mode_t)
	__attribute__((nonnull(3)));

/* destroy() is deliberately NOT marked: its body is `(void)a;`, never
 * dereferencing a at all. */
int posix_spawnattr_init(posix_spawnattr_t *) __attribute__((nonnull(1)));
int posix_spawnattr_destroy(posix_spawnattr_t *);
int posix_spawnattr_getflags(const posix_spawnattr_t *__restrict, short *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_setflags(posix_spawnattr_t *, short) __attribute__((nonnull(1)));
int posix_spawnattr_getpgroup(const posix_spawnattr_t *__restrict, pid_t *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_setpgroup(posix_spawnattr_t *, pid_t) __attribute__((nonnull(1)));
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *__restrict, sigset_t *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_setsigdefault(posix_spawnattr_t *__restrict, const sigset_t *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_getsigmask(const posix_spawnattr_t *__restrict, sigset_t *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_setsigmask(posix_spawnattr_t *__restrict, const sigset_t *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_getschedparam(const posix_spawnattr_t *__restrict, struct sched_param *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_setschedparam(posix_spawnattr_t *__restrict, const struct sched_param *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *__restrict, int *__restrict)
    __attribute__((nonnull(1, 2)));
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *, int) __attribute__((nonnull(1)));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
