/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The private shape of FILE, and the helpers shared between the files in
 * src/stdio/.  Nothing here is visible outside this directory: callers
 * only ever see the incomplete struct _IO_FILE that <stdio.h> declares.
 *
 * A FILE owns one buffer that is either full of unread bytes (a "read"
 * buffer, rpos..rend valid) or holding bytes not yet written (a "write"
 * buffer, wpos pending) but never both at once; switching direction
 * flushes or un-reads as fseek would.  A file backed by memory
 * (fmemopen/open_memstream) skips this buffer entirely and is served
 * directly out of the memory block, since there is no fd to be economical
 * about calling into.
 */
#ifndef _NTLIBC_STDIO_IMPL_H
#define _NTLIBC_STDIO_IMPL_H

#include <stdio.h>
#include <sys/types.h>
#include "libc.h"

struct _IO_FILE {
	int fd;                 /* -1 for a memory-backed FILE */
	unsigned char eof;
	unsigned char err;
	unsigned char bufmode;   /* _IOFBF, _IOLBF, _IONBF */
	unsigned char user_buf;  /* the buffer was given by setvbuf/setbuffer: don't free it */
	unsigned char readable;
	unsigned char writable;
	unsigned char is_mem;    /* fmemopen/open_memstream */
	unsigned char mem_dynamic; /* open_memstream: mem_buf grows and is reported back */
	unsigned char mem_owned; /* fmemopen(NULL,...): mem_buf is ours, free it at fclose */
	unsigned char no_close;  /* fclose must not close fd (stdin/out/err) */

	unsigned char *buf;
	size_t bufsz;
	size_t rpos, rend;       /* unread bytes buf[rpos..rend) */
	size_t wpos;             /* unwritten bytes buf[0..wpos) */

	int unget[8];
	int nunget;

	/* fmemopen / open_memstream */
	unsigned char *mem_buf;
	size_t mem_size;         /* allocated capacity */
	size_t mem_len;          /* logical length (the string so far) */
	size_t mem_pos;          /* current offset */
	char **mem_out_ptr;      /* open_memstream: where to store mem_buf on flush */
	size_t *mem_out_size;    /* open_memstream: where to store mem_len on flush */

	pid_t pid;               /* >0 if popen()ed: pclose waits for it */

	struct _IO_FILE *next;   /* every open FILE, for __stdio_exit */
};

/* fopen mode string -> open() flags; -1 with errno on an invalid mode. */
int __fmodeflags(const char *mode);

/* Allocate the read/write buffer (lazily: not every FILE ever needs one,
 * e.g. one only ever fwrite()n in chunks bigger than BUFSIZ). */
void __ensure_buf(FILE *f);

/* Allocate a FILE around an already-open fd, link it into the open list. */
FILE *__file_new(int fd, int flags);
/* Unlink and free a FILE (the fd/memory it wraps is the caller's problem). */
void __file_free(FILE *f);

/* Flush a pending write buffer to the fd (or memory).  0 or EOF+errno. */
int __fflush_locked(FILE *f);
/* Make ready to read: flush any pending write.  0 or EOF+errno. */
int __toread(FILE *f);
/* Make ready to write: un-read any buffered-ahead bytes via seek.  0 or EOF+errno. */
int __towrite(FILE *f);
/* Refill the read buffer.  Returns bytes available (0 at EOF), or -1 with err set. */
int __fill(FILE *f);

/* The raw operations every FILE goes through: fd read/write/seek, or the
 * memory-block equivalent when f->is_mem. */
ssize_t __file_read(FILE *f, void *buf, size_t n);
ssize_t __file_write(FILE *f, const void *buf, size_t n);
long long __file_seek(FILE *f, long long off, int whence);

/* The list of every FILE currently open, for __stdio_exit. */
extern FILE *__stdio_files;

/* The core formatter/parser every printf/scanf variant calls into. */
int __vfprintf(FILE *f, const char *fmt, va_list ap);
int __vfscanf(FILE *f, const char *fmt, va_list ap);

/* fread/fwrite/fgetc/fputc without the (nonexistent) locking; the public
 * fread etc are these under a name that matches. */
size_t __fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t __fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int __fgetc(FILE *f);
int __fputc(int c, FILE *f);

#endif
