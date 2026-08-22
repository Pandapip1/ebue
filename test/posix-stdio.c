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
#include <sys/wait.h>

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

	if (fails) printf("%d check(s) failed\n", fails);
	else printf("all checks passed\n");
	return fails != 0;
}
