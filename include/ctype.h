#ifndef	_CTYPE_H
#define	_CTYPE_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

int   isalnum(int);
int   isalpha(int);
int   isblank(int);
int   iscntrl(int);
int   isdigit(int);
int   isgraph(int);
int   islower(int);
int   isprint(int);
int   ispunct(int);
int   isspace(int);
int   isupper(int);
int   isxdigit(int);
int   tolower(int);
int   toupper(int);

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
int   isascii(int);
int   toascii(int);
#define _tolower(a) ((a)|0x20)
#define _toupper(a) ((a)&0x5f)
#endif

#ifdef __cplusplus
}
#endif

#endif
