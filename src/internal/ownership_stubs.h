/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _NTLIBC_OWNERSHIP_STUBS_H
#define _NTLIBC_OWNERSHIP_STUBS_H

/* These declarations are the leaf axioms used to connect a concrete state
 * transition in a function body to its ownership-token contract.  They are
 * visible only to the static analyzer; ordinary builds erase each proof call
 * and therefore gain neither a runtime dependency nor a private ABI. */
#ifdef __clang_analyzer__

__attribute__((ownership_adds_token(pthread_mutex_unlocked, 1)))
void __ownership_pthread_mutex_initialized(void *);
__attribute__((ownership_drops_token(pthread_mutex_unlocked, 1),
	ownership_adds_duplicable_token(pthread_mutex_locked, 1)))
void __ownership_pthread_mutex_locked(void *);
__attribute__((ownership_drops_token(pthread_mutex_locked, 1),
	ownership_adds_token(pthread_mutex_unlocked, 1)))
void __ownership_pthread_mutex_unlocked(void *);
__attribute__((ownership_drops_token(pthread_mutex_unlocked, 1)))
void __ownership_pthread_mutex_destroyed(void *);

__attribute__((ownership_adds_token(pthread_spin_unlocked, 1)))
void __ownership_pthread_spin_initialized(void *);
__attribute__((ownership_drops_token(pthread_spin_unlocked, 1),
	ownership_adds_duplicable_token(pthread_spin_locked, 1)))
void __ownership_pthread_spin_locked(void *);
__attribute__((ownership_drops_token(pthread_spin_locked, 1),
	ownership_adds_token(pthread_spin_unlocked, 1)))
void __ownership_pthread_spin_unlocked(void *);
__attribute__((ownership_drops_token(pthread_spin_unlocked, 1)))
void __ownership_pthread_spin_destroyed(void *);

#else

#define __ownership_pthread_mutex_initialized(object) ((void)0)
#define __ownership_pthread_mutex_locked(object) ((void)0)
#define __ownership_pthread_mutex_unlocked(object) ((void)0)
#define __ownership_pthread_mutex_destroyed(object) ((void)0)
#define __ownership_pthread_spin_initialized(object) ((void)0)
#define __ownership_pthread_spin_locked(object) ((void)0)
#define __ownership_pthread_spin_unlocked(object) ((void)0)
#define __ownership_pthread_spin_destroyed(object) ((void)0)

#endif
#endif
