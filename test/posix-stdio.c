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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>

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

/* perror.html DESCRIPTION: "First (if s is not a null pointer and the
 * character pointed to by s is not the null byte), the string pointed
 * to by s followed by a <colon> and a <space>. Then an error message
 * string followed by a <newline>." "The error messages ... shall be
 * the same as those returned by strerror()." "The perror() function
 * shall not change the orientation of the standard error stream" (not
 * meaningfully testable here -- ntlibc's stderr is always byte
 * oriented, there is no wide-orientation mode to switch into) and,
 * from the "Error Checking" application-usage note (implicit in the
 * DESCRIPTION's silence about errno), a successful perror() call must
 * not itself change errno -- captured by comparing errno before/after.
 * Output is captured through a real pipe on fd 2, the same technique
 * test/posix-strings.c's test_assert_message_and_death() uses for the
 * same reason (a named file isn't guaranteed visible the way an
 * inherited/duplicated fd is, and this also works under the native
 * ASan harness). No child process is needed here since perror() does
 * not abort -- the redirect/restore happens in this process. */
static void capture_stderr(void (*fn)(void), char *out, size_t outsz)
{
	int p[2], saved;
	ssize_t n;

	CHECK(pipe(p) == 0);
	saved = dup(2);
	CHECK(saved >= 0);
	CHECK(dup2(p[1], 2) == 2);
	close(p[1]);

	fn();

	dup2(saved, 2);
	close(saved);
	n = read(p[0], out, outsz - 1);
	close(p[0]);
	out[n > 0 ? n : 0] = 0;
}

static void perror_prefixed(void) { errno = ENOENT; perror("myprefix"); }
static void perror_noprefix_null(void) { errno = EACCES; perror(0); }
static void perror_noprefix_empty(void) { errno = EACCES; perror(""); }

static void test_perror(void)
{
	char buf[256];
	int e;

	/* "s followed by a <colon> and a <space>. Then an error message
	 * string ... followed by a <newline>." and "shall be the same as
	 * those returned by strerror()". */
	capture_stderr(perror_prefixed, buf, sizeof buf);
	{
		char want[256];
		strcpy(want, "myprefix: ");
		strcat(want, strerror(ENOENT));
		strcat(want, "\n");
		CHECK(strcmp(buf, want) == 0);
	}

	/* "if s is not a null pointer and the character pointed to by s is
	 * not the null byte" -- s == NULL: no prefix/colon/space at all. */
	capture_stderr(perror_noprefix_null, buf, sizeof buf);
	{
		char want[256];
		strcpy(want, strerror(EACCES));
		strcat(want, "\n");
		CHECK(strcmp(buf, want) == 0);
	}

	/* s == "" (not null, but *s == '\0'): same "no prefix" case. */
	capture_stderr(perror_noprefix_empty, buf, sizeof buf);
	{
		char want[256];
		strcpy(want, strerror(EACCES));
		strcat(want, "\n");
		CHECK(strcmp(buf, want) == 0);
	}

	/* perror() itself must not change errno on success (the
	 * "Error Checking" note only makes sense if a successful call
	 * leaves errno as the caller set it -- otherwise the "clearerr()
	 * then check errno" recipe it describes couldn't distinguish a
	 * write failure from perror() clobbering errno on its own). */
	e = ENOENT;
	errno = e;
	capture_stderr(perror_prefixed, buf, sizeof buf);
	CHECK(errno == ENOENT);
	(void)e;
}

/* popen.html/pclose.html: src/stdio/misc.c's own header comment
 * documents that popen() hands the command to cmd.exe /c rather than a
 * POSIX shell, since there is no /bin/sh on NT -- that divergence is
 * real and not tested here (the *string* handed to the interpreter is
 * cmd syntax, not sh syntax).  Everything else the spec promises about
 * the *stream* and *pclose()*'s return, though, is plain fd/process
 * plumbing that does not depend on which interpreter runs the command,
 * and src/stdio/misc.c implements all of it -- so it is asserted for
 * real below rather than waved off as "no POSIX shell". */
/* popen() spawns cmd.exe (via %ComSpec%, per src/stdio/misc.c) as a real
 * child process. Under Wine (the normal `make check` target) that is a
 * real, present binary. Under this project's native-ASan harness there
 * is no cmd.exe at all -- __spawn's ability to reach *this test binary*
 * again under a special-cased argv (as test/posix-strings.c's spawn
 * helper does) does not extend to an arbitrary Windows executable path,
 * so popen() legitimately fails to spawn there. That is an environment
 * limitation, not a popen()/pclose() bug, so each block below degrades
 * to a note instead of a hard failure when popen() itself returns NULL;
 * every assertion still runs for real wherever cmd.exe is actually
 * reachable. */
