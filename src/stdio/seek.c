/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fseek/ftell and friends.  The FILE's own idea of its position is the
 * fd's (or memory block's) position adjusted by whatever is sitting in
 * the buffer: subtract unread look-ahead bytes when reading, add
 * unwritten ones when writing.
 */
#include <stdio.h>
#include "stdio_impl.h"

int fseeko(FILE *f, off_t off, int whence)
{
	long long r;
	if (f->wpos && __fflush_locked(f) < 0) return -1;
	if (whence == SEEK_CUR)
		off -= (off_t)(f->rend - f->rpos) + (off_t)f->nunget;
	/* Seek FIRST, discard the buffered read-ahead only once it has
	 * succeeded.  fseek.html: on failure "the file offset ... shall
	 * remain unchanged", and ftello() reconstructs the logical position
	 * as the underlying offset minus whatever is still unread in the
	 * buffer -- so throwing the buffer away before a seek that then
	 * fails loses exactly the term that makes that reconstruction
	 * correct, and ftell() starts reporting the read-ahead point.
	 *
	 * Reachable since fmemopen() began rejecting a seek past the end of
	 * a fixed buffer: fmemopen(buf, 10, "r+"), read to position 5, then
	 * fseek(f, 6, SEEK_CUR) is refused -- and ftell() answered 10, the
	 * point __fill() had read ahead to, instead of the unchanged 5.
	 * Nothing has moved when the seek fails, so nothing needs
	 * discarding. */
	r = __file_seek(f, off, whence);
	if (r < 0) return -1;
	f->rpos = f->rend = 0;
	f->nunget = 0;
	f->nwunget = 0;
	f->eof = 0;
	return 0;
}

int fseek(FILE *f, long off, int whence) { return fseeko(f, (off_t)off, whence); }

off_t ftello(FILE *f)
{
	long long pos = __file_seek(f, 0, SEEK_CUR);
	if (pos < 0) return -1;
	/* The buffer is never both a read and a write buffer at once, so
	 * whichever of these is nonzero is the one that applies; on a "w+"
	 * or "r+" stream either may be, so test the buffer, not the mode. */
	pos -= (long long)(f->rend - f->rpos) + f->nunget;
	pos += (long long)f->wpos;
	return (off_t)pos;
}

long ftell(FILE *f)
{
	off_t o = ftello(f);
	return (long)o;
}

void rewind(FILE *f)
{
	fseeko(f, 0, SEEK_SET);
	f->err = 0;
}

int fgetpos(FILE *__restrict f, fpos_t *__restrict pos)
{
	off_t o = ftello(f);
	if (o < 0) return -1;
	pos->__lldata = o;
	return 0;
}

int fsetpos(FILE *f, const fpos_t *pos)
{
	return fseeko(f, (off_t)pos->__lldata, SEEK_SET);
}
