/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ntstubs.c -- the ntdll side of the world, for native (Linux) builds.
 *
 * The point of this file is to let the *real* src/*.c be compiled and
 * linked by a native clang with ASan/UBSan/libFuzzer.  Nothing here is
 * part of ntlibc: it stands in for ntdll.dll, which is the one thing a
 * native build cannot have.  Everything ntlibc itself computes -- format
 * conversion, number parsing, buffer sizing -- runs unmodified.
 *
 * Three grades of stub live here:
 *
 *   real       RtlAllocateHeap & friends (ASan's allocator, so ntlibc's
 *              heap use is redzone-checked), NtWriteFile/NtReadFile
 *              (raw Linux write/read so tests can talk), the clocks,
 *              RtlUTF8ToUnicodeN/RtlUnicodeToUTF8N (a from-spec
 *              conversion; see the note above them), RtlInitUnicodeString.
 *   plausible  NtQueryVolumeInformationFile / NtQueryInformationFile,
 *              enough for __handle_type() to classify fds 0-2.
 *   refusing   everything else: STATUS_NOT_IMPLEMENTED.  Any ntlibc code
 *              path that reaches one of those is simply not covered by
 *              the native build, and will report an error rather than
 *              pretend to work.
 *
 * Host services are reached through syscall(2) rather than through
 * write()/read()/malloc(), because those names belong to ntlibc in this
 * link and calling them would recurse straight back into the library.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "libc.h"

extern long syscall(long, ...);
extern void *__interceptor_malloc(size_t);
extern void __interceptor_free(void *);
extern void *__interceptor_realloc(void *, size_t);
extern size_t __sanitizer_get_allocated_size(const void *);

#ifndef STATUS_SOME_NOT_MAPPED
#define STATUS_SOME_NOT_MAPPED ((NTSTATUS)0x80000005L)
#endif
#ifndef STATUS_END_OF_FILE
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011L)
#endif

#define SYS_read  0
#define SYS_write 1
#define SYS_exit_group 231
#define SYS_clock_gettime 228
#define SYS_nanosleep 35

/* Handles are (fd + 1), so that 0 stays "no handle". */
#define H2FD(h) ((int)(long)(h) - 1)
#define FD2H(f) ((HANDLE)(long)((f) + 1))

/* ------------------------------------------------------------------ heap */

PVOID NTAPI RtlAllocateHeap(PVOID heap, ULONG flags, SIZE_T n)
{
	void *p;
	(void)heap;
	p = __interceptor_malloc(n ? n : 1);
	if (p && (flags & HEAP_ZERO_MEMORY)) memset(p, 0, n);
	return p;
}

BOOLEAN NTAPI RtlFreeHeap(PVOID heap, ULONG flags, PVOID p)
{
	(void)heap; (void)flags;
	__interceptor_free(p);
	return 1;
}

PVOID NTAPI RtlReAllocateHeap(PVOID heap, ULONG flags, PVOID p, SIZE_T n)
{
	(void)heap; (void)flags;
	return __interceptor_realloc(p, n ? n : 1);
}

SIZE_T NTAPI RtlSizeHeap(PVOID heap, ULONG flags, PVOID p)
{
	(void)heap; (void)flags;
	return p ? __sanitizer_get_allocated_size(p) : 0;
}

PVOID NTAPI RtlCreateHeap(ULONG a, PVOID b, SIZE_T c, SIZE_T d, PVOID e, PVOID f)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
	return (PVOID)(long)0x1000;
}

/* ------------------------------------------------------- the PEB and TEB */

static RTL_USER_PROCESS_PARAMETERS shim_pp;
static PEB shim_peb;
static char shim_teb[4096];
PPEB __peb = &shim_peb;

PTEB __teb(void)
{
	/* On NT this is gs:0x30.  Natively there is no TEB, and the only
	 * thing ntlibc reads out of it is the last-error slot, so a plain
	 * zeroed block is enough. */
	return (PTEB)shim_teb;
}

PPEB NTAPI RtlGetCurrentPeb(void) { return &shim_peb; }

/*
 * Natively there is no crt1.o: glibc's startup calls main() directly, so
 * the parts of __libc_start_main() that ntlibc code depends on have to
 * happen in a constructor instead.  ASan's own initialisation runs at
 * priority 1, ahead of this.
 */
char **__argv;
int __argc;
char *__progname;
char *__progname_full;
static char *shim_argv[2] = { (char *)"ntlibc-native", 0 };