static void test_popen(void)
{
	FILE *f;
	int st;
	char buf[256];

	/* popen.html DESCRIPTION, mode 'r': "the file descriptor
	 * fileno(stream) in the calling process ... shall be the readable
	 * end of the pipe" -- reading actually returns the child's output,
	 * and writing to it fails (not the writable end). */
	f = popen("echo hello", "r");
	if (!f) {
		printf("note: popen() could not spawn a command interpreter in this environment (errno %d); popen/pclose \"r\" checks skipped\n", errno);
	} else {
		CHECK(fgets(buf, sizeof buf, f) != 0);
		CHECK(strstr(buf, "hello") != 0);
		errno = 0;
		CHECK(fputc('x', f) == EOF && errno == EBADF);
		/* pclose.html RETURN VALUE: "Upon successful return, pclose()
		 * shall return the termination status of the command language
		 * interpreter" -- cmd.exe /c "echo hello" exits 0. */
		st = pclose(f);
		CHECK(st != -1);
		CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	}

	/* mode 'w': "STDIN_FILENO shall be the readable end of the pipe,
	 * and ... fileno(stream) in the calling process ... shall be the
	 * writable end" -- writing succeeds, reading fails.  cmd.exe /c
	 * "exit 7" doesn't touch stdin at all, so what it does with the
	 * bytes written is unobserved here; what IS observed is that
	 * pclose() reports the exact exit status the interpreter used,
	 * which is the concrete, checkable form of "termination status of
	 * the command language interpreter" for this mode. */
	f = popen("exit 7", "w");
	if (!f) {
		printf("note: popen() could not spawn a command interpreter in this environment (errno %d); popen/pclose \"w\" checks skipped\n", errno);
	} else {
		CHECK(fputs("ignored\n", f) >= 0);
		errno = 0;
		CHECK(fgetc(f) == EOF && errno == EBADF);
		st = pclose(f);
		CHECK(st != -1);
		CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 7);
	}

	/* popen.html ERRORS ("may fail"): "[EINVAL] The mode argument is
	 * invalid." src/stdio/misc.c rejects any mode[0] other than 'r'/
	 * 'w' outright, before ever trying to spawn anything, so this one
	 * holds regardless of whether cmd.exe is reachable. */
	errno = 0;
	CHECK(popen("echo hi", "x") == 0 && errno == EINVAL);

	/* pclose.html ERRORS: "[ECHILD] The status of the child process
	 * could not be obtained" -- a legitimate way this happens without
	 * touching any ntlibc-internal state: the application itself reaps
	 * the popen'd child (e.g. via a wait()/waitpid(-1, ...) loop that
	 * doesn't know or care it came from popen(), which POSIX permits),
	 * so by the time pclose() calls waitpid() on that same pid there is
	 * nothing left to wait for. */
	f = popen("exit 0", "w");
	if (!f) {
		printf("note: popen() could not spawn a command interpreter in this environment (errno %d); pclose() ECHILD check skipped\n", errno);
	} else {
		CHECK(waitpid(-1, &st, 0) > 0); /* reaps popen()'s own child first */
		errno = 0;
		CHECK(pclose(f) == -1 && errno == ECHILD);
	}
}

#if 0 /* N/A: popen.html ERRORS "shall fail" clause: "[EMFILE]
       * {STREAM_MAX} streams are currently open in the calling
       * process." Driving the process to STREAM_MAX (FOPEN_MAX) open
       * FILE*s purely to observe one more popen() call fail is not a
       * popen()-specific behaviour -- every *fopen()-family function
       * hits the same wall the same way, and ntlibc has no
       * STREAM_MAX-specific logic in popen() to distinguish from the
       * generic "out of fd table / out of memory" paths fopen() itself
       * already exercises; doing it here would just be an expensive,
       * redundant repeat of that generic exhaustion test under a
       * different function name. */
static void test_popen_emfile(void)
{
	FILE *fs[8192];
	int i, n = 0;

	for (i = 0; i < 8192; i++) {
		fs[i] = popen("exit 0", "r");
		if (!fs[i]) break;
		n++;
	}
	CHECK(fs[n] == 0 && errno == EMFILE);
	for (i = 0; i < n; i++) pclose(fs[i]);
}
#endif


/* Everything below exists in src/ and links, but was named by no
 * assertion anywhere in test/*.c before this (see
 * test/POSIX-GAP-ACCOUNTING.md's "implemented, but no assertion
 * anywhere in test/*.c" list, which this closes for <stdio.h>).  These
 * are deliberately the cheap RETURN VALUE / DESCRIPTION clauses, not a
 * clause-by-clause audit -- the ledger's stdio.h section stays the
 * authority for that.  They run on real Windows in CI too, which is the
 * only authority for real-NT behaviour; nothing here depends on any
 * Wine-specific behaviour. */

/* putc.html RETURN VALUE: "shall return the value written"; putc() and
 * fputc() are equivalent except that putc() may be a macro evaluating
 * its stream argument more than once.  putc_unlocked.html: "versions of
 * ... putc() ... that ... may be safely used only within a scope
 * protected by flockfile()"; behaviour is otherwise identical. */
static void test_putc_family(const char *name)
{
	FILE *f;
	char buf[8];
	size_t n;

	f = fopen(name, "wb");
	CHECK(f != 0);
	if (!f) return;
	CHECK(putc('a', f) == 'a');
	CHECK(putc_unlocked('b', f) == 'b');
	/* putc.html: the value is written "as an unsigned char converted
	 * to an int", so a byte above 0x7f comes back positive, not
	 * sign-extended. */
	CHECK(putc((int)(unsigned char)0xfe, f) == 0xfe);
	CHECK(fclose(f) == 0);

	f = fopen(name, "rb");
	CHECK(f != 0);
	if (!f) return;
	n = fread(buf, 1, sizeof buf, f);
	CHECK(n == 3);
	CHECK(buf[0] == 'a');
	CHECK(buf[1] == 'b');
	CHECK((unsigned char)buf[2] == 0xfe);
	CHECK(fclose(f) == 0);
}

