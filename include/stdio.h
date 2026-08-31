/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _STDIO_H
#define _STDIO_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif

#define __NEED_FILE
#define __NEED___isoc_va_list
#define __NEED_size_t

#if __STDC_VERSION__ < 201112L
#define __NEED_struct__IO_FILE
#endif

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define __NEED_ssize_t
#define __NEED_off_t
#define __NEED_va_list
#endif

#include <bits/alltypes.h>

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define BUFSIZ 4096
#define FILENAME_MAX 4096
#define FOPEN_MAX 1000
#define TMP_MAX 10000
#define L_tmpnam 20

typedef union _G_fpos64_t {
	char __opaque[16];
	long long __lldata;
	double __align;
} fpos_t;

extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;

#define stdin  (stdin)
#define stdout (stdout)
#define stderr (stderr)

FILE *fopen(const char *__restrict, const char *__restrict);
/* freopen's own stream is required: src/stdio/file.c's freopen() reads
 * `oldfd = f->fd;` right after its own `fflush(f)` (which, like plain
 * fflush() below, tolerates a null stream on its own), unconditionally,
 * with no check of f's own nullness anywhere in the function -- unlike
 * fopen()'s path/mode, which this project's own freopen() only ever
 * forwards to __fmodeflags()/open(), never dereferencing here itself.
 * Every real call site in this tree (test/posix-wchar.c, test/posix-
 * stdio.c, test/posix-unreferenced.c, test/stdio.c) already guards its
 * own `f` with `if (f)`/`if (!freopen(...))`-style checks before ever
 * reaching this call, matching the contract. */
FILE *freopen(const char *__restrict, const char *__restrict, FILE *__restrict) __attribute__((nonnull(3)));
/* Every stdio function below that takes a FILE * dereferences it
 * unconditionally in its own body (src/stdio/file.c, buf.c, seek.c,
 * rw.c, wide.c) with no defensive check -- POSIX documents the
 * behaviour of each as undefined on a stream that does not designate
 * an open file, the same "not the callee's job to validate" contract
 * as dirent's DIR * family (see include/dirent.h). fflush() is the one
 * deliberate exception: `if (!f) { ...flush every open stream...}` in
 * src/stdio/buf.c is fflush(NULL)'s own POSIX-documented meaning, not
 * an omitted check, so its f is left unmarked. */
int fclose(FILE *) __attribute__((nonnull(1)));

int remove(const char *);
int rename(const char *, const char *);

int feof(FILE *) __attribute__((nonnull(1)));
int ferror(FILE *) __attribute__((nonnull(1)));
int fflush(FILE *);
void clearerr(FILE *) __attribute__((nonnull(1)));

/* fseeko/ftello below (src/stdio/seek.c) are both flagged directly;
 * fseek/ftell/rewind/fgetpos forward straight to fseeko/ftello with no
 * check of their own, inheriting the same requirement -- true and
 * cheap to state even though this tree's own sweep did not separately
 * flag each forwarder. */
int fseek(FILE *, long, int) __attribute__((nonnull(1)));
long ftell(FILE *) __attribute__((nonnull(1)));
void rewind(FILE *) __attribute__((nonnull(1)));

int fgetpos(FILE *__restrict, fpos_t *__restrict) __attribute__((nonnull(1, 2)));
/* fsetpos's pos is required (`pos->__lldata` dereferenced unconditionally,
 * its only use); f is required the same way, forwarded straight into
 * fseeko(f, ...) with no check of its own. */
int fsetpos(FILE *, const fpos_t *) __attribute__((nonnull(1, 2)));

size_t fread(void *__restrict, size_t, size_t, FILE *__restrict);
size_t fwrite(const void *__restrict, size_t, size_t, FILE *__restrict);

int fgetc(FILE *) __attribute__((nonnull(1)));
int getc(FILE *) __attribute__((nonnull(1)));
int getchar(void);
/* ungetc's f is dereferenced via `!f->readable`, a content check, not
 * a check of f's own nullness. */
int ungetc(int, FILE *) __attribute__((nonnull(2)));

int fputc(int, FILE *) __attribute__((nonnull(2)));
int putc(int, FILE *) __attribute__((nonnull(2)));
int putchar(int);

