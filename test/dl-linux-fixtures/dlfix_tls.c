/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A deliberately TLS-bearing .so for test/posix-dl-linux.c's per-object
 * TLS coverage -- see src/dlfcn/linux/plat_dlfcn.c's own "TLS /
 * per-library thread descriptors" banner. This single __thread variable
 * is what gives the built .so a PT_TLS segment at all; on aarch64 this
 * pass's own dlopen() now loads it for real (module-id allocation, the
 * DTV-indexing TLSDESC resolver -- see that banner), so bump_tls_
 * counter() is expected to actually work end to end. On any other arch
 * this pass did not extend (x86_64/i386 -- see that same banner's own
 * "not extended" note), dlopen() of this file is expected to fail
 * cleanly instead, exactly as it always has.
 */
static __thread int tls_counter;
int bump_tls_counter(void) { return ++tls_counter; }