/* putchar.html: "equivalent to putc(c, stdout)", RETURN VALUE "the
 * value written".  Asserted without redirecting stdout: the only
 * observable effect is one extra newline in this test's own output,
 * which no harness parses. */
static void test_putchar_return(void)
{
	CHECK(putchar('\n') == '\n');
	CHECK(putchar_unlocked('\n') == '\n');
}

/* getchar.html: "equivalent to getc(stdin)"; RETURN VALUE "the next
 * byte ... as an unsigned char converted to an int", or EOF at
 * end-of-file.  stdin is repointed at a known file rather than trusted
 * to be anything in particular -- the gate runs with stdin from
 * /dev/null, CI on real Windows does not necessarily. */
static void test_getchar(const char *name)
{
	FILE *f;

	f = fopen(name, "wb");
	CHECK(f != 0);
	if (!f) return;
	CHECK(fputc('X', f) == 'X');
	CHECK(fputc((int)(unsigned char)0x80, f) == 0x80);
	CHECK(fclose(f) == 0);

	if (!freopen(name, "rb", stdin)) { CHECK(0); return; }
	CHECK(getchar() == 'X');
	/* unsigned-char conversion, not sign extension */
	CHECK(getchar_unlocked() == 0x80);
	CHECK(getchar() == EOF);
	CHECK(feof(stdin));
}

/* ftell.html/fseek.html, off_t forms: "ftello() ... shall obtain the
 * current value of the file-position indicator"; fseeko() returns 0 on
 * success.  Same contract as ftell()/fseek(), which test/stdio.c
 * already covers -- what is new here is only that the off_t-typed
 * spellings exist and agree with them. */
static void test_fseeko_ftello(const char *name)
{
	FILE *f;
	off_t pos;

	f = fopen(name, "wb");
	CHECK(f != 0);
	if (!f) return;
	CHECK(fwrite("0123456789", 1, 10, f) == 10);
	pos = ftello(f);
	CHECK(pos == (off_t)10);
	CHECK(fseeko(f, (off_t)3, SEEK_SET) == 0);
	CHECK(ftello(f) == (off_t)3);
	CHECK(fseeko(f, (off_t)2, SEEK_CUR) == 0);
	CHECK(ftello(f) == (off_t)5);
	CHECK(fseeko(f, (off_t)0, SEEK_END) == 0);
	CHECK(ftello(f) == (off_t)10);
	CHECK(ftell(f) == 10L);
	CHECK(fclose(f) == 0);
}

/* flockfile.html: ftrylockfile() "shall return zero for success", and
 * the lock is recursive ("the lock count ... shall be incremented"), so
 * a nested acquisition must also succeed and needs a matching
 * funlockfile().  ntlibc is single-threaded today and src/stdio/file.c
 * implements all three as no-ops; the clause these assert is the
 * RETURN VALUE, which a no-op still has to get right. */
static void test_flockfile(const char *name)
{
	FILE *f = fopen(name, "wb");
	CHECK(f != 0);
	if (!f) return;
	flockfile(f);
	CHECK(ftrylockfile(f) == 0);
	funlockfile(f);
	funlockfile(f);
	CHECK(fclose(f) == 0);
}

/* vfprintf.html: the v-forms are "equivalent to ... with the variable
 * argument list replaced by arg", and return the same byte count.
 * Wrapped here so each one is reached through a real va_list. */
static int via_vfprintf(FILE *f, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}

static int via_vsnprintf(char *b, size_t n, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vsnprintf(b, n, fmt, ap);
	va_end(ap);
	return r;
}

static int via_vsprintf(char *b, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vsprintf(b, fmt, ap);
	va_end(ap);
	return r;
}

static int via_vsscanf(const char *src, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vsscanf(src, fmt, ap);
	va_end(ap);
	return r;
}

static int via_vfscanf(FILE *f, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vfscanf(f, fmt, ap);
	va_end(ap);
	return r;
}

static int via_vdprintf(int fd, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vdprintf(fd, fmt, ap);
	va_end(ap);
	return r;
}

static void test_v_forms(const char *name)
{
	FILE *f;
	char buf[64];
	int fd, got;

	CHECK(via_vsnprintf(buf, sizeof buf, "%s-%d", "ab", 7) == 4);
	CHECK(strcmp(buf, "ab-7") == 0);
	/* vsnprintf.html RETURN VALUE: "the number of bytes that would
	 * have been written ... had n been sufficiently large", not the
	 * number actually written. */
	CHECK(via_vsnprintf(buf, 3, "%s-%d", "ab", 7) == 4);
	CHECK(strcmp(buf, "ab") == 0);

	memset(buf, 0, sizeof buf);
	CHECK(via_vsprintf(buf, "%d/%c", 42, 'z') == 4);
	CHECK(strcmp(buf, "42/z") == 0);

	got = 0;
	CHECK(via_vsscanf("  91x", "%d", &got) == 1);
	CHECK(got == 91);

	f = fopen(name, "wb");
	CHECK(f != 0);
	if (!f) return;
	CHECK(via_vfprintf(f, "%s=%d\n", "k", 5) == 4);
	CHECK(fclose(f) == 0);

	f = fopen(name, "rb");
	CHECK(f != 0);
	if (!f) return;
	got = 0;
	CHECK(via_vfscanf(f, "k=%d", &got) == 1);
	CHECK(got == 5);
	CHECK(fclose(f) == 0);

	/* dprintf.html: "equivalent to fprintf(), except that dprintf()
	 * shall write output to the file associated with the file
	 * descriptor fildes rather than ... a stream". */
	fd = open(name, O_WRONLY | O_TRUNC);
	CHECK(fd >= 0);
	if (fd < 0) return;
	CHECK(dprintf(fd, "%s", "abcd") == 4);
	CHECK(via_vdprintf(fd, "%d", 12345) == 5);
	CHECK(close(fd) == 0);

	f = fopen(name, "rb");
	CHECK(f != 0);
	if (!f) return;
	memset(buf, 0, sizeof buf);
	CHECK(fread(buf, 1, sizeof buf - 1, f) == 9);
	CHECK(strcmp(buf, "abcd12345") == 0);
	CHECK(fclose(f) == 0);
}

