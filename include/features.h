/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _FEATURES_H
#define _FEATURES_H

#if defined(_ALL_SOURCE) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#if defined(_DEFAULT_SOURCE) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif

#if !defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) \
 && !defined(_XOPEN_SOURCE) && !defined(_GNU_SOURCE) \
 && !defined(_BSD_SOURCE) && !defined(__STRICT_ANSI__)
#define _BSD_SOURCE 1
#define _XOPEN_SOURCE 700
#endif

#if __STDC_VERSION__ >= 199901L
#define __restrict restrict
#elif !defined(__GNUC__)
#define __restrict
#endif

#if __STDC_VERSION__ >= 199901L || defined(__cplusplus)
#define __inline inline
#elif !defined(__GNUC__)
#define __inline
#endif

#if __STDC_VERSION__ >= 201112L
#elif defined(__GNUC__)
#define _Noreturn __attribute__((__noreturn__))
#else
#define _Noreturn
#endif

/* __wraps -- "the wraparound in this function is on purpose".
 *
 * Unsigned overflow is not undefined behaviour; it is modular arithmetic
 * (C99 6.2.5p9), which is why -fsanitize=undefined does not check it and
 * -fsanitize=integer does.  Marking the functions that wrap deliberately
 * is what makes an accidental wrap visible, so the annotation is the
 * point of the check, not an escape from it.
 *
 * Only clang has the attribute *and* the check, so only clang gets it.
 * tcc parses __attribute__ and silently ignores contents it does not
 * know, and its __has_attribute() answers 0 for no_sanitize, so either
 * guard alone would do; gcc knows no_sanitize but not these sanitizer
 * names, and warns under -Wattributes (which tools/lint.sh turns on), so
 * the __clang__ test is the one that earns its keep.
 *
 * Internal to the library: programs including <ctype.h> never see it.
 *
 * __has_attribute gets a fallback definition rather than a bare
 * `defined(__has_attribute)` guard: a compiler old enough to lack the
 * builtin cannot be asked whether it has it either, so the fallback
 * just answers 0.  This also keeps cppcheck's --force preprocessor
 * (which explores every #ifdef configuration, including one with
 * __clang__ defined) from hitting __has_attribute(...) as a directive
 * it cannot evaluate at all. */
#ifdef _NTLIBC_INTERNAL
#ifndef __has_attribute
#define __has_attribute(x) 0
#endif
#if defined(__clang__) && __has_attribute(no_sanitize)
#define __wraps __attribute__((no_sanitize("unsigned-integer-overflow", \
                                           "unsigned-shift-base")))
#endif
#ifndef __wraps
#define __wraps
#endif
#endif

/* Allocation-lifetime contracts.  Like __wraps above, these are present only
 * in the one analysis that consumes them; installed headers under every real
 * compiler configuration retain their ordinary declarations and ABI.  The
 * family token pairs producers with their unique ownership_takes freer.
 * __NTLIBC_REALLOCATES adds the conditional input transition which Clang's
 * standard ownership attributes cannot express by themselves. */
#if defined(__clang__) && defined(NTLIBC_ALLOCATION_LIFETIME_ANALYSIS)
#define __NTLIBC_RETURNS_OWNERSHIP(family) \
	__attribute__((ownership_returns(family)))
#define __NTLIBC_TAKES_OWNERSHIP(family, ...) \
	__attribute__((ownership_takes(family, __VA_ARGS__)))
#define __NTLIBC_REALLOCATES(family, argument) \
	__NTLIBC_RETURNS_OWNERSHIP(family) \
	__attribute__((annotate("ntlibc.reallocates:" #argument)))
#else
#define __NTLIBC_RETURNS_OWNERSHIP(family)
#define __NTLIBC_TAKES_OWNERSHIP(family, ...)
#define __NTLIBC_REALLOCATES(family, argument)
#endif

#define __REDIR(x,y) __typeof__(x) x __asm__(#y) // NOLINT(bugprone-macro-parentheses) -- x is the redirected declaration's identifier, where an expression-style wrapper is not applicable

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
