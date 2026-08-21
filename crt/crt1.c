/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Program startup.
 *
 * The kernel enters a new process's first thread at ntdll's
 * RtlUserThreadStart, which calls the image's entry point -- this file's
 * _start -- with the PEB as its one argument.  tcc's PE linker names
 * _start as the entry when linking with -nostdlib, so nothing has to be
 * said on the command line.
 *
 * What a C program expects to have been done by the time main runs, and
 * is done here: argv split out of the one command line string Windows
 * hands a process, the environment block turned into environ, the three
 * standard handles turned into descriptors 0, 1 and 2, and exit arranged
 * so that main's return value becomes the process's exit status.
 */
#include <stdlib.h>
#include <string.h>
#include "libc.h"

int main();

/* Defined in src/internal so that crt1.o stays small; declared here
 * because the startup path cannot use the public headers' wchar.h. */
size_t wcslen_(const WCHAR *);

PPEB __peb;
char **environ;
char **__argv;
int __argc;
char *__progname;
char *__progname_full;

/* Split a Windows command line into arguments by the rules every Windows
 * C runtime uses (the ones CommandLineToArgvW implements):
 *
 *   - arguments are separated by spaces or tabs outside quotes;
 *   - a double quote toggles "in quotes", where whitespace is literal;
 *   - 2n backslashes followed by a quote are n backslashes and the quote
 *     is special; 2n+1 backslashes followed by a quote are n backslashes
 *     and a literal quote; backslashes not followed by a quote are literal;
 *   - inside quotes, "" is a literal quote (the post-2008 rule).
 *
 * The first argument (the program name) is special: backslashes are never
 * escapes, and only quotes delimit it.  Done on the UTF-16 string so that
 * the quoting rules see the same code units the shell produced; each
 * argument is then converted to UTF-8. */
static int split_cmdline(const WCHAR *p, size_t n, char ***argvp)
{
	WCHAR *buf = __malloc((n + 1) * sizeof(WCHAR));
	char **argv = __malloc(sizeof(char *) * 2);
	int argc = 0, cap = 2;
	size_t i = 0;

	if (!buf || !argv) return -1;

	/* program name */
	{
		size_t o = 0;
		int inq = 0;
		while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
		while (i < n) {
			if (p[i] == '"') { inq = !inq; i++; continue; }
			if (!inq && (p[i] == ' ' || p[i] == '\t')) break;
			buf[o++] = p[i++];
		}
		argv[argc++] = __utf16_to_utf8(buf, o);
	}

	for (;;) {
		size_t o = 0;
		int inq = 0;
		while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
		if (i >= n) break;
		for (;;) {
			if (i >= n) break;
			if (p[i] == '\\') {
				size_t nb = 0;
				while (i < n && p[i] == '\\') { nb++; i++; }
				if (i < n && p[i] == '"') {
					size_t k;
					for (k = 0; k < nb / 2; k++) buf[o++] = '\\';
					if (nb & 1) { buf[o++] = '"'; i++; }
					/* even: the quote is handled by the loop */
				} else {
					size_t k;
					for (k = 0; k < nb; k++) buf[o++] = '\\';
				}
				continue;
			}
			if (p[i] == '"') {
				if (inq && i + 1 < n && p[i+1] == '"') { buf[o++] = '"'; i += 2; continue; }
				inq = !inq; i++;
				continue;
			}
			if (!inq && (p[i] == ' ' || p[i] == '\t')) break;
			buf[o++] = p[i++];
		}
		if (argc + 1 >= cap) {
			char **nv = __malloc(sizeof(char *) * cap * 2);
			if (!nv) return -1;
			memcpy(nv, argv, sizeof(char *) * argc);
			__free(argv);
			argv = nv;
			cap *= 2;
		}
		argv[argc++] = __utf16_to_utf8(buf, o);
	}
	argv[argc] = 0;
	__free(buf);
	*argvp = argv;
	return argc;
}

/* The environment block is a sequence of NUL-terminated UTF-16 strings
 * ended by an empty one.  Windows keeps some "=C:=C:\dir" entries for
 * per-drive current directories; those are kept too, the way msvcrt and
 * Cygwin keep them, since a child may need them. */
static char **build_environ(const WCHAR *env)
{
	size_t count = 0, i;
	const WCHAR *p;
	char **ev;

	if (!env) {
		ev = __malloc(sizeof(char *));
		if (ev) ev[0] = 0;
		return ev;
	}
	for (p = env; *p; p += wcslen_(p) + 1) count++;
	ev = __malloc(sizeof(char *) * (count + 1));
	if (!ev) return 0;
	for (p = env, i = 0; *p; p += wcslen_(p) + 1)
		ev[i++] = __utf16_to_utf8(p, wcslen_(p));
	ev[i] = 0;
	return ev;
}

void __libc_start_main(void)
{
	PRTL_USER_PROCESS_PARAMETERS pp;
	int rc;

	__peb = RtlGetCurrentPeb();
	pp = __peb->ProcessParameters;

	__argc = split_cmdline(pp->CommandLine.Buffer, pp->CommandLine.Length / sizeof(WCHAR), &__argv);
	if (__argc < 0) NtTerminateProcess(NtCurrentProcess(), STATUS_NO_MEMORY);
	__progname = __argv[0];
	__progname_full = __utf16_to_utf8(pp->ImagePathName.Buffer, pp->ImagePathName.Length / sizeof(WCHAR));
	__environ = build_environ(pp->Environment);
	if (!__environ) NtTerminateProcess(NtCurrentProcess(), STATUS_NO_MEMORY);

	__fd_init();
	__signal_init();

	rc = main(__argc, __argv, __environ);
	exit(rc);
}

void _start(void *peb_unused)
{
	(void)peb_unused;
	__libc_start_main();
}
