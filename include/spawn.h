/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* <spawn.h> -- see
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/spawn.h.html
 *
 * The _POSIX_SPAWN option (SPN).  Every one of the 21 interfaces
 * basedefs/spawn.h.html lists is declared and defined here; what varies
 * is which spawn-*attributes* posix_spawn() can act on, and that is
 * spelled out in src/process/posix_spawn.c rather than papered over.
 * The short version, because it is the thing a caller has to know:
 *
 *   - the file actions (addopen/addclose/adddup2) are fully honoured,
 *     in the order added, and are the reason this header exists at all;
 *   - POSIX_SPAWN_SETSIGDEF, POSIX_SPAWN_RESETIDS and (GNU's, not
 *     POSIX's) POSIX_SPAWN_USEVFORK are satisfied by construction on NT;
 *   - POSIX_SPAWN_SETSIGMASK is honoured only for an *empty* mask,
 *     POSIX_SPAWN_SETPGROUP only for the one process group this
 *     platform has, and POSIX_SPAWN_SETSCHEDPARAM/POSIX_SPAWN_SETSCHEDULER
 *     not at all;
 *   - anything not honoured makes posix_spawn() *fail*, with the errno
 *     POSIX's ERRORS section routes that flag to, rather than being
 *     accepted and quietly dropped.
 *
 * struct sched_param is defined here rather than in <sched.h>.  POSIX
 * puts it in <sched.h> and has this header make it visible; ntlibc's
 * <sched.h> deliberately declares nothing from the
 * _POSIX_PRIORITY_SCHEDULING option group (see its header comment: a
 * configure probe that finds sched_setscheduler() concludes the option
 * group is present, and NT cannot honestly support it).  The type
 * itself is not part of that option group's *functionality* -- it is
 * just the shape posix_spawnattr_setschedparam() takes an argument in,
 * and a caller of that function needs a complete type to build one --
 * so it comes from bits/alltypes.h with its own __NEED guard, which is
 * exactly the mechanism that lets <sched.h> start defining it later
 * without a redefinition.
 */

#ifndef _SPAWN_H
#define _SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

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
/* Not POSIX: a GNU extension every consumer that uses posix_spawn at
 * all probes for (GNU make's src/job.c sets it unconditionally when the
 * macro exists).  It is a hint about *how* the new process image comes
 * into being -- "the implementation may use vfork() instead of fork()"
 * -- and __spawn() never copies the parent's address space in the first
 * place, so it is satisfied by construction here. */
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

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *__restrict,
	int, const char *__restrict, int, mode_t);

int posix_spawnattr_init(posix_spawnattr_t *);
int posix_spawnattr_destroy(posix_spawnattr_t *);
int posix_spawnattr_getflags(const posix_spawnattr_t *__restrict, short *__restrict);
int posix_spawnattr_setflags(posix_spawnattr_t *, short);
int posix_spawnattr_getpgroup(const posix_spawnattr_t *__restrict, pid_t *__restrict);
int posix_spawnattr_setpgroup(posix_spawnattr_t *, pid_t);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *__restrict, sigset_t *__restrict);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *__restrict, const sigset_t *__restrict);
int posix_spawnattr_getsigmask(const posix_spawnattr_t *__restrict, sigset_t *__restrict);
int posix_spawnattr_setsigmask(posix_spawnattr_t *__restrict, const sigset_t *__restrict);
int posix_spawnattr_getschedparam(const posix_spawnattr_t *__restrict, struct sched_param *__restrict);
int posix_spawnattr_setschedparam(posix_spawnattr_t *__restrict, const struct sched_param *__restrict);
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *__restrict, int *__restrict);
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *, int);

#ifdef __cplusplus
}
#endif

#endif
