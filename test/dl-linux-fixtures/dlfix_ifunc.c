/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A GNU indirect-function ("ifunc") fixture for test/posix-dl-linux.c's
 * R_AARCH64_IRELATIVE/R_X86_64_IRELATIVE coverage -- see src/dlfcn/
 * linux/plat_dlfcn.c's own R_AARCH64_IRELATIVE #define comment for the
 * relocation shape this exercises.
 *
 * `visibility("hidden")` on the ifunc declaration is deliberate, not
 * decorative: confirmed empirically (see that same #define comment)
 * that it is exactly what makes this dev host's own clang emit an
 * R_AARCH64_IRELATIVE relocation (addend = the RESOLVER's own vaddr,
 * no symbol at all) rather than an R_AARCH64_JUMP_SLOT one naming an
 * STT_GNUIFUNC-typed dynamic symbol (a different, NOT-yet-handled
 * shape this fixture is not exercising -- dropping `hidden` here would
 * silently switch which gap this fixture actually proves closed).
 *
 * pick_target() deliberately selects target_double(), NOT some default
 * "first" implementation -- so that a loader bug which stores the
 * RESOLVER's own address at the relocated slot (instead of calling it
 * and storing its return value) is caught for real: calling that
 * broken slot would invoke pick_target() itself with call_ifunc()'s
 * own `int x` argument, a signature pick_target() (a zero-argument
 * function) was never compiled to expect -- garbage or a crash, not
 * silently "close enough to right".
 */
static int target_double(int x) { return x * 2; }

static void *pick_target(void) { return (void *)target_double; }

__attribute__((ifunc("pick_target"), visibility("hidden")))
int ifunc_compute(int x);

int call_ifunc(int x) { return ifunc_compute(x); }