/* fgets: `if (n <= 0) return 0;` is the same size-style escape as
 * mem*'s own n == 0 convention (a real, callable "n <= 0 means no
 * buffer to fill" is not documented for fgets -- it is an incidental
 * early return, not a null convention on s or f), and s is written
 * through unconditionally (`*p = 0;`) on every path that is not that
 * escape; f is dereferenced directly (`!f->readable`). Matches glibc's
 * real fgets nonnull(1, 3). */
char *fgets(char *__restrict, int, FILE *__restrict) __attribute__((nonnull(1, 3)));
#if __STDC_VERSION__ < 201112L
char *gets(char *);
#endif

/* fputs: s is dereferenced by strlen(s), unconditionally, first
 * statement; f is required the same way __fwrite() (rw.c) needs it. */
int fputs(const char *__restrict, FILE *__restrict) __attribute__((nonnull(1, 2)));
int puts(const char *) __attribute__((nonnull(1)));

/* fmt is required everywhere below (src/stdio/printf.c's own
 * formatter loop dereferences it unconditionally, whichever entry
 * point reaches it); f is required wherever it appears, the same
 * required FILE * as the rest of stdio.h. sprintf/vsprintf's s is
 * required too -- unlike snprintf/vsnprintf below, there is no bound
 * that could legitimately make it optional (sprintf always writes an
 * unbounded amount, `vxprintf_mem(s, (size_t)-1, ...)`).
 * snprintf/vsnprintf's own s is deliberately NOT marked: `snprintf(s,
 * 0, fmt, ...)` with s == NULL is POSIX-documented, real, load-bearing
 * behaviour (src/stdio/printf.c's own vxprintf_mem() comment explains
 * why), not an omitted check. */
int printf(const char *__restrict, ...) __attribute__((nonnull(1)));
int fprintf(FILE *__restrict, const char *__restrict, ...) __attribute__((nonnull(1, 2)));
int sprintf(char *__restrict, const char *__restrict, ...) __attribute__((nonnull(1, 2)));
int snprintf(char *__restrict, size_t, const char *__restrict, ...) __attribute__((nonnull(3)));

int vprintf(const char *__restrict, __isoc_va_list) __attribute__((nonnull(1)));
int vfprintf(FILE *__restrict, const char *__restrict, __isoc_va_list) __attribute__((nonnull(1, 2)));
int vsprintf(char *__restrict, const char *__restrict, __isoc_va_list) __attribute__((nonnull(1, 2)));
int vsnprintf(char *__restrict, size_t, const char *__restrict, __isoc_va_list) __attribute__((nonnull(3)));

/* fmt is required throughout (src/stdio/scanf.c's own vfscanf_st()
 * dereferences it unconditionally via its main loop's gf(fp, st), and
 * every entry point below forwards straight into it); s (the source
 * string for the s-family) is required the same way -- vsscanf_impl()
 * dereferences it directly (`strlen(s)`). f is deliberately left
 * unmarked: none of fscanf/vfscanf/vscanf's own bodies dereference it
 * directly, only forwarding it into vfscanf_st(), which itself only
 * ever touches it indirectly, through sc.f inside rd()/unrd() -- a
 * different function's own proven obligation, not this one's. */
int scanf(const char *__restrict, ...) __attribute__((nonnull(1)));
int fscanf(FILE *__restrict, const char *__restrict, ...) __attribute__((nonnull(2)));
int sscanf(const char *__restrict, const char *__restrict, ...) __attribute__((nonnull(1, 2)));
int vscanf(const char *__restrict, __isoc_va_list) __attribute__((nonnull(1)));
int vfscanf(FILE *__restrict, const char *__restrict, __isoc_va_list) __attribute__((nonnull(2)));
int vsscanf(const char *__restrict, const char *__restrict, __isoc_va_list) __attribute__((nonnull(1, 2)));

void perror(const char *);

/* setvbuf's f is required (dereferenced unconditionally after the mode
 * check); buf is genuinely optional -- src/stdio/buf.c's own `if (buf)
 * { ... }` guards every use of it, falling back to allocating one on
 * first use when buf is null, exactly setvbuf.html's own documented
 * "If buf is a null pointer, ... a buffer will be allocated"
 * convention, the same shape as strtok_r's optional s. */
int setvbuf(FILE *__restrict, char *__restrict, int, size_t) __attribute__((nonnull(1)));
void setbuf(FILE *__restrict, char *__restrict);