__attribute__((constructor(200))) void __ntshim_init(void)
{
	shim_peb.ProcessHeap = (PVOID)(long)0x1000;
	shim_peb.ProcessParameters = &shim_pp;
	shim_pp.StandardInput  = FD2H(0);
	shim_pp.StandardOutput = FD2H(1);
	shim_pp.StandardError  = FD2H(2);
	__argc = 1;
	__argv = shim_argv;
	__progname = shim_argv[0];
	__progname_full = shim_argv[0];
	/* setenv()/putenv() realloc environ, so it has to start out on the
	 * heap the same way crt1.c's build_environ() leaves it. */
	environ = __interceptor_malloc(sizeof(char *));
	environ[0] = 0;
	__fd_init();

	/* Nothing calls ntlibc's exit() in a native build -- glibc's start-up
	 * calls main() and glibc's exit() ends it -- so __stdio_exit() never
	 * runs and anything left in a FILE buffer is lost.  libFuzzer's own
	 * diagnostics go through these two (its fprintf/stderr references bind
	 * to ntlibc's, which are the definitions in this executable), so they
	 * would vanish.  Unbuffered costs nothing here and loses nothing. */
	setvbuf(stdout, 0, _IONBF, 0);
	setvbuf(stderr, 0, _IONBF, 0);
}

/* -------------------------------------------------------------- file I/O */

