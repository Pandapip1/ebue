/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _GETOPT_H
#define _GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

/* argv/optstring both required: src/misc/getopt.c own body
 * dereferences argv[optind] unconditionally past the optind >= argc
 * bound check (which says nothing about argv itself), and optstring[0]
 * unconditionally on the multi-character-short-option path (past the
 * early return for a bare dash argument) -- no real caller in this
 * tree (~19 getopt(), ~40 getopt_long() call sites) ever passes NULL
 * for either. */
int getopt(int, char * const [], const char *) __attribute__((nonnull(2, 3)));
extern char *optarg;
extern int optind, opterr, optopt, optreset;

struct option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};

/* argv/optstring required, same evidence as getopt() above: both
 * flow, unguarded, into the static __getopt_long()/__getopt_long_core()
 * in src/misc/getopt_long.c, which either dereference them directly
 * (argv[optind] and optstring[0] at the top of __getopt_long(),
 * argv[optind][0] on the longopts-taken path of __getopt_long_core())
 * or forward them into the now-required getopt(). longopts is
 * deliberately NOT marked: the longopts-non-null test in
 * __getopt_long_core() is a real, live guard, not decoration --
 * getopt_long_only() own single-character-name fallback runs with
 * longopts present, but the underlying short-option grammar getopt()
 * implements has never required one. idx is likewise left unmarked,
 * guarded by its own non-null test before every write. */
int getopt_long(int, char *const *, const char *, const struct option *, int *)
    __attribute__((nonnull(2, 3)));
int getopt_long_only(int, char *const *, const char *, const struct option *, int *)
    __attribute__((nonnull(2, 3)));

#define no_argument        0
#define required_argument  1
#define optional_argument  2

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