char *tmpnam(char *);
FILE *tmpfile(void);

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
/* fmemopen's mode is dereferenced unconditionally (`mode[0] == 'a'`,
 * unconditional after the accmode switch); buf is genuinely optional
 * (`if (!b) { b = malloc(size); ... }` -- fmemopen.html itself: "If buf
 * is a null pointer, ... size bytes ... shall be allocated"). */
FILE *fmemopen(void *__restrict, size_t, const char *__restrict) __attribute__((nonnull(3)));
FILE *open_memstream(char **, size_t *);
FILE *fdopen(int, const char *);
/* popen's mode is dereferenced unconditionally (`mode[0] == 'w'`,
 * first statement); cmd is only ever forwarded into argv[2] without
 * being dereferenced in this function's own body. */
FILE *popen(const char *, const char *) __attribute__((nonnull(2)));
int pclose(FILE *) __attribute__((nonnull(1)));
int fileno(FILE *) __attribute__((nonnull(1)));
int fseeko(FILE *, off_t, int) __attribute__((nonnull(1)));
off_t ftello(FILE *) __attribute__((nonnull(1)));
/* fmt is required the same way as the rest of the printf family above. */
int dprintf(int, const char *__restrict, ...) __attribute__((nonnull(2)));
int vdprintf(int, const char *__restrict, __isoc_va_list) __attribute__((nonnull(2)));
/* flockfile/ftrylockfile/funlockfile are all `(void)f;` no-ops (see
 * src/stdio/file.c's own comment on why: there is no threading here),
 * so f is genuinely never dereferenced -- nothing in their own bodies
 * for the attribute to describe. */
void flockfile(FILE *);
int ftrylockfile(FILE *);
void funlockfile(FILE *);
int getc_unlocked(FILE *) __attribute__((nonnull(1)));
int getchar_unlocked(void);
int putc_unlocked(int, FILE *) __attribute__((nonnull(2)));
int putchar_unlocked(int);
/* getdelim.html ERRORS: "[EINVAL] lineptr or n is a null pointer" --
 * src/stdio/rw.c's own `if (!buf || !n) { errno = EINVAL; return -1;
 * }` is a real, documented check of buf/n's OWN nullness (the same
 * shape as setenv's name check), not an omission, so they are left
 * unmarked; f is required (`!f->readable`, dereferenced unconditionally
 * once past that check). getline() forwards straight into getdelim()
 * with no check of its own, inheriting the same requirement on f (but
 * not on buf/n, for the same reason). */
ssize_t getdelim(char **__restrict, size_t *__restrict, int, FILE *__restrict) __attribute__((nonnull(4)));
ssize_t getline(char **__restrict, size_t *__restrict, FILE *__restrict) __attribute__((nonnull(3)));
int renameat(int, const char *, int, const char *);
char *ctermid(char *);
#define L_ctermid 20
#endif


#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define P_tmpdir "/tmp"
char *tempnam(const char *, const char *);
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
/* setlinebuf forwards straight into setvbuf(f, 0, _IOLBF, 0) with no
 * check of its own, inheriting that function's own requirement on f
 * (see setvbuf's own comment above; the buf argument it passes is the
 * literal 0, not something forwarded from a caller, so there is
 * nothing else to say here). */
void setlinebuf(FILE *) __attribute__((nonnull(1)));
/* asprintf/vasprintf's s (the char ** result) is required: both
 * `*s = 0;` (on vxprintf_mem()'s error return) and `*s =
 * malloc(...)` afterward are unconditional once past that check, with
 * no defensive check of s's own nullness anywhere in vasprintf's body
 * (src/stdio/printf.c). fmt is required the same way as the rest of
 * the printf family. */
int asprintf(char **, const char *, ...) __attribute__((nonnull(1, 2)));
int vasprintf(char **, const char *, __isoc_va_list) __attribute__((nonnull(1, 2)));
#endif

#if defined(_LARGEFILE64_SOURCE)
#define tmpfile64 tmpfile
#define fopen64 fopen
#define freopen64 freopen
#define fseeko64 fseeko
#define ftello64 ftello
#define fgetpos64 fgetpos
#define fsetpos64 fsetpos
#define fpos64_t fpos_t
#define off64_t off_t
#endif

#ifdef __cplusplus
}
#endif

#endif
