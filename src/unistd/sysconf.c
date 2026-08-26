/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include "libc.h"

long sysconf(int name)
{
	switch (name) {
	case _SC_ARG_MAX: return ARG_MAX;
	case _SC_CHILD_MAX: return CHILD_CAP_LIMIT_;
	case _SC_CLK_TCK: return 100;
	case _SC_NGROUPS_MAX: return NGROUPS_MAX;
	case _SC_OPEN_MAX: return FD_MAX;
	case _SC_STREAM_MAX: return FD_MAX;
	case _SC_TZNAME_MAX: return TZNAME_MAX;
	case _SC_JOB_CONTROL: return -1;
	case _SC_SAVED_IDS: return -1;
	case _SC_VERSION: return _POSIX_VERSION;
	case _SC_MONOTONIC_CLOCK: return _POSIX_MONOTONIC_CLOCK;
	case _SC_PAGESIZE: return 4096;
	case _SC_LINE_MAX: return 4096;
	case _SC_HOST_NAME_MAX: return HOST_NAME_MAX;
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN: {
		SYSTEM_BASIC_INFORMATION sbi;
		if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof sbi, 0)))
			return sbi.NumberOfProcessors;
		return 1;
	}
	case _SC_PHYS_PAGES: {
		SYSTEM_BASIC_INFORMATION sbi;
		if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof sbi, 0)))
			return (long)((unsigned long long)sbi.NumberOfPhysicalPages * sbi.PageSize / 4096);
		return -1;
	}
	default:
		errno = EINVAL;
		return -1;
	}
}

long pathconf(const char *path, int name)
{
	(void)path;
	switch (name) {
	case _PC_LINK_MAX: return 1023;
	case _PC_MAX_CANON: return 255;
	case _PC_MAX_INPUT: return 255;
	case _PC_NAME_MAX: return NAME_MAX;
	case _PC_PATH_MAX: return PATH_MAX;
	case _PC_PIPE_BUF: return PIPE_BUF;
	case _PC_CHOWN_RESTRICTED: return 1;
	case _PC_NO_TRUNC: return 1;
	case _PC_VDISABLE: return 0;
	default: errno = EINVAL; return -1;
	}
}

long fpathconf(int fd, int name) { (void)fd; return pathconf("", name); }
int getpagesize(void) { return 4096; }
int getdtablesize(void) { return FD_MAX; }
/* confstr.html RETURN VALUE: an invalid name is 0 with [EINVAL], not the
 * 1 that a lone terminating null would account for and not (size_t)-1.
 * The name set is closed here rather than defaulted, which is what makes
 * that reachable: <unistd.h> defines exactly one _CS_* constant, so
 * every name but _CS_PATH is invalid, and a name added to the header has
 * to gain a case below with it.
 *
 * POSIX's other zero -- a valid name with no configuration-defined
 * value, which returns 0 with errno UNCHANGED -- has no name to reach it
 * in this tree.  A case wanting it cannot just set s to "": the tail
 * below counts the null it writes and returns 1.
 */
size_t confstr(int name, char *buf, size_t len)
{
	const char *s;
	size_t i;

	switch (name) {
	case _CS_PATH: s = "/bin:/usr/bin"; break;
	default: errno = EINVAL; return 0;
	}
	for (i = 0; s[i] && i + 1 < len; i++) buf[i] = s[i];
	if (len) buf[i] = 0;
	while (s[i]) i++;
	return i + 1;
}
