/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A *measurement*, not an assertion.  This file checks nothing and can
 * never fail: it prints raw NTSTATUS/field values from two NT
 * behaviours that no amount of source reading can settle, and exits 77
 * ("ran, but verified nothing new" -- tools/runtests.sh's third bucket,
 * the same convention test/posix-socket.c uses for an environment gap).
 *
 * 77 is deliberate and is not a fallback.  runtests.sh prints a test's
 * log only for the 77 and FAIL buckets -- a PASS is reported as one
 * word with its output discarded -- so 77 is the only exit code that
 * gets these numbers off CI's real-Windows legs and into the run
 * summary where a human can read them, without turning the board red.
 * Everything printed below therefore has to fit in the last 40 lines of
 * the log, which is what runtests.sh tails.
 *
 * Neither probe forks, so this is not a *-win.c file and it runs under
 * Wine too (Makefile's TEST_RUN filters out %-win.exe).  Wine's answers
 * are a smoke test that the probe itself works; they are *not* the
 * answer to either question, because both questions are precisely
 * "what does real ntdll/afd.sys do, where Wine and ReactOS are only
 * reimplementations."
 *
 * ---- probe 1: RtlSetCurrentDirectory_U on four path shapes ----
 *
 * chdir("regularfile/below") returns ENOENT where POSIX wants ENOTDIR
 * (src/unistd/chdir.c maps whatever RtlSetCurrentDirectory_U returns
 * through __set_errno_status()).  A source audit found NT's path parser
 * distinguishes final from non-final component rather than
 * file-from-missing, so "regularfile/below" and "missingdir/below" may
 * both yield STATUS_OBJECT_PATH_NOT_FOUND, in which case no blanket
 * remap of that status can be right.  Wine's own conformance tests
 * pin the file-as-prefix case on real Windows
 * (dlls/ntdll/tests/file.c:417-424, no todo_wine/broken()), but
 * *nothing* pins the missing-intermediate case there.  If real NT
 * separates the two, the recommended fix flips entirely.  So: measure
 * all four shapes and print the raw statuses.
 *
 * Case c doubles as the second question.  Both Wine and ReactOS pass
 * NtOpenFile's status through RtlSetCurrentDirectory_U unmodified;
 * nobody has disassembled real ntdll to check.  If c is not
 * STATUS_NOT_A_DIRECTORY, real ntdll rewrites statuses and even the
 * case ntlibc gets right today is an accident.
 *
 * ---- probe 2: the IOCTL_AFD_SELECT shared in/out buffer ----
 *
 * src/select/select.c's __fd_probe() passes one buffer as both input
 * and output to IOCTL_AFD_SELECT (METHOD_BUFFERED, so afd.sys writes
 * its reply over the request).  If the driver writes back only the
 * handles that actually fired, a zero-event poll leaves the *requested*
 * mask sitting in the buffer, __afd_poll_get_events() reads it back,
 * and select()/poll() report a never-connected socket ready -- an
 * always-ready bug living inside the fix for an always-ready bug.
 *
 * The request is built with src/socket/afdsupport.c's own
 * __afd_build_poll_request()/__afd_poll_set_handle() and issued through
 * its own __afd_ioctl(), exactly as __fd_probe() does it, so this
 * measures what the library actually sends rather than a hand-rolled
 * lookalike.  Events are pre-set to 0x1ff (all nine AFD_EVENT_* bits)
 * so a read-back of the input mask is unmistakable.
 *
 * Probe 2 may find no working device under it: test/posix-socket.c's
 * banner documents an environment whose Wine accepts a portably-opened
 * AFD handle and then refuses the first real ioctl on it, because such
 * a handle is never routed to Wine's own AFD implementation.  If that
 * happens the ioctl status is simply printed as measured and the run
 * continues -- there is nothing to degrade, because nothing here is an
 * assertion.  (Observed at the time of writing: the Wine build in
 * CONTRIBUTING.md's environment does answer IOCTL_AFD_SELECT on such a
 * handle, st=0, so probe 2 does produce numbers there.  They are still
 * not the answer; afd.sys is the thing being asked about.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Hand-declared for the same reason test/posix-signal.c hand-declares
 * NtAllocateVirtualMemory: test/ is not on the -I path for
 * src/internal/nt.h or src/internal/afd.h, and these need real ntdll
 * and libc-internal prototypes that are part of no public header.  The
 * spellings match those two headers exactly. */
typedef int NTSTATUS;
typedef void *HANDLE;
typedef unsigned long ULONG;
typedef unsigned short USHORT;
typedef unsigned short WCHAR;
#ifdef __i386__
#define NTAPI __attribute__((stdcall))
typedef unsigned long ULONG_PTR;
#else
#define NTAPI
typedef unsigned long long ULONG_PTR;
#endif
typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	WCHAR *Buffer;
} UNICODE_STRING;
typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS Status;
		void *Pointer;
	};
	ULONG_PTR Information;
} IO_STATUS_BLOCK;