NTSTATUS NTAPI NtWriteFile(HANDLE h, HANDLE ev, PIO_APC_ROUTINE apc, PVOID ctx,
                           PIO_STATUS_BLOCK io, const void *buf, ULONG len,
                           LARGE_INTEGER *off, PULONG key)
{
	long n;
	(void)ev; (void)apc; (void)ctx; (void)off; (void)key;
	if (H2FD(h) < 0) return STATUS_INVALID_HANDLE;
	n = syscall(SYS_write, H2FD(h), buf, (size_t)len);
	if (n < 0) return STATUS_INVALID_DEVICE_REQUEST;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = (ULONG_PTR)n; }
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtReadFile(HANDLE h, HANDLE ev, PIO_APC_ROUTINE apc, PVOID ctx,
                          PIO_STATUS_BLOCK io, PVOID buf, ULONG len,
                          LARGE_INTEGER *off, PULONG key)
{
	long n;
	(void)ev; (void)apc; (void)ctx; (void)off; (void)key;
	if (H2FD(h) < 0) return STATUS_INVALID_HANDLE;
	n = syscall(SYS_read, H2FD(h), buf, (size_t)len);
	if (n < 0) return STATUS_INVALID_DEVICE_REQUEST;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = (ULONG_PTR)n; }
	if (n == 0) return STATUS_END_OF_FILE;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE h, PIO_STATUS_BLOCK io, PVOID buf,
                                            ULONG len, FS_INFORMATION_CLASS cls)
{
	FILE_FS_DEVICE_INFORMATION *d = buf;
	(void)io;
	if (cls != FileFsDeviceInformation || len < sizeof *d) return STATUS_INVALID_INFO_CLASS;
	if (H2FD(h) < 0) return STATUS_INVALID_HANDLE;
	/* stdin/stdout/stderr of a native test run are pipes or ttys; call
	 * them character devices, which is the conservative answer (no
	 * seeking, no directory). */
	d->DeviceType = FILE_DEVICE_NULL;
	d->Characteristics = 0;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryInformationFile(HANDLE h, PIO_STATUS_BLOCK io, PVOID buf,
                                      ULONG len, FILE_INFORMATION_CLASS cls)
{
	(void)h; (void)io; (void)buf; (void)len; (void)cls;
	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NTAPI NtClose(HANDLE h) { (void)h; return STATUS_SUCCESS; }

NTSTATUS NTAPI NtFlushBuffersFile(HANDLE h, PIO_STATUS_BLOCK io)
{
	(void)h; (void)io; return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- clocks */

NTSTATUS NTAPI NtQuerySystemTime(LARGE_INTEGER *t)
{
	struct { long sec, nsec; } ts = { 0, 0 };
	syscall(SYS_clock_gettime, 0 /*CLOCK_REALTIME*/, &ts);
	/* 100ns ticks since 1601-01-01; 11644473600s from then to the epoch. */
	*t = (long long)(ts.sec + 11644473600LL) * 10000000LL + ts.nsec / 100;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryPerformanceCounter(LARGE_INTEGER *c, LARGE_INTEGER *f)
{
	struct { long sec, nsec; } ts = { 0, 0 };
	syscall(SYS_clock_gettime, 1 /*CLOCK_MONOTONIC*/, &ts);
	*c = (long long)ts.sec * 10000000LL + ts.nsec / 100;
	if (f) *f = 10000000LL;
	return STATUS_SUCCESS;
}

/*
 * This one has to really sleep.  libFuzzer runs a watchdog thread that
 * loops on sleep(1); returning immediately turns that into a spin that
 * starves the fuzzing thread -- the symptom is three executions in ninety
 * seconds.
 */
NTSTATUS NTAPI NtDelayExecution(BOOLEAN alertable, LARGE_INTEGER *t)
{
	struct { long sec, nsec; } ts;
	long long ticks;
	(void)alertable;
	if (!t) return STATUS_SUCCESS;
	ticks = *t;
	if (ticks >= 0) return STATUS_SUCCESS;   /* absolute time: not supported */
	ticks = -ticks;                          /* relative, in 100ns units */
	ts.sec = (long)(ticks / 10000000LL);
	ts.nsec = (long)((ticks % 10000000LL) * 100);
	syscall(SYS_nanosleep, &ts, (void *)0);
	return STATUS_SUCCESS;
}

/* --------------------------------------------------------------- process */

NTSTATUS NTAPI NtTerminateProcess(HANDLE h, NTSTATUS code)
{
	(void)h;
	syscall(SYS_exit_group, (long)(int)code);
	return STATUS_SUCCESS;
}

/* --------------------------------------------------------------- strings */

void NTAPI RtlInitUnicodeString(PUNICODE_STRING s, PCWSTR src)
{
	size_t n = 0;
	if (src) while (src[n]) n++;
	s->Length = (USHORT)(n * sizeof(WCHAR));
	s->MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
	s->Buffer = (PWSTR)src;
}

ULONG NTAPI RtlNtStatusToDosError(NTSTATUS st) { return (ULONG)st & 0xffff; }

/*
 * RtlUTF8ToUnicodeN / RtlUnicodeToUTF8N.
 *
 * These two are ntdll's, not ntlibc's -- src/internal/utf.c is a wrapper
 * around them.  Written from the documented behaviour (malformed input is
 * replaced with U+FFFD and reported as STATUS_SOME_NOT_MAPPED; a short
 * destination is filled as far as it goes and reported as
 * STATUS_BUFFER_TOO_SMALL), so that what the utf harness actually
 * exercises is utf.c's *buffer sizing*: the "UTF-16 is never longer in
 * code units than UTF-8 is in bytes" and "at most 3 bytes per code unit"
 * claims it allocates on.  It does not exercise ntdll's converter, and is
 * not evidence about it.
 */
#define REPL 0xFFFDu

NTSTATUS NTAPI RtlUTF8ToUnicodeN(PWSTR dst, ULONG dstbytes, PULONG written,
                                 const char *src, ULONG srcbytes)
{
	const unsigned char *s = (const unsigned char *)src, *end = s + srcbytes;
	ULONG out = 0;
	int lossy = 0, full = 0;

	if (!src) return STATUS_INVALID_PARAMETER;
	while (s < end) {
		unsigned int cp;
		int extra, i;
		unsigned char c = *s++;

		if (c < 0x80)                { cp = c; extra = 0; }
		else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
		else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
		else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
		else                         { cp = REPL; extra = 0; lossy = 1; }

		for (i = 0; i < extra; i++) {
			if (s >= end || (*s & 0xc0) != 0x80) { cp = REPL; extra = -1; lossy = 1; break; }
			cp = (cp << 6) | (*s++ & 0x3f);
		}
		if (extra > 0) {
			/* overlong, surrogate, or out of range */
			if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
			    (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
			    (cp >= 0xD800 && cp <= 0xDFFF)) { cp = REPL; lossy = 1; }
		}

		if (cp >= 0x10000) {
			if (dst && out + 2 * sizeof(WCHAR) > dstbytes) { full = 1; break; }
			if (dst) {
				dst[out / sizeof(WCHAR)]     = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
				dst[out / sizeof(WCHAR) + 1] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3ff));
			}
			out += 2 * sizeof(WCHAR);
		} else {
			if (dst && out + sizeof(WCHAR) > dstbytes) { full = 1; break; }
			if (dst) dst[out / sizeof(WCHAR)] = (WCHAR)cp;
			out += sizeof(WCHAR);
		}
	}
	if (written) *written = out;
	if (full) return STATUS_BUFFER_TOO_SMALL;
	return lossy ? STATUS_SOME_NOT_MAPPED : STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUnicodeToUTF8N(char *dst, ULONG dstbytes, PULONG written,
                                 PCWSTR src, ULONG srcbytes)
{
	ULONG i, n = srcbytes / sizeof(WCHAR), out = 0;
	int lossy = 0, full = 0;

	if (!src) return STATUS_INVALID_PARAMETER;
	if (srcbytes % sizeof(WCHAR)) return STATUS_INVALID_PARAMETER;
	for (i = 0; i < n; i++) {
		unsigned int cp = src[i];
		int len;
		unsigned char buf[4];

		if (cp >= 0xD800 && cp <= 0xDBFF) {
			if (i + 1 < n && src[i+1] >= 0xDC00 && src[i+1] <= 0xDFFF)
				cp = 0x10000 + ((cp - 0xD800) << 10) + (src[++i] - 0xDC00);
			else { cp = REPL; lossy = 1; }
		} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
			cp = REPL; lossy = 1;
		}

		if (cp < 0x80)          { len = 1; buf[0] = (unsigned char)cp; }
		else if (cp < 0x800)    { len = 2; buf[0] = (unsigned char)(0xc0 | (cp >> 6));
		                                    buf[1] = (unsigned char)(0x80 | (cp & 0x3f)); }
		else if (cp < 0x10000)  { len = 3; buf[0] = (unsigned char)(0xe0 | (cp >> 12));
		                                    buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
		                                    buf[2] = (unsigned char)(0x80 | (cp & 0x3f)); }
		else                    { len = 4; buf[0] = (unsigned char)(0xf0 | (cp >> 18));
		                                    buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
		                                    buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
		                                    buf[3] = (unsigned char)(0x80 | (cp & 0x3f)); }

		if (dst && out + (ULONG)len > dstbytes) { full = 1; break; }
		if (dst) memcpy(dst + out, buf, (size_t)len);
		out += (ULONG)len;
	}
	if (written) *written = out;
	if (full) return STATUS_BUFFER_TOO_SMALL;
	return lossy ? STATUS_SOME_NOT_MAPPED : STATUS_SUCCESS;
}

/*
 * libFuzzer is built with _FORTIFY_SOURCE, so its Printf() calls
 * __vfprintf_chk rather than vfprintf.  Its FILE* is ntlibc's stderr (the
 * only stderr in this executable), but __vfprintf_chk would come from
 * glibc and would read that pointer as a glibc FILE -- so every diagnostic
 * libFuzzer prints, and every crash artefact it announces, silently
 * vanished.  Routing the checked forms back to ntlibc's own stdio is what
 * makes the fuzzer able to talk.
 */
int __vfprintf_chk(FILE *f, int flag, const char *fmt, __builtin_va_list ap)
{
	(void)flag;
	return vfprintf(f, fmt, ap);
}

int __fprintf_chk(FILE *f, int flag, const char *fmt, ...)
{
	__builtin_va_list ap;
	int r;
	(void)flag;
	__builtin_va_start(ap, fmt);
	r = vfprintf(f, fmt, ap);
	__builtin_va_end(ap);
	return r;
}

int __printf_chk(int flag, const char *fmt, ...)
{
	__builtin_va_list ap;
	int r;
	(void)flag;
	__builtin_va_start(ap, fmt);
	r = vfprintf(stdout, fmt, ap);
	__builtin_va_end(ap);
	return r;
}

int __snprintf_chk(char *b, size_t n, int flag, size_t slen, const char *fmt, ...)
{
	__builtin_va_list ap;
	int r;
	(void)flag; (void)slen;
	__builtin_va_start(ap, fmt);
	r = vsnprintf(b, n, fmt, ap);
	__builtin_va_end(ap);
	return r;
}

void *__memcpy_chk(void *d, const void *s, size_t n, size_t dlen)
{
	(void)dlen;
	return memcpy(d, s, n);
}

/* ------------------------------------------------- everything not native */

#define NOTIMPL(name, proto) NTSTATUS NTAPI name proto { return STATUS_NOT_IMPLEMENTED; }

NOTIMPL(NtCreateFile, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c, PIO_STATUS_BLOCK d,
                       LARGE_INTEGER *e, ULONG f, ULONG g, ULONG h, ULONG i, PVOID j, ULONG k))
NOTIMPL(NtOpenFile, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c, PIO_STATUS_BLOCK d,
                     ULONG e, ULONG f))
