/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * $ORIGIN-relative DLL search: the half of the RPATH-equivalent mechanism
 * that decides *where* a delay-loaded DLL comes from. Two things call
 * into this file the first time a delay-imported function is used:
 * __delayLoadHelper2 (crt/delayload2.c), the documented interface -- a
 * plain `extern` declaration, an ordinary call, built with
 * -Wl,--delay-all, no macro at the call site -- and
 * include/ntlibc/delayload.h's hand-authored, macro-based
 * NTLIBC_DELAY_DLL/_STUB machinery, kept for a tcc without that flag
 * (see that header's own note on why it is no longer what new code
 * should reach for). __rpath, below, is the one thing a program using
 * either mechanism still declares itself.
 *
 * ---- Where the search path lives -----------------------------------
 *
 * The executable defines the well-known symbol __rpath: a NULL-terminated
 * array of search directories, e.g.
 *
 *   const char *const __rpath[] = { "plugins", "../lib", 0 };
 *
 * This is a plain extern reference from ntlibc_rpath_load(), the same
 * way __progname is a plain global defined by crt1.c and read by
 * whoever wants it -- no linker section, no custom PE directory, and
 * nothing for a bare `tcc prog.c -lc` invocation to know about beyond
 * "define this array if you use this API". Only a translation unit that
 * calls ntlibc_rpath_load() pulls in __rpath as an undefined reference;
 * a program that never delay-loads anything never mentions it, so
 * nothing is required of, or added to, a normal build or to crt1.c's
 * startup path.
 *
 * ---- Search order and $ORIGIN ----------------------------------------
 *
 * ntlibc_rpath_load(dllname):
 *
 *   1. If dllname itself contains a path component ('/' or '\\', or a
 *      drive letter): an absolute one is used as-is; a relative one is
 *      resolved against the image's own directory ($ORIGIN) -- *never*
 *      against the current working directory -- and __rpath is not
 *      consulted at all.
 *   2. Otherwise, __rpath's entries are tried in array order. A relative
 *      entry is resolved against the image directory ($ORIGIN); an
 *      absolute entry (leading '/' or '\\', or a drive letter) is used
 *      as-is. For each entry a full path
 *      "<resolved-entry>\<dllname>" is built and handed to LdrLoadDll
 *      *as a fully-qualified path*. A fully-qualified DllName makes the
 *      NT loader load exactly that file with no further search -- this
 *      is what keeps the current working directory, PATH, and the
 *      system directories out of the picture entirely (see "Threat
 *      model" below).
 *   3. If no entry yields the DLL, ntlibc_rpath_load() fails. There is
 *      no implicit fallback to the ordinary system search order: this
 *      API is opt-in and its whole point is a search path the image
 *      controls, so silently widening it back out to "wherever Windows
 *      would normally look" -- which includes the CWD -- would defeat
 *      that. A program that also wants the ordinary system search order
 *      tried can add an absolute entry for it explicitly.
 *
 * A failed resolution -- DLL not found, or (from ntlibc_rpath_sym())
 * symbol not found -- is always reported through ntlibc_rpath_error()
 * and, when reached through the delay-load stubs in delayload.h, is
 * never allowed to fall through into a jump through an unresolved
 * pointer: ntlibc_rpath_fail() prints the diagnosis and aborts. (This
 * project has lost real debugging time to exactly a jump through an
 * unexpectedly-null resolved pointer -- see tools/asan-build.sh's
 * symbol-preemption note -- so this path is deliberately not "fail
 * open".)
 *
 * ---- Threat model -------------------------------------------------
 *
 * $ORIGIN-relative loading is a classic DLL-hijacking vector: anything
 * that lets an attacker-writable directory sit ahead of the real DLL in
 * the search order lets them substitute their own. This implementation:
 *
 *   - never consults the current working directory, at any step;
 *   - never calls LdrLoadDll with a bare (unqualified) name -- every
 *     candidate handed to the loader is already a fully-qualified path
 *     that this code built itself, so the loader's own search order
 *     (which does include the CWD on plenty of Windows configurations)
 *     is never invoked by this API;
 *   - resolves every relative __rpath entry, and every relative
 *     dllname-with-a-path, against the image's own directory, which is
 *     no more attacker-controlled than the executable itself is;
 *   - does not follow symlinks/junctions specially or expand
 *     environment references in __rpath entries -- an entry is either a
 *     literal absolute path or a literal path fragment joined onto the
 *     image directory, nothing more.
 *
 * An absolute __rpath entry naming a world-writable directory is still
 * the caller's problem, the same way an absolute DT_RPATH entry is on
 * ELF: this API narrows the search to what the image asked for, it
 * cannot make an intentionally-listed directory safe.
 */
#ifndef NTLIBC_RPATH_H
#define NTLIBC_RPATH_H

/* For _Noreturn (used below): gcc/clang accept the bare keyword as an
 * extension in C regardless of -std=, but C++ does not, so without this
 * include a solo `#include <ntlibc/rpath.h>` in a C++ TU fails with
 * "'_Noreturn' does not name a type" -- found by tools/hdr-hygiene.sh's
 * cxx stage, which is exactly the case this header's own extern "C"
 * block promises to support. */
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

/* Symmetrical with ntlibc_rpath_load(): releases one reference on `dll`
 * (LdrUnloadDll's own LoadCount, not tracked separately here -- see
 * src/internal/rpath.c's comment on ntlibc_rpath_unload() for why
 * nothing more is needed), unloading it once the count reaches zero.
 * Returns 0 on success; on failure returns nonzero and
 * ntlibc_rpath_error() describes why. */
int ntlibc_rpath_unload(ntlibc_dll_t *dll);

/* Describes the most recent failure from ntlibc_rpath_load()/
 * ntlibc_rpath_sym()/ntlibc_rpath_unload() on this process. Valid only
 * right after such a call returned failure; there is no thread-local
 * version because ntlibc has no thread support to make one meaningful.
 * Deliberately sticky across repeated calls -- see this function's own
 * implementation comment -- unlike POSIX dlerror()'s single-shot
 * contract; src/dlfcn/dlfcn.c's dlerror() layers that contract on top
 * using ntlibc_rpath_error_seq() below rather than changing this
 * function's own behaviour, which callers other than dlerror() already
 * rely on. */
const char *ntlibc_rpath_error(void);

/* Monotonic counter, bumped once per failure recorded by
 * ntlibc_rpath_load()/_sym()/_unload(), 0 until the first ever failure.
 * Lets a caller distinguish "the same failure I already saw" from "a
 * new failure happened since" without ntlibc_rpath_error() itself
 * needing to stop being sticky. Only meaningful use today is
 * src/dlfcn/dlfcn.c's dlerror(). */
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