NTSTATUS NTAPI RtlSetCurrentDirectory_U(UNICODE_STRING *);

/* src/internal/afd.h: _AFD_CONTROL_CODE(AFD_SELECT = 9, METHOD_BUFFERED). */
#define IOCTL_AFD_SELECT 0x12024UL
/* All nine AFD_EVENT_* bits, so a read-back of the request is obvious. */
#define AFD_ALL_EVENTS 0x1ffUL
/* Reply's handle count: AFD_POLL_REQ_OFF_HANDLE_COUNT. */
#define AFD_POLL_REQ_OFF_HANDLE_COUNT ((size_t)8)

unsigned long __afd_poll_request_size(unsigned long nhandles);
void __afd_build_poll_request(void *buf, long long timeout, unsigned long nhandles);
void __afd_poll_set_handle(void *buf, unsigned long i, HANDLE h, uint32_t events);
uint32_t __afd_poll_get_events(const void *buf, unsigned long i);
NTSTATUS __afd_ioctl(HANDLE h, ULONG code, void *in, ULONG inlen,
                     void *out, ULONG outlen, IO_STATUS_BLOCK *io_out);
HANDLE __fd_handle(int fd);

/* ------------------------------------------------------------------ */

/* ASCII -> UNICODE_STRING over caller storage.  Returns 0 if the path
 * is not pure ASCII or does not fit; probe 1 then says so rather than
 * printing a number measured on some other string. */
static int us_from_ascii(UNICODE_STRING *us, WCHAR *buf, size_t cap, const char *s)
{
	size_t i, n = strlen(s);
	if (n + 1 > cap || n * sizeof(WCHAR) > 0xfffeu) return 0;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 0x80) return 0;
		buf[i] = (WCHAR)c;
	}
	buf[n] = 0;
	us->Buffer = buf;
	us->Length = (USHORT)(n * sizeof(WCHAR));
	us->MaximumLength = (USHORT)(us->Length + sizeof(WCHAR));
	return 1;
}

static unsigned long setcwd_status(const char *path)
{
	static WCHAR wbuf[4096];
	UNICODE_STRING us;
	if (!us_from_ascii(&us, wbuf, sizeof wbuf / sizeof wbuf[0], path)) return 0xffffffffUL;
	return (unsigned long)(uint32_t)RtlSetCurrentDirectory_U(&us);
}

