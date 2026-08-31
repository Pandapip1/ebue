/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <dlfcn.h> -- POSIX dynamic loading (dlopen.html/dlsym.html/
 * dlclose.html/dlerror.html). Implemented in src/dlfcn/dlfcn.c as a
 * thin, POSIX-shaped layer over src/internal/rpath.c's existing
 * LdrLoadDll()/LdrGetProcedureAddress()/LdrUnloadDll() wrappers
 * (include/ntlibc/rpath.h) -- see that implementation file's own
 * comment for what each function actually does on NT and the two
 * points that needed real thought rather than a wrapper: dlerror()'s
 * single-shot ("NULL on the second consecutive call") contract, and
 * which search order a bare (no-slash) `file` argument gets.
 *
 * Mode flags, in brief (dlopen.html DESCRIPTION; full rationale in
 * dlfcn.c):
 *
 *   RTLD_NOW    what LdrLoadDll() always does -- every relocation is
 *               resolved before it returns, unconditionally.
 *   RTLD_LAZY   accepted, but honoured identically to RTLD_NOW: ntdll
 *               has no per-import lazy-binding stub LdrLoadDll() could
 *               defer to for an ordinary (non-delay-load) import, so
 *               "lazy" is a compatibility no-op here, not a real
 *               scheduling difference. Never a behavioural gap: every
 *               symbol RTLD_LAZY promises to eventually resolve is, in
 *               fact, already resolved the moment dlopen() returns.
 *   RTLD_GLOBAL what LdrLoadDll() always does -- an NT module's
 *               exports are reachable from any handle on it,
 *               process-wide, from the moment it is mapped. Accepted
 *               for source compatibility; never changes behaviour.
 *   RTLD_LOCAL  N/A on this platform: NT's loader has no per-handle
 *               symbol-scoping primitive to narrow a module's exports
 *               back out of later lookups (see dlfcn.c). Accepted as a
 *               bit (so `RTLD_NOW | RTLD_LOCAL`, the common default
 *               combination, still compiles and loads) but never
 *               actually isolates anything.
 *
 * RTLD_DEFAULT/RTLD_NEXT (dlsym.html), reserved by POSIX for handle
 * values with special search-order meaning "for future use", are
 * deliberately not defined here: neither has an NT loader mechanism
 * behind it (both presuppose an ordered list of already-loaded objects
 * dlsym() searches instead of a specific handle's own export table --
 * LdrGetProcedureAddress() only ever answers "does *this* module
 * export this name", nothing broader), and a program that never sees
 * them defined cannot accidentally rely on either.
 */
#ifndef _DLFCN_H
#define _DLFCN_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_GLOBAL 4
#define RTLD_LOCAL  8

void *dlopen(const char *, int);
void *dlsym(void *__restrict, const char *__restrict);
int   dlclose(void *);
char *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