NOTIMPL(NtSetInformationFile, (HANDLE a, PIO_STATUS_BLOCK b, PVOID c, ULONG d,
                               FILE_INFORMATION_CLASS e))
NOTIMPL(NtQueryDirectoryFile, (HANDLE a, HANDLE b, PIO_APC_ROUTINE c, PVOID d,
                               PIO_STATUS_BLOCK e, PVOID f, ULONG g,
                               FILE_INFORMATION_CLASS h, BOOLEAN i, PUNICODE_STRING j, BOOLEAN k))
NOTIMPL(NtQueryFullAttributesFile, (POBJECT_ATTRIBUTES a, FILE_NETWORK_OPEN_INFORMATION *b))
NOTIMPL(NtFsControlFile, (HANDLE a, HANDLE b, PIO_APC_ROUTINE c, PVOID d, PIO_STATUS_BLOCK e,
                          ULONG f, PVOID g, ULONG h, PVOID i, ULONG j))
NOTIMPL(NtCreateNamedPipeFile, (PHANDLE a, ULONG b, POBJECT_ATTRIBUTES c, PIO_STATUS_BLOCK d,
                                ULONG e, ULONG f, ULONG g, ULONG h, ULONG i, ULONG j,
                                ULONG k, ULONG l, ULONG m, LARGE_INTEGER *n))
