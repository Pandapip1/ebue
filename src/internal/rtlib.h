/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Declarations for the functions the toolchain calls but C code never
 * does: the helpers the code generator emits calls to, and the image
 * entry point the loader jumps to.
 *
 * Nothing in the tree calls any of these by name, so without this header
 * each is a definition with no prior declaration -- which is what
 * -Wmissing-prototypes reports, but the warning is not the point.  The
 * point is that such a definition has nothing to be type-checked
 * against, so a wrong signature (a `long` where the code generator will
 * pass a `long long`, a signed return on an unsigned helper) is not a
 * compile error but a miscompile at every call site the code generator
 * emitted -- call sites that appear in no source file, so nothing else
 * would ever catch it.  Declaring them here restores the check.
 *
 * These names are deliberately not in any public header: a program that
 * calls __divdi3 or _start by hand is doing something wrong.
 */
#ifndef _NTLIBC_RTLIB_H
#define _NTLIBC_RTLIB_H

/* ---- 64-bit integer helpers (arch/i386/src/int64.c) -------------------- */
/* tcc's i386 code generator calls these for /, % and variable-count
 * shifts on long long.  x86_64 has the instructions and needs none of
 * them, so that arch defines none of these; the declarations are
 * harmless there.  Signatures match libgcc's. */
unsigned long long __udivdi3(unsigned long long, unsigned long long);
unsigned long long __umoddi3(unsigned long long, unsigned long long);
long long __divdi3(long long, long long);
long long __moddi3(long long, long long);
long long __ashldi3(long long, int);
unsigned long long __lshrdi3(unsigned long long, int);
long long __ashrdi3(long long, int);

/* ---- float <-> 64-bit integer helpers (arch/<arch>/src/fpconv.c) ------- */
/* Emitted for every float-to-long-long conversion on i386, and for the
 * unsigned long long cases on both arches. */
unsigned long long __fixunssfdi(float);
unsigned long long __fixunsdfdi(double);
unsigned long long __fixunsxfdi(long double);
long long __fixsfdi(float);
long long __fixdfdi(double);
long long __fixxfdi(long double);
double __floatundidf(unsigned long long);
float __floatundisf(unsigned long long);
long double __floatundixf(unsigned long long);

/* ---- binary128 <-> binary64/binary32 helpers (arch/aarch64/src/
 * ld128_convert.c) ---------------------------------------------------- */
/* Emitted for every `long double` <-> `double`/`float` conversion on
 * aarch64, where `long double` is real IEEE 754 binary128 ("quad") and
 * the CPU has no hardware for it at all -- see that file's own banner
 * for why this build needs its own, real implementation rather than
 * linking libgcc/compiler-rt (it links neither; this build is
 * -nostdlib). Signatures match libgcc's/compiler-rt's own. */
long double __extenddftf2(double);
double __trunctfdf2(long double);
float __trunctfsf2(long double);

/* ---- program entry (crt/crt1.c) ---------------------------------------- */
/* tcc's PE linker picks _start up as the image entry point by name.
 * Neither of these is ever called from C.
 *
 * __libc_start_main takes nothing: the PEB is read from the TEB, because
 * the entry point of a Windows-subsystem image is not reliably passed one
 * (see crt1.c's __libc_start_main for the full reasoning and citations).
 *
 * _start's two parameters are NOT arguments anyone promises to pass.  They
 * are the entry-argument measurement and its control, captured into
 * __entry_arg0/__entry_arg1 and never used for anything else -- in
 * particular never as the source of __peb, which is the whole point.
 * Reading them is safe under both calling conventions ntlibc targets (a
 * register on x86_64, a caller-owned stack slot on i386, and _start never
 * returns).  See crt/crt1.c and test/entry-arg.c. */
void __libc_start_main(void);
void _start(void *arg0, void *arg1);

/* ---- delay-load helper (crt/delayload2.c) ------------------------------ */
/* Looked up by this exact name and called by the linker-generated
 * per-DLL tail-merge stub a -Wl,--delay-all build emits (tinycc's
 * pe_emit_delay_tailmerge(), tccpe.c) -- never by name from any C
 * source file, the same as _start/__libc_start_main above. */
void *__delayLoadHelper2(void *descr, void **piat);

#endif
