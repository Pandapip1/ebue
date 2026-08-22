/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit for <stdio.h>, cross-checked
 * against https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * <name>.html.  test/stdio.c already has ~430 broad sanity checks for
 * nearly every function here (see test/posix-coverage/stdio.md for the
 * function-by-function map of what it covers); this file adds the
 * clauses that file's checks do not exercise, with a citation on every
 * assertion. Run headless under Wine, same as test/stdio.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char *make_tmp(const char *tmpl)
{
	char *t = strdup(tmpl);
	int fd = mkstemp(t);
	if (fd < 0) { free(t); return 0; }
	close(fd);
	return t;
}

/* fflush.html DESCRIPTION: "For a stream open for reading with an
 * underlying file description, if the file is not already at EOF, and
 * the file is one capable of seeking, the file offset of the underlying
 * open file description shall be set to the file position of the
 * stream, and any characters pushed back onto the stream by ungetc()
 * ... that have not subsequently been read from the stream shall be
 * discarded (without further changing the file offset)."  This applies
 * to any readable stream backed by a real fd, not just update ("+")
 * streams. */
static void test_fflush_read_stream(const char *name)
{
	FILE *f;
	int c, raw;
	char rawbuf[8];

	f = fopen(name, "wb");
	CHECK(f != 0);
	if (!f) return;
	CHECK(fputs("abcdefgh", f) == 0);
	CHECK(fclose(f) == 0);

	/* Part 1: fflush() must discard an unread pushed-back byte. */
	f = fopen(name, "rb");
	CHECK(f != 0);
	if (f) {
		CHECK((c = fgetc(f)) == 'a');
		CHECK((c = fgetc(f)) == 'b');
		CHECK(ungetc(c, f) == 'b');
		CHECK(fflush(f) == 0);
		CHECK((c = fgetc(f)) == 'c');
		CHECK(fclose(f) == 0);
	}

	/* Part 2: fflush() must resync the underlying fd's offset to the
	 * stream's position, so a raw read() on fileno(f) right after
	 * continues where the stream left off, not wherever read-ahead
	 * buffering left the fd. */
	f = fopen(name, "rb");
	CHECK(f != 0);
	if (f) {
		CHECK(setvbuf(f, 0, _IOFBF, 4096) == 0); /* force read-ahead beyond 2 bytes */
		CHECK((c = fgetc(f)) == 'a');
		CHECK((c = fgetc(f)) == 'b');
		CHECK(fflush(f) == 0);
		raw = read(fileno(f), rawbuf, 1);
		CHECK(raw == 1 && rawbuf[0] == 'c');
		CHECK(fclose(f) == 0);
	}
}

/* fopen.html DESCRIPTION: on a stream open for update ("+"), "output is
 * not directly followed by input without an intervening call to
 * fflush() or to a file positioning function..., and input is not
 * directly followed by output without an intervening call to a file
 * positioning function, unless the input operation encounters
 * end-of-file." ntlibc's __toread/__towrite (src/stdio/buf.c) apply the
 * fflush/seek automatically on every direction switch, which is a
 * strict superset of what the standard requires (it makes the "shall
 * be preceded by" cases work too, not just leaves them as UB) -- so
 * this exercises exactly the sequences the clause calls out and expects
 * them to behave as if the intervening call had been made explicitly. */
static void test_update_stream_rule(const char *name)
{
	FILE *f;
	char buf[16];

	f = fopen(name, "wb+");
	CHECK(f != 0);
	if (!f) return;
	CHECK(fputs("0123456789", f) == 0);
	/* write directly followed by read: no explicit fflush/fseek */
	rewind(f);
	memset(buf, 0, sizeof buf);
	CHECK(fread(buf, 1, 5, f) == 5);
	CHECK(memcmp(buf, "01234", 5) == 0);
	/* read directly followed by write, mid-stream (not at EOF) */
	CHECK(fputs("XY", f) == 0);
	rewind(f);
	memset(buf, 0, sizeof buf);
	CHECK(fread(buf, 1, 10, f) == 10);
	CHECK(memcmp(buf, "01234XY789", 10) == 0);
	/* read to EOF directly followed by write (explicitly permitted by
	 * the "unless the input operation encounters end-of-file" clause) */
	fseek(f, 0, SEEK_END);
	CHECK(fgetc(f) == EOF);
	CHECK(fputs("!", f) == 0);
	rewind(f);
	memset(buf, 0, sizeof buf);
	CHECK(fread(buf, 1, 11, f) == 11);
	CHECK(memcmp(buf, "01234XY789!", 11) == 0);
	CHECK(fclose(f) == 0);
}