/* getc_unlocked.html.  DESCRIPTION: the _unlocked forms "shall be
 * functionally equivalent to the original versions, with the exception
 * that they are not required to be implemented in a fully thread-safe
 * manner", and "shall be thread-safe when used within a scope protected
 * by flockfile() ... and funlockfile()".  RETURN VALUE and ERRORS both
 * defer to getc()/getchar()/putc()/putchar().
 *
 * test_putc_family() and test_getchar() above already reach
 * putc_unlocked, putchar_unlocked and getchar_unlocked; getc_unlocked
 * was the one name in the family nothing in the tree had ever called
 * (test/POSIX-GAP-ACCOUNTING.md's <stdio.h> pair).  Asserted against
 * getc() on the same stream, inside a flockfile()/funlockfile() scope as
 * the page prescribes.  Pure library behaviour: Wine is a sound oracle.
 *
 * N/A, with the reason: the thread-safety clause itself.  ntlibc has no
 * threading to speak of (src/signal/signal.c's banner and the FILE
 * locking in src/stdio/file.c say as much), so "not required to be
 * thread-safe" and "thread-safe under flockfile()" have no observable
 * difference to test between. */
static void test_getc_unlocked(const char *name)
{
	FILE *f = fopen(name, "wb");
	int c;

	CHECK(f != 0);
	if (!f) return;
	CHECK(fwrite("Ab\200", 1, 3, f) == 3);
	CHECK(fclose(f) == 0);

	f = fopen(name, "rb");
	CHECK(f != 0);
	if (!f) return;
	flockfile(f);
	CHECK(getc_unlocked(f) == 'A');
	/* functionally equivalent to getc(): same stream, same position */
	CHECK(getc(f) == 'b');
	/* unsigned-char conversion, not sign extension */
	CHECK(getc_unlocked(f) == 0x80);
	c = getc_unlocked(f);
	CHECK(c == EOF);
	CHECK(feof(f));
	funlockfile(f);
	CHECK(fclose(f) == 0);
}

/* tempnam.html (XSI, obsolescent).  DESCRIPTION: "shall generate a
 * pathname that may be used for a temporary file"; if dir "is a null
 * pointer or points to a string which is not a pathname for an
 * appropriate directory, the path prefix defined as P_tmpdir in the
 * <stdio.h> header shall be used"; pfx is "a string of up to five bytes
 * to be used as the beginning of the filename".  RETURN VALUE: "Upon
 * successful completion, tempnam() shall allocate space for a string,
 * put the generated pathname in that space, and return a pointer to it.
 * The pointer shall be suitable for use in a subsequent call to
 * free()."  ERRORS: "[ENOMEM] Insufficient storage space is available."
 *
 * That the result must be free()-able is the clause with teeth here, and
 * it is checked for real: tools/asan-build.sh runs every test under
 * LeakSanitizer, so a returned pointer that is not a live malloc block
 * fails the asan gate rather than passing quietly.  This is the same
 * mechanism that caught the vdprintf() leak the last never-asserted
 * sweep turned up.
 *
 * N/A, with the reason: [ENOMEM].  Reaching it needs allocator
 * exhaustion, which this suite has no way to induce -- the ledger
 * already treats malloc-exhaustion paths that way throughout.
 *
 * Filesystem-adjacent (it names a path under the temp directory and
 * src/stdio/misc.c reaches mkstemp() to pick one), so real-Windows CI
 * is the authority; Wine agreeing is weak evidence. */
static void test_tempnam(void)
{
	char *a, *b;
	struct stat st;

	a = tempnam(".", "tn");
	CHECK(a != 0);
	if (a) {
		/* honours dir and pfx */
		CHECK(!strncmp(a, "./tn", 4));
		/* "a pathname that may be used for a temporary file": the name
		 * must not already be taken, or the caller's create races. */
		CHECK(stat(a, &st) == -1);
		/* usable as promised */
		{
			FILE *f = fopen(a, "wb");
			CHECK(f != 0);
			if (f) {
				CHECK(fputs("x", f) != EOF);
				CHECK(fclose(f) == 0);
				CHECK(stat(a, &st) == 0);
				CHECK(remove(a) == 0);
			}
		}
	}

	b = tempnam(".", "tn");
	CHECK(b != 0);
	/* two calls must not hand out the same name */
	if (a && b) CHECK(strcmp(a, b) != 0);
	if (b) CHECK(stat(b, &st) == -1);

	/* a null dir falls back to P_tmpdir (or an implementation-defined
	 * directory); only that a pathname comes back at all is portable. */
	free(a);
	free(b);
	a = tempnam(NULL, "tn");
	CHECK(a != 0);
	if (a) {
		CHECK(strlen(a) > 0);
		CHECK(stat(a, &st) == -1);
	}
	/* "suitable for use in a subsequent call to free()" -- and under
	 * tools/asan-build.sh's LeakSanitizer, not freeing would fail. */
	free(a);

	/* a null pfx is allowed too */
	b = tempnam(".", NULL);
	CHECK(b != 0);
	if (b) CHECK(stat(b, &st) == -1);
	free(b);
}

