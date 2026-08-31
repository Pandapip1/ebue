/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A second, deliberately TLS-bearing .so tools/linux-build-dlfcn.sh
 * dlopen()s to prove src/dlfcn/linux/plat_dlfcn.c's documented PT_TLS
 * refusal actually fires (see that file's "TLS / per-library thread
 * descriptors" banner: a PT_TLS segment is a clean, disclosed dlopen()
 * failure in this pass, not a silent mis-load). This __thread variable
 * is what gives the built .so a PT_TLS segment at all -- nothing here
 * needs to actually be usable, since dlopen() of this file is expected
 * to fail before any relocation or TLS setup is attempted.
 */
static __thread int tls_counter;
int bump_tls_counter(void) { return ++tls_counter; }
