/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * exec.
 *
 * No Windows call replaces the running image, so execve cannot do what
 * it does on Unix.  What it does instead is start the program as a child,
 * wait for it, and exit with its status -- so that to anything watching
 * (a shell running `exec prog`, a parent that will waitpid) the process
 * runs prog and ends when prog ends.  The one visible difference is that
 * the pid changes; nothing else here can be helped.
 *
 * This is the same thing the M2libc Windows port does, and the same
 * thing every from-scratch Unix-on-Windows layer without a personality
 * in the kernel ends up doing.
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include "libc.h"

int execve(const char *path, char *const argv[], char *const envp[])
{
	int pid, status;
	__fd_close_all_cloexec();
	pid = __spawn(path, argv, envp);
	if (pid < 0) return -1;
	if (waitpid(pid, &status, 0) < 0) return -1;
	if (WIFEXITED(status)) exit(WEXITSTATUS(status));
	/* The child died by a signal; this process is standing in for it, so
	 * end the same way and let *our* parent's waitpid see WIFSIGNALED. */
	__stdio_exit();
	__nt_exit(__NT_SIGNAL_EXIT(WTERMSIG(status)));
}

int execv(const char *path, char *const argv[])
{
	return execve(path, argv, __environ);
}

int execvpe(const char *file, char *const argv[], char *const envp[])
{
	char *full;
	int use_path = !strchr(file, '/') && !strchr(file, '\\');
	int r;
	full = __find_program(file, use_path);
	if (!full) { errno = ENOENT; return -1; }
	r = execve(full, argv, envp);
	free(full);
	return r;
}

int execvp(const char *file, char *const argv[])
{
	return execvpe(file, argv, __environ);
}

static char **build_argv(const char *arg0, va_list ap, char ***envout)
{
	size_t cap = 8, n = 0;
	char **v = malloc(cap * sizeof(char *));
	if (!v) return 0;
	v[n++] = (char *)arg0;
	while (v[n-1]) {
		if (n + 1 >= cap) {
			char **nv = realloc(v, (cap *= 2) * sizeof(char *));
			if (!nv) { free(v); return 0; }
			v = nv;
		}
		v[n++] = va_arg(ap, char *);
	}
	if (envout) *envout = va_arg(ap, char **);
	return v;
}

int execl(const char *path, const char *arg0, ...)
{
	va_list ap; char **v; int r;
	va_start(ap, arg0);
	v = build_argv(arg0, ap, 0);
	va_end(ap);
	if (!v) return -1;
	r = execv(path, v);
	free(v);
	return r;
}

int execle(const char *path, const char *arg0, ...)
{
	va_list ap; char **v, **env = 0; int r;
	va_start(ap, arg0);
	v = build_argv(arg0, ap, &env);
	va_end(ap);
	if (!v) return -1;
	r = execve(path, v, env);
	free(v);
	return r;
}

int execlp(const char *file, const char *arg0, ...)
{
	va_list ap; char **v; int r;
	va_start(ap, arg0);
	v = build_argv(arg0, ap, 0);
	va_end(ap);
	if (!v) return -1;
	r = execvp(file, v);
	free(v);
	return r;
}

int fexecve(int fd, char *const argv[], char *const envp[])
{
	char *p = __handle_path(__fd_handle(fd));
	int r;
	if (!p) return -1;
	r = execve(p, argv, envp);
	__free(p);
	return r;
}