/* vprintf.html / vscanf.html, and the <stdarg.h> machinery they are
 * defined in terms of:
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/vprintf.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/vscanf.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/va_arg.html
 *
 * vprintf.html DESCRIPTION: the v-forms "shall be equivalent to
 * printf() ... respectively, except that instead of being called with a
 * variable number of arguments, they are called with an argument list";
 * RETURN VALUE: "the number of bytes transmitted".  vscanf.html: the
 * same equivalence to scanf(), returning "the number of successfully
 * matched and assigned input items".  va_arg.html DESCRIPTION: va_copy
 * "initializes dest as a copy of src, as if the va_start() macro had
 * been applied to dest followed by the same sequence of uses of the
 * va_arg() macro as had previously been used to reach the present state
 * of src".
 *
 * test_v_forms() above already reaches vfprintf/vsnprintf/vsprintf/
 * vsscanf/vfscanf/vdprintf and, through them, va_start/va_end.  vprintf,
 * vscanf, va_arg and va_copy were the four names left with no assertion
 * anywhere (test/POSIX-GAP-ACCOUNTING.md's <stdarg.h> group).  vprintf
 * and vscanf are the stdout/stdin forms, so they need those two streams
 * redirected -- fd-level for stdout (dup/dup2 around the call, so the
 * stream object survives), freopen() for stdin, the same way
 * test_getchar() does it.
 *
 * Pure C library over a redirected fd: Wine is a sound oracle. */
static int via_vprintf(const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vprintf(fmt, ap);
	va_end(ap);
	return r;
}

static int via_vscanf(const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vscanf(fmt, ap);
	va_end(ap);
	return r;
}

/* Walks ap with va_arg, then walks a va_copy of it from the same point
 * and checks the copy yields the identical sequence -- the whole of what
 * va_copy promises.  The original is consumed first so that, if va_copy
 * silently aliased rather than copied, the second walk would read past
 * the end instead of repeating. */
static int va_copy_sees_same(int n, ...)
{
	va_list ap, ap2;
	int first[8], second[8], i, ok = 1;

	if (n > 8) return 0;
	va_start(ap, n);
	va_copy(ap2, ap);
	for (i = 0; i < n; i++) first[i] = va_arg(ap, int);
	va_end(ap);
	for (i = 0; i < n; i++) second[i] = va_arg(ap2, int);
	va_end(ap2);
	for (i = 0; i < n; i++) if (first[i] != second[i]) ok = 0;
	for (i = 0; i < n; i++) if (first[i] != i * 11 + 1) ok = 0;
	return ok;
}

static void test_vprintf_vscanf(const char *name)
{
	char buf[64];
	int saved, fd, got = 0, r;
	FILE *f;

	/* va_arg/va_copy first: no I/O involved. */
	CHECK(va_copy_sees_same(5, 1, 12, 23, 34, 45));
	CHECK(va_copy_sees_same(1, 1));
	CHECK(va_copy_sees_same(0));

	/* vprintf: redirect fd 1 at the temp file for the duration. */
	saved = dup(1);
	CHECK(saved >= 0);
	fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (saved < 0 || fd < 0) return;
	CHECK(fflush(stdout) == 0);
	CHECK(dup2(fd, 1) == 1);
	CHECK(close(fd) == 0);
	r = via_vprintf("%s=%d\n", "vp", 17);
	CHECK(fflush(stdout) == 0);
	CHECK(dup2(saved, 1) == 1);
	CHECK(close(saved) == 0);
	/* "the number of bytes transmitted": "vp=17\n" */
	CHECK(r == 6);

	f = fopen(name, "rb");
	CHECK(f != 0);
	if (f) {
		memset(buf, 0, sizeof buf);
		CHECK(fread(buf, 1, sizeof buf - 1, f) == 6);
		CHECK(!strcmp(buf, "vp=17\n"));
		CHECK(fclose(f) == 0);
	}

	/* vscanf: same file, now as stdin. */
	if (!freopen(name, "rb", stdin)) { CHECK(0); return; }
	CHECK(via_vscanf("vp=%d", &got) == 1);
	CHECK(got == 17);
	/* "the number of successfully matched and assigned input items" is
	 * EOF once the input is exhausted before any conversion. */
	CHECK(via_vscanf("%d", &got) == EOF);
}

/* ------------------------------------------------------------------ *
 * Clause audit of the <stdio.h> (16) and <stdarg.h> (12) rows of
 * test/POSIX-GAP-ACCOUNTING.md's "Implemented, not clause-audited"
 * table.  Everything below cites
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 * (or basedefs/stdarg.h.html) by section.
 * ------------------------------------------------------------------ */