/* setvbuf.html DESCRIPTION: "may be used after the stream ... is
 * associated with an open file but before any other operation ... is
 * performed on the stream." setvbuf.html RETURN VALUE: 0 on success,
 * non-zero if type is invalid or the request cannot be honored. */
static void test_setvbuf(const char *name)
{
	FILE *f;

	f = fopen(name, "w");
	CHECK(f != 0);
	if (!f) return;
	/* the very first operation on the stream: must succeed */
	CHECK(setvbuf(f, 0, _IOFBF, 1024) == 0);
	CHECK(fputs("hi", f) == 0);
	CHECK(fclose(f) == 0);

	/* an invalid mode is rejected */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		CHECK(setvbuf(f, 0, 12345, 0) != 0);
		CHECK(fclose(f) == 0);
	}

	/* setbuf(f, NULL) is equivalent to setvbuf(f, NULL, _IONBF, BUFSIZ):
	 * every byte written lands immediately, observable without an
	 * explicit fflush(). */
	f = fopen(name, "w");
	CHECK(f != 0);
	if (f) {
		FILE *g;
		setbuf(f, 0);
		CHECK(fputc('Z', f) == 'Z');
		g = fopen(name, "r");
		CHECK(g != 0);
		if (g) {
			CHECK(fgetc(g) == 'Z');
			CHECK(fclose(g) == 0);
		}
		CHECK(fclose(f) == 0);
	}
}

/* ungetc.html RETURN VALUE: "Otherwise, it shall return EOF" -- e.g. for
 * c == EOF (tested in test/stdio.c already) or a non-readable stream. */
static void test_ungetc_errors(const char *name)
{
	FILE *f = fopen(name, "w");
	CHECK(f != 0);
	if (!f) return;
	CHECK(ungetc('x', f) == EOF); /* stream not open for reading */
	CHECK(fclose(f) == 0);
}

/* fprintf.html RETURN VALUE: "Upon successful completion ... shall
 * return the number of bytes transmitted." (checked here against a
 * real FILE*, not just the sprintf/snprintf path test/stdio.c already
 * exercises heavily.) */
static void test_fprintf_return(const char *name)
{
	FILE *f = fopen(name, "w");
	CHECK(f != 0);
	if (!f) return;
	CHECK(fprintf(f, "%d-%s", 12, "ab") == 5);
	CHECK(fclose(f) == 0);
}

/* printf.c's own header comment: "Positional (%n$) arguments are not
 * implemented; nothing in this tree uses them." POSIX (fprintf.html
 * DESCRIPTION) specifies %n$ as a full reordering mechanism; ntlibc's
 * parser has no notion of '$' at all, so "%1$d" is read as: flags none,
 * width "1", then the byte '$' taken as the conversion specifier. '$'
 * matches no case in __vfprintf's conversion switch, so it falls to the
 * "unknown conversion: emit it literally, the way glibc does" default,
 * which emits "%$" and does NOT consume the int argument or the
 * trailing "d" as part of the specifier -- the "d" is then copied
 * through as an ordinary literal character. This asserts the documented
 * (divergent-from-POSIX) behaviour, not the standard's. */
static void test_printf_positional_divergence(void)
{
	char buf[32];
	int n = snprintf(buf, sizeof buf, "%1$d", 42);
	CHECK(n == (int)strlen("%$d"));
	CHECK(strcmp(buf, "%$d") == 0);
}

/* clearerr.html DESCRIPTION: "clears the end-of-file and error
 * indicators for the stream pointed to by stream." Both, independently
 * of each other -- test/stdio.c exercises feof/ferror but not a stream
 * that has both indicators set at once, cleared by one call. */
static void test_clearerr_both(const char *name)
{
	FILE *f = fopen(name, "rb");
	CHECK(f != 0);
	if (!f) return;
	CHECK(fseek(f, 0, SEEK_END) == 0);
	CHECK(fgetc(f) == EOF);          /* sets the EOF indicator */
	CHECK(feof(f));
	CHECK(fputc('z', f) == EOF);     /* not writable: sets the error indicator too */
	CHECK(ferror(f));
	CHECK(feof(f) && ferror(f));     /* both set at once */
	clearerr(f);
	CHECK(!feof(f));
	CHECK(!ferror(f));
	CHECK(fclose(f) == 0);
}

int main(void)
{
	char *name = make_tmp("posix-stdio-XXXXXX");
	CHECK(name != 0);
	if (name) {
		test_fflush_read_stream(name);
		test_update_stream_rule(name);
		test_setvbuf(name);
		test_ungetc_errors(name);
		test_fprintf_return(name);
		test_clearerr_both(name);
		remove(name);
		free(name);
	}
	test_printf_positional_divergence();

	if (fails) printf("%d check(s) failed\n", fails);
	else printf("all checks passed\n");
	return fails != 0;
}
