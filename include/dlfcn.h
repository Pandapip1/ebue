/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <dlfcn.h> -- POSIX dynamic loading, implemented in src/dlfcn/dlfcn.c
 * over src/internal/rpath.c's LdrLoadDll()/LdrGetProcedureAddress()/
 * LdrUnloadDll() wrappers.
 *
 * NT's loader always resolves every relocation before returning and always
 * makes a module's exports globally reachable, so RTLD_NOW/RTLD_GLOBAL are
 * simply how it behaves, RTLD_LAZY is a no-op honoured as RTLD_NOW, and
 * RTLD_LOCAL is accepted as a bit but never actually isolates anything
 * (no per-handle symbol-scoping primitive exists to do so).
 *
 * RTLD_DEFAULT/RTLD_NEXT are deliberately not defined: LdrGetProcedureAddress()
 * only answers "does *this* module export this name", with no NT mechanism
 * for the ordered-search-list semantics either would need.
 */
#ifndef _DLFCN_H
#define _DLFCN_H

#include <features.h>
#include <string_tokens.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_GLOBAL 4
#define RTLD_LOCAL  8

void *dlopen(const char *, int);
void *dlsym(void *__restrict, const char *__restrict withtok(null_terminated));
int   dlclose(void *);
char *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