/* fprintf.html DESCRIPTION: "The snprintf() function shall be
 * equivalent to sprintf(), with the addition of the n argument which
 * states the size of the buffer referred to by s.  If n is zero,
 * nothing shall be written and s may be a null pointer.  Otherwise,
 * output bytes beyond the n-1st shall be discarded instead of being
 * written to the array, and a null byte is written at the end of the
 * bytes actually written into the array."
 *
 * fprintf.html RETURN VALUE: "Upon successful completion, the
 * snprintf() function shall return the number of bytes that would be
 * written to s had n been sufficiently large excluding the terminating
 * null byte", and "If the value of n is zero on a call to snprintf(),
 * nothing shall be written, the number of bytes that would have been
 * written had n been sufficiently large excluding the terminating null
 * shall be returned, and s may be a null pointer."
 *
 * fprintf.html RETURN VALUE, sprintf: "the number of bytes written to
 * s, excluding the terminating null byte"; DESCRIPTION: sprintf "shall
 * place output followed by the null byte".
 *
 * The exact-fit / one-short / one-byte / zero-byte cases are walked
 * individually, and each writes a sentinel past the buffer end first so
 * that a formatter that scribbled one byte too far would be caught here
 * rather than only under asan.  Pure library code over memory: Wine is
 * a sound oracle. */
static void test_snprintf_boundaries(void)
{
	char b[16];
	int r;

	/* exact fit: n == strlen + 1 */
	memset(b, '#', sizeof b);
	r = snprintf(b, 5, "abcd");
	CHECK(r == 4);
	CHECK(!strcmp(b, "abcd"));
	CHECK(b[5] == '#');          /* nothing past the n-1st byte's NUL */

	/* one short: the 4th byte is discarded, a NUL goes at b[3] */
	memset(b, '#', sizeof b);
	r = snprintf(b, 4, "abcd");
	CHECK(r == 4);               /* what *would* have been written */
	CHECK(!strcmp(b, "abc"));
	CHECK(b[4] == '#');

	/* n == 1: only the null byte fits */
	memset(b, '#', sizeof b);
	r = snprintf(b, 1, "abcd");
	CHECK(r == 4);
	CHECK(b[0] == 0);
	CHECK(b[1] == '#');

	/* n == 0: "nothing shall be written" -- not even the NUL */
	memset(b, '#', sizeof b);
	r = snprintf(b, 0, "abcd");
	CHECK(r == 4);
	CHECK(b[0] == '#');

	/* n == 0 and "s may be a null pointer" */
	CHECK(snprintf(NULL, 0, "abcd%d", 7) == 5);

	/* the same three shapes through the v-form (vfprintf.html:
	 * "equivalent to ... snprintf()") */
	memset(b, '#', sizeof b);
	CHECK(via_vsnprintf(b, 5, "abcd") == 4);
	CHECK(!strcmp(b, "abcd") && b[5] == '#');
	memset(b, '#', sizeof b);
	CHECK(via_vsnprintf(b, 4, "abcd") == 4);
	CHECK(!strcmp(b, "abc") && b[4] == '#');
	CHECK(via_vsnprintf(NULL, 0, "abcd") == 4);

	/* sprintf: bytes written excluding the NUL, and the NUL is there */
	memset(b, '#', sizeof b);
	r = sprintf(b, "%s%d", "x", 42);
	CHECK(r == 3);
	CHECK(!strcmp(b, "x42"));
	CHECK(b[4] == '#');
	memset(b, '#', sizeof b);
	CHECK(via_vsprintf(b, "%s%d", "x", 42) == 3);
	CHECK(!strcmp(b, "x42"));

	/* DESCRIPTION: "If the format is exhausted while arguments remain,
	 * the excess arguments shall be evaluated but are otherwise
	 * ignored." */
	memset(b, '#', sizeof b);
	CHECK(snprintf(b, sizeof b, "x", 1, 2) == 1);
	CHECK(!strcmp(b, "x"));
}

/* fprintf.html ERRORS: "The snprintf() function shall fail if:
 * [EOVERFLOW] The value of n is greater than {INT_MAX}."  Note the
 * "shall": unlike the [EBADF] under dprintf() this is not a "may fail",
 * so a conforming snprintf() has to reject the call, return a negative
 * value (RETURN VALUE: "If an output error was encountered, these
 * functions shall return a negative value and set errno") and never
 * touch s.
 *
 * src/stdio/printf.c's vxprintf_mem() takes n straight to the throwaway
 * memory FILE's mem_size and never compares it to INT_MAX, so the call
 * succeeds and formats normally.  Measured: returns 1 with errno left
 * at 0 and "z" in the buffer. */
#if 0 /* BUG: snprintf() does not fail with [EOVERFLOW] when n > INT_MAX; POSIX fprintf.html ERRORS makes that a "shall fail" for snprintf, so the call must return a negative value and set errno, not format into the (unreachably large) buffer */
static void test_snprintf_eoverflow(void)
{
	char b[8];
	int r;

	memset(b, '#', sizeof b);
	errno = 0;
	r = snprintf(b, (size_t)INT_MAX + 1, "z");
	CHECK(r < 0);
	CHECK(errno == EOVERFLOW);
	CHECK(b[0] == '#');
}
#endif

