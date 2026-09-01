/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Two independent, unrelated proofs bundled into one small .so purely
 * to keep the fixture count down -- see test/posix-dl-linux.c's own
 * header comment for which test function exercises which half:
 *
 *   - ctor_ran/get_ctor_ran(): an __attribute__((constructor)) function
 *     that runs with no caller ever invoking it directly -- proves
 *     src/dlfcn/linux/plat_dlfcn.c's run_ctors() actually executes
 *     DT_INIT_ARRAY at dlopen() time. (DT_INIT itself is NOT separately
 *     exercised here: a -nostdlib .so built by this exact toolchain,
 *     with no crti.o/crtn.o supplying a real _init, never emits a
 *     DT_INIT entry at all -- run_ctors()'s DT_INIT half is exercised
 *     only by inspection, disclosed here rather than silently assumed
 *     covered by this fixture.)
 *
 *   - relro_fp/get_relro_fp_addr(): a `const` global whose own
 *     initializer (target_func's address) is not known until load
 *     time, so it needs a real R_AARCH64_RELATIVE relocation applied
 *     to it -- exactly the shape a real linker places in a RELRO-
 *     covered section (`.data.rel.ro`), not `.rodata` (relocations
 *     cannot target genuinely read-only-from-the-start memory) and not
 *     ordinary `.data` (nothing about this declaration is mutable at
 *     the C level). get_relro_fp_addr() hands the TEST the address of
 *     that word so it can attempt to write through it in a forked
 *     child and confirm the write faults -- see that test's own
 *     comment for why fork() rather than any other mechanism.
 */
static void target_func(void) {}
void (*const relro_fp)(void) = target_func;
void **get_relro_fp_addr(void) { return (void **)&relro_fp; }

static int ctor_ran;
__attribute__((constructor)) static void mark_ctor_ran(void) { ctor_ran = 1; }
int get_ctor_ran(void) { return ctor_ran; }
