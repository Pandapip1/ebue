#ifndef _ERRNO_H
#define _ERRNO_H

#include <features.h>
#include <bits/errno.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int errno;
#define errno errno

#ifdef _GNU_SOURCE
extern char *program_invocation_short_name, *program_invocation_name;
#endif

#ifdef __cplusplus
}
#endif

#endif