/* fprintf.html, the flag characters: "'  [CX] (The <apostrophe>.)  The
 * integer portion of the result of a decimal conversion ( %i, %d, %u,
 * %f, %F, %g, or %G ) shall be formatted with thousands' grouping
 * characters.  ...  The non-monetary grouping character is used."
 *
 * This is a [CX] flag, i.e. base POSIX, not XSI.  In the POSIX locale
 * the non-monetary grouping is empty (XBD 7.3.4 LC_NUMERIC: the POSIX
 * locale's `grouping` is unspecified-length/no grouping, and
 * localeconv()'s POSIX-locale `grouping` is ""), so a conforming
 * implementation must accept the flag and produce exactly what the
 * unflagged conversion produces.  ntlibc's flag loop in
 * src/stdio/printf.c recognises only '-', '+', ' ', '0' and '#', so a
 * <apostrophe> ends the flag scan and then falls out of the conversion
 * switch's default arm, which emits the two bytes literally.  Measured:
 * snprintf("%'d", 1234567) yields "%'d", 3 bytes. */
#if 0 /* BUG: the [CX] <apostrophe> flag is not recognised; "%'d" is emitted literally instead of formatting the argument (in the POSIX locale, identically to "%d") */
static void test_printf_apostrophe_flag(void)
{
	char grouped[32], plain[32];

	CHECK(snprintf(plain, sizeof plain, "%d", 1234567) == 7);
	CHECK(snprintf(grouped, sizeof grouped, "%'d", 1234567) == 7);
	CHECK(!strcmp(grouped, plain));
}
#endif

/* fprintf.html DESCRIPTION, field width and precision: "A negative
 * field width is taken as a '-' flag followed by a positive field
 * width.  A negative precision is taken as if the precision were
 * omitted."  Also the flag table's '#', '+' and <space> entries, the
 * "null digit string is treated as zero" precision rule, and the
 * length modifiers.
 *
 * The wide-field/wide-precision cases exist because this formatter
 * sizes nothing from the caller's precision on purpose (see PREC_MAX
 * and BODYMAX in src/stdio/printf.c) and a fixed-size body buffer here
 * has been a real defect before: every one of these writes far more
 * bytes than any internal buffer, and runs under asan in
 * tools/asan-build.sh. */
static void test_printf_width_precision(void)
{
	static char big[8192];
	char b[64];

	/* "A negative precision is taken as if the precision were omitted" */
	CHECK(snprintf(b, sizeof b, "%.*d", -3, 42) == 2);
	CHECK(!strcmp(b, "42"));
	/* "A negative field width is taken as a '-' flag followed by a
	 * positive field width" */
	CHECK(snprintf(b, sizeof b, "%*d|", -5, 42) == 6);
	CHECK(!strcmp(b, "42   |"));
	/* '#' for o and x; '+' and <space> for signed conversions; '#' for
	 * f ("the result shall always contain a radix character") */
	CHECK(snprintf(b, sizeof b, "%#o|%#x|%+d|% d|%#.0f", 8, 255, 3, 3, 1.0) == 17);
	CHECK(!strcmp(b, "010|0xff|+3| 3|1."));
	/* precision 0 with a zero d value prints nothing: "The result of
	 * converting a zero value with a precision of 0 shall be no
	 * characters." */
	CHECK(snprintf(b, sizeof b, "[%.0d][%.0d]", 0, 5) == 5);
	CHECK(!strcmp(b, "[][5]"));
	/* length modifiers narrow the argument before conversion */
	CHECK(snprintf(b, sizeof b, "%hhd|%hd", 300, 70000) == 7);
	CHECK(!strcmp(b, "44|4464"));

	/* widths and precisions far past any internal buffer */
	CHECK(snprintf(big, sizeof big, "%5000d", 7) == 5000);
	CHECK(strlen(big) == 5000);
	CHECK(big[4999] == '7' && big[0] == ' ');
	CHECK(snprintf(big, sizeof big, "%.5000d", 7) == 5000);
	CHECK(strlen(big) == 5000);
	CHECK(snprintf(big, sizeof big, "%.5000f", 1.0 / 3.0) == 5002);
	CHECK(strlen(big) == 5002);
	CHECK(!strncmp(big, "0.3333333333", 12));
	CHECK(snprintf(big, sizeof big, "%.5000e", 1.0 / 3.0) == 5006);
	CHECK(strlen(big) == 5006);
	/* "%g: the maximum number of significant digits" -- trailing zeros
	 * are removed, so a huge precision does not make a huge result */
	CHECK(snprintf(big, sizeof big, "%.5000g", 1.0 / 3.0) == 56);
	/* "the number of digits to appear after the radix character for the
	 * a, A, e, E, f, and F conversion specifiers" -- every one of them,
	 * exactly, out to the last place a double has: the smallest
	 * subnormal is 2^-1074, so its exact expansion's last non-zero
	 * fractional digit is the 1074th and it is a '5' (the whole tail is
	 * "...265625", 2^-1074 being 5^1074 * 10^-1074).  A formatter that
	 * clamped its internal expansion short would still get the length
	 * right and the leading digits right, and only this would catch it. */
	CHECK(snprintf(big, sizeof big, "%.1074f", 4.9406564584124654e-324) == 1076);
	CHECK(strlen(big) == 1076);
	CHECK(!strcmp(big + 1070, "265625"));

	/* "the maximum number of bytes to be printed from a string" */
	CHECK(snprintf(big, sizeof big, "%.5000s", "abc") == 3);
	CHECK(snprintf(big, sizeof big, "%.3s", "abcdef") == 3);
	CHECK(!strcmp(big, "abc"));
}

