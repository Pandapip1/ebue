/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/utsname.h>: uname() fills every field from something this
 * backend genuinely knows -- on NT, RtlGetVersion() for sysname/release/
 * version, a registry lookup (falling back to gethostname()) for
 * nodename, and a compile-time arch check for machine; on Linux, a
 * single real uname(2) syscall answers all of them directly. See
 * src/misc/uname.c's header comment and src/misc/{nt,linux}/plat_misc.c's
 * own __plat_uname() for exactly what each field reports and why. */
#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* utsname.h.html gives no required size for these members; 256 is
 * comfortably above anything any field here can actually produce (the
 * longest -- nodename, via gethostname() -- is itself capped at
 * HOST_NAME_MAX+1 == 256, include/limits.h). */
struct utsname {
	char sysname[256];
	char nodename[256];
	char release[256];
	char version[256];
	char machine[256];
};

int uname(struct utsname *);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
