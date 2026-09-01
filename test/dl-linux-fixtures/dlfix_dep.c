/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The leaf of test/posix-dl-linux.c's DT_NEEDED dependency-chain proof
 * (see that file's own header comment, and src/dlfcn/linux/plat_dlfcn.c's
 * "NAMESPACE ISOLATION" banner's own DT_NEEDED section). Built as an
 * ordinary Linux .so with no dependencies of its own -- the Makefile
 * target this compiles to is obj/test/dlfix_dep.so; dlfix_needs.c links
 * directly against it (see that file's own comment) so the static
 * linker emits a real DT_NEEDED entry naming it.
 */
int dep_answer(void) { return 55; }
