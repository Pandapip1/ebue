/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * $ORIGIN-relative DLL search: decides *where* a delay-loaded DLL comes
 * from. Called into by __delayLoadHelper2 (crt/delayload2.c, the
 * -Wl,--delay-all path) and by delayload.h's macro-based
 * NTLIBC_DELAY_DLL/_STUB machinery (kept for tcc without that flag).
 * __rpath, below, is the one thing a program using either mechanism
 * still declares itself: a plain extern array,
 * `const char *const __rpath[] = { "plugins", "../lib", 0 };`, pulled
 * in only by a translation unit that calls ntlibc_rpath_load().
 *
 * Search order for ntlibc_rpath_load(dllname):
 *   1. dllname containing a path component ('/' or '\\', or a drive
 *      letter) is used as a pathname directly; __rpath is not
 *      consulted.
 *   2. Otherwise, __rpath's entries are tried in order: relative
 *      entries resolve against the image directory ($ORIGIN), absolute
 *      entries are used as-is. Each candidate is built as a full path
 *      and handed to LdrLoadDll *as a fully-qualified path*, which
 *      makes the NT loader load exactly that file with no further
 *      search -- this is what keeps the CWD, PATH, and system
 *      directories out of the picture (see threat model below).
 *   3. If no entry yields the DLL, ntlibc_rpath_load() fails -- no
 *      implicit fallback to the ordinary system search order. A
 *      program that wants that too can add an absolute entry for it.
 *
 * A failed resolution is always reported through ntlibc_rpath_error();
 * when reached through delayload.h's stubs, ntlibc_rpath_fail() prints
 * the diagnosis and aborts rather than falling through to a jump
 * through an unresolved pointer.
 *
 * Threat model: $ORIGIN-relative loading is a classic DLL-hijacking
 * vector. This implementation consults the CWD only when the caller
 * explicitly supplies a relative pathname containing a separator;
 * never calls LdrLoadDll with a bare (unqualified) name, so the
 * loader's own CWD-inclusive search order is never invoked; resolves
 * relative __rpath entries only against the image's own directory; and
 * does not follow symlinks/junctions specially or expand environment
 * references in __rpath entries. An absolute __rpath entry naming a
 * world-writable directory is still the caller's problem, the same way
 * an absolute DT_RPATH entry is on ELF.
 */
#ifndef NTLIBC_RPATH_H
#define NTLIBC_RPATH_H

/* For _Noreturn (used below): a bare C++ TU does not accept the keyword
 * the way gcc/clang do in C regardless of -std=. */
#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle for a DLL loaded through this API -- in practice the
 * same module base LdrLoadDll/LdrGetProcedureAddress already use. */
typedef void ntlibc_dll_t;

/* Defined by the program, not by ntlibc: a NULL-terminated array of
 * search directories, each either absolute or relative to the image's
 * own directory. Only referenced by ntlibc_rpath_load(), so a program
 * that never delay-loads anything never needs to define it. */
extern const char *const __rpath[];

/* Resolve and load `dllname` per the search order documented above.
 * Returns a handle on success; on failure returns NULL and
 * ntlibc_rpath_error() describes why. */
ntlibc_dll_t *ntlibc_rpath_load(const char *dllname);

/* Resolve `symbol` in a handle from ntlibc_rpath_load(). Returns NULL
 * and sets the diagnosable error on failure. */
void *ntlibc_rpath_sym(ntlibc_dll_t *dll, const char *symbol);

/* Symmetrical with ntlibc_rpath_load(): releases one reference on
 * `dll`, unloading it once the count reaches zero. Returns 0 on
 * success; on failure returns nonzero and ntlibc_rpath_error()
 * describes why. */
int ntlibc_rpath_unload(ntlibc_dll_t *dll);

/* Describes the most recent failure from ntlibc_rpath_load()/
 * ntlibc_rpath_sym()/ntlibc_rpath_unload() on this process. Valid only
 * right after such a call returned failure. Deliberately sticky across
 * repeated calls, unlike POSIX dlerror()'s single-shot contract;
 * src/dlfcn/dlfcn.c's dlerror() layers that contract on top using
 * ntlibc_rpath_error_seq() below instead. */
const char *ntlibc_rpath_error(void);

/* Monotonic counter, bumped once per failure recorded by
 * ntlibc_rpath_load()/_sym()/_unload(), 0 until the first ever
 * failure. Lets a caller distinguish "the same failure I already saw"
 * from "a new failure happened since" without ntlibc_rpath_error()
 * itself needing to stop being sticky. */
unsigned long ntlibc_rpath_error_seq(void);

/* Prints the diagnosis for a failed resolution of dllfile!symbol to
 * stderr and aborts the process. Used by delayload.h's generated stubs
 * so a failed delay load can never fall through to calling a null
 * pointer. Does not return. */
_Noreturn void ntlibc_rpath_fail(const char *dllfile, const char *symbol);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