static void probe1(void)
{
	char cwd[2048], p[4][2100];
	unsigned long st[4];
	int fd, i;
	static const char *label[4] = {
		"a file-as-prefix   ", "b missing-as-prefix",
		"c file-as-target   ", "d missing-as-target"
	};

	if (!getcwd(cwd, sizeof cwd)) {
		printf("PROBE1 SETUP-FAIL getcwd errno=%d\n", errno);
		return;
	}
	fd = open("probe.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		printf("PROBE1 SETUP-FAIL open(probe.txt) errno=%d\n", errno);
		return;
	}
	close(fd);
	/* "nodir" must not exist; the run dir is a fresh mktemp -d, but be
	 * explicit rather than assume it. */
	rmdir("nodir");
	unlink("nodir");
	/* getcwd() hands back a DOS path with '/' separators (Wine's
	 * Z:/tmp/..., and src/unistd/getcwd.c does the same conversion on
	 * real Windows).  RtlSetCurrentDirectory_U's parser accepts both,
	 * but this measurement must not rest on that: hand it the
	 * all-backslash form every Win32 caller sends, so that what is
	 * measured is the component-vs-component distinction and nothing
	 * else. */
	for (i = 0; cwd[i]; i++) if (cwd[i] == '/') cwd[i] = '\\';

	snprintf(p[0], sizeof p[0], "%s\\probe.txt\\sub", cwd);
	snprintf(p[1], sizeof p[1], "%s\\nodir\\sub", cwd);
	snprintf(p[2], sizeof p[2], "%s\\probe.txt", cwd);
	snprintf(p[3], sizeof p[3], "%s\\nodir", cwd);

	for (i = 0; i < 4; i++) st[i] = setcwd_status(p[i]);
	/* None of the four is expected to succeed, but a success would
	 * silently move the run dir out from under probe 2, so put it back
	 * unconditionally. */
	chdir(cwd);

	printf("PROBE1 RtlSetCurrentDirectory_U raw NTSTATUS, base=%s\n", cwd);
	for (i = 0; i < 4; i++)
		printf("PROBE1   %s = 0x%08lx\n", label[i], st[i]);
	printf("PROBE1 legend (0xffffffff = path not measurable, see us_from_ascii)\n");
	printf("PROBE1   a==b==0xC000003A            -> pre-probe required (expected)\n");
	printf("PROBE1   a==0xC000003A, b==0xC0000034 -> overturns it: split the errno arm\n");
	printf("PROBE1   a==0xC0000103               -> NT stricter than Wine+ReactOS: no probe\n");
	printf("PROBE1   c!=0xC0000103               -> ntdll rewrites NtOpenFile statuses\n");
	unlink("probe.txt");
}

static void probe2(void)
{
	/* Pointer-aligned storage, at least __afd_poll_request_size(1);
	 * the long long member is what forces the alignment __afd_* wants. */
	union { long long align; unsigned char b[64]; } req;
	IO_STATUS_BLOCK io;
	unsigned long len;
	uint32_t count, events;
	NTSTATUS st;
	HANDLE h;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		printf("PROBE2 SETUP-FAIL socket() errno=%d -- nothing measured\n", errno);
		return;
	}
	h = __fd_handle(fd);
	if (!h) {
		printf("PROBE2 SETUP-FAIL __fd_handle errno=%d -- nothing measured\n", errno);
		close(fd);
		return;
	}

	len = __afd_poll_request_size(1);
	memset(&io, 0, sizeof io);
	/* Exactly __fd_probe()'s sequence: Timeout 0, one handle, one
	 * buffer handed in as both input and output. */
	__afd_build_poll_request(&req, 0, 1);
	__afd_poll_set_handle(&req, 0, h, (uint32_t)AFD_ALL_EVENTS);
	st = __afd_ioctl(h, IOCTL_AFD_SELECT, &req, (ULONG)len, &req, (ULONG)len, &io);

	memcpy(&count, req.b + AFD_POLL_REQ_OFF_HANDLE_COUNT, sizeof count);
	events = __afd_poll_get_events(&req, 0);
	printf("PROBE2 IOCTL_AFD_SELECT shared in/out buf, Timeout=0, Events=0x%03lx, never-connected TCP\n",
	       AFD_ALL_EVENTS);
	printf("PROBE2   st=0x%08lx info=%lu count=%lu events=0x%08lx\n",
	       (unsigned long)(uint32_t)st, (unsigned long)io.Information,
	       (unsigned long)count, (unsigned long)events);
	printf("PROBE2 legend (st!=0 -> the ioctl was refused and count/events mean nothing)\n");
	printf("PROBE2   events=0x000001ff -> request bytes read back: stale-buffer concern is real\n");
	printf("PROBE2   events=0x00000000 -> concern unfounded, only the count check is worth keeping\n");
	close(fd);
}

int main(void)
{
	printf("nt-behavior-probe: measurement only, asserts nothing, always rc=77\n");
	probe1();
	probe2();
	printf("NOTE Wine's values here are a smoke test only. The real-Windows CI legs\n");
	printf("NOTE are the authoritative answer for both probes.\n");
	fflush(stdout);
	return 77;
}