NOTIMPL(NtDuplicateObject, (HANDLE a, HANDLE b, HANDLE c, PHANDLE d, ACCESS_MASK e,
                            ULONG f, ULONG g))
NOTIMPL(NtOpenProcess, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c, PCLIENT_ID d))
NOTIMPL(NtOpenSymbolicLinkObject, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c))
NOTIMPL(NtQuerySymbolicLinkObject, (HANDLE a, PUNICODE_STRING b, PULONG c))
NOTIMPL(NtQueryInformationProcess, (HANDLE a, PROCESSINFOCLASS b, PVOID c, ULONG d, PULONG e))
NOTIMPL(NtQueryObject, (HANDLE a, ULONG b, PVOID c, ULONG d, PULONG e))
NOTIMPL(NtQuerySystemInformation, (SYSTEM_INFORMATION_CLASS a, PVOID b, ULONG c, PULONG d))
NOTIMPL(NtSetSystemTime, (LARGE_INTEGER *a, LARGE_INTEGER *b))
NOTIMPL(NtWaitForSingleObject, (HANDLE a, BOOLEAN b, LARGE_INTEGER *c))
NOTIMPL(NtResumeThread, (HANDLE a, PULONG b))
NOTIMPL(RtlDosPathNameToNtPathName_U_WithStatus, (PCWSTR a, PUNICODE_STRING b, PCWSTR *c, PVOID d))
NOTIMPL(RtlSetCurrentDirectory_U, (PUNICODE_STRING a))
NOTIMPL(RtlCreateProcessParametersEx, (PRTL_USER_PROCESS_PARAMETERS *a, PUNICODE_STRING b,
                                       PUNICODE_STRING c, PUNICODE_STRING d, PUNICODE_STRING e,
                                       PVOID f, PUNICODE_STRING g, PUNICODE_STRING h,
                                       PUNICODE_STRING i, PUNICODE_STRING j, ULONG k))
NOTIMPL(RtlDestroyProcessParameters, (PRTL_USER_PROCESS_PARAMETERS a))
NOTIMPL(RtlCreateUserProcess, (PUNICODE_STRING a, ULONG b, PRTL_USER_PROCESS_PARAMETERS c,
                               PVOID d, PVOID e, HANDLE f, BOOLEAN g, HANDLE h, HANDLE i,
                               RTL_USER_PROCESS_INFORMATION *j))
NOTIMPL(RtlCloneUserProcess, (ULONG a, PVOID b, PVOID c, HANDLE d,
                              RTL_USER_PROCESS_INFORMATION *e))

ULONG NTAPI RtlGetCurrentDirectory_U(ULONG len, PWSTR buf)
{
	if (len < 3 * sizeof(WCHAR)) return 0;
	buf[0] = 'C'; buf[1] = ':'; buf[2] = 0;
	return 2 * sizeof(WCHAR);
}

PVOID NTAPI RtlAddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h)
{
	(void)first; (void)h;
	return (PVOID)(long)1;
}