/* fprintf.html RETURN VALUE: "If an output error was encountered, these
 * functions shall return a negative value and set errno to indicate the
 * error", and ERRORS: "For the conditions under which dprintf(),
 * fprintf(), and printf() fail and may fail, refer to fputc" --
 * fputc.html ERRORS lists [EBADF] "The file descriptor underlying
 * stream is not a valid file descriptor open for writing" and [EPIPE]
 * "An attempt is made to write to a pipe or FIFO that is not open for
 * reading by any process.  A SIGPIPE signal shall also be sent to the
 * thread."
 *
 * fputc.html also requires the stream's error indicator to be set
 * ("shall set the error indicator for the stream"), which ferror()
 * observes.  The EPIPE case ignores SIGPIPE first, precisely because
 * the signal is the default outcome: without the SIG_IGN the process
 * dies of it, which is itself the clause's other half and is what this
 * measured before adding the ignore.
 *
 * Wine is a sound oracle for the EBADF path (pure fd bookkeeping);
 * the EPIPE path goes through NT named pipes, so real-Windows CI is
 * the authority there. */
static void test_printf_output_error(const char *name)
{
	FILE *f;
	int fds[2], r;

	f = fopen(name, "wb");
	CHECK(f != 0);
	if (!f) return;
	CHECK(setvbuf(f, 0, _IONBF, 0) == 0);
	CHECK(close(fileno(f)) == 0);
	errno = 0;
	r = fprintf(f, "hello");
	CHECK(r < 0);
	CHECK(errno == EBADF);
	CHECK(ferror(f) != 0);
	clearerr(f);
	fclose(f);

	/* dprintf.html/fprintf.html ERRORS: "[EBADF] The fildes argument is
	 * not a valid file descriptor" (a "may fail" for dprintf, but the
	 * write is an output error either way, so the negative return and
	 * the errno are required by RETURN VALUE). */
	errno = 0;
	CHECK(dprintf(-1, "x") < 0);
	CHECK(errno == EBADF);
	errno = 0;
	CHECK(via_vdprintf(-1, "x") < 0);
	CHECK(errno == EBADF);

	signal(SIGPIPE, SIG_IGN);
	if (pipe(fds) == 0) {
		CHECK(close(fds[0]) == 0);
		f = fdopen(fds[1], "wb");
		CHECK(f != 0);
		if (f) {
			CHECK(setvbuf(f, 0, _IONBF, 0) == 0);
			errno = 0;
			r = fprintf(f, "hello");
			CHECK(r < 0);
			CHECK(errno == EPIPE);
			CHECK(ferror(f) != 0);
			clearerr(f);
			fclose(f);
		} else {
			close(fds[1]);
		}
	} else {
		CHECK(0);
	}
	signal(SIGPIPE, SIG_DFL);
}

/* fprintf.html DESCRIPTION: "The dprintf() function shall be equivalent
 * to the fprintf() function, except that dprintf() shall write output
 * to the file associated with the file descriptor specified by the
 * fildes argument rather than place output on a stream."
 *
 * "The file associated with the file descriptor" means the open file
 * description, offset and all: a dprintf() has to land where the fd is
 * positioned and leave the fd advanced past what it wrote, so that a
 * raw write() in between interleaves rather than overwrites.  That is
 * the half of the clause a FILE*-shaped implementation could get wrong,
 * and src/stdio/printf.c's vdprintf() does wrap the raw fd in a stack
 * FILE -- so this checks that the wrapper writes through rather than
 * buffering somewhere of its own.  Filesystem-adjacent, so real-Windows
 * CI is the authority. */
static void test_dprintf_fd_path(const char *name)
{
	FILE *f;
	char buf[32];
	int fd;

	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0);
	if (fd < 0) return;
	CHECK(dprintf(fd, "%s", "AA") == 2);
	CHECK(write(fd, "-", 1) == 1);
	CHECK(via_vdprintf(fd, "%s", "BB") == 2);
	CHECK(lseek(fd, 0, SEEK_CUR) == (off_t)5);
	CHECK(close(fd) == 0);

	f = fopen(name, "rb");
	CHECK(f != 0);
	if (!f) return;
	memset(buf, 0, sizeof buf);
	CHECK(fread(buf, 1, sizeof buf - 1, f) == 5);
	CHECK(!strcmp(buf, "AA-BB"));
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
	test_perror();
	test_popen();

	test_snprintf_boundaries();
	test_printf_width_precision();

	name = make_tmp("posix-stdio2-XXXXXX");
	CHECK(name != 0);
	if (name) {
		test_putc_family(name);
		test_fseeko_ftello(name);
		test_flockfile(name);
		test_v_forms(name);
		test_getc_unlocked(name);
		test_tempnam();
		test_printf_output_error(name);
		test_dprintf_fd_path(name);
		/* these two repoint stdin at the temp file and leave it there */
		test_vprintf_vscanf(name);
		test_getchar(name);
		remove(name);
		free(name);
	}
	test_putchar_return();

	if (fails) printf("%d check(s) failed\n", fails);
	else printf("all checks passed\n");
	return fails != 0;
}
