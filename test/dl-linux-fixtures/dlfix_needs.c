/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The dependent half of test/posix-dl-linux.c's DT_NEEDED chain: this
 * .so calls dep_answer() (dlfix_dep.c) without defining it itself, so a
 * plain, unresolved-at-this-file's-own-link-time call turns into a real
 * DT_NEEDED entry naming dlfix_dep.so once the Makefile's own build
 * rule links this against obj/test/dlfix_dep.so directly (see that
 * rule's own comment). Proves src/dlfcn/linux/plat_dlfcn.c's
 * load_object() actually chases DT_NEEDED and loads the dependency
 * itself, rather than requiring the TEST to dlopen() both files by
 * hand: needs_answer() only works at all if the loader did that.
 */
extern int dep_answer(void);
int needs_answer(void) { return dep_answer() + 1000; }
