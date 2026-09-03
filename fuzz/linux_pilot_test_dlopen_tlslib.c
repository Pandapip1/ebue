/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A second, deliberately TLS-bearing .so tools/linux-build-dlfcn.sh
 * dlopen()s to prove src/dlfcn/linux/plat_dlfcn.c's real per-object TLS
 * actually works on this pass's target (aarch64) -- see that file's
 * "TLS / per-library thread descriptors" banner: a PT_TLS segment gets
 * its own module id and mini-TCB (setup_object_tls()), not a clean
 * refusal. This __thread variable is what gives the built .so a PT_TLS
 * segment at all; bump_tls_counter() is called from
 * fuzz/linux_pilot_test_dlopen.c to prove the resulting TLSDESC access
 * actually reads/writes it, and that two independent dlopen()s of this
 * same .so get two genuinely separate counters.
 */
static __thread int tls_counter;
int bump_tls_counter(void) { return ++tls_counter; }
