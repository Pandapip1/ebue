/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * exec.
 *
 * No Windows call replaces the running image, so execve cannot do what
 * it does on Unix.  What it does instead is start the program as a child,
 * wait for it, and end with its status -- so that to anything watching
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
#include <sys/stat.h>
#include "libc.h"

int execve(const char *path, char *const argv[], char *const envp[])
{
	int pid, status;
	pid = __spawn(path, argv, envp);
	if (pid < 0) return -1;
	/* Past this point exec has "succeeded": the new program is running
	 * and this process only stands in for it until it ends.  Only now may
	 * the close-on-exec descriptors go.  Closing them before the spawn --
	 * which is what this used to do -- broke the one thing POSIX promises
	 * about a *failed* exec, that the process image is unchanged: a
	 * caller whose execv() returned ENOENT got back a process whose
	 * cloexec fds had already been closed under it.
	 *
	 * Nothing about the child needs them closed first.  A cloexec
	 * descriptor's handle is created without OBJ_INHERIT (src/fcntl/open.c,
	 * src/unistd/dup.c, src/unistd/pipe.c, src/fcntl/fcntl.c), so
	 * RtlCreateUserProcess does not copy it however the flag is set here,
	 * and __fd_runtime_data (src/internal/fd.c) leaves cloexec entries out
	 * of the table the child reads back.  The close is still done, rather
	 * than dropped, because this process outlives the spawn: holding a
	 * file open for the child's whole run would keep a lock or a pending
	 * delete alive that a real exec would have released. */
	__fd_close_all_cloexec();
	if (waitpid(pid, &status, 0) < 0) return -1;
	/* End the way _exit() does, not the way exit() does.  exec.html
	 * DESCRIPTION: "After a successful call to any of the exec functions,
	 * any functions previously registered by the atexit(),
	 * at_quick_exit(), or pthread_atfork() functions are no longer
	 * registered."  A real exec throws the address space away, so nothing
	 * registered before it can run afterwards.  This stand-in keeps the
	 * address space, so calling exit() here ran the *caller's* atexit
	 * handlers at the moment the exec'd program finished.
	 *
	 * That is not cosmetic.  GCC's driver registers delete_temp_files()
	 * with atexit() and then fork()s + execv()s cc1; the forked stand-in
	 * ran that handler when cc1 exited, deleting the driver's own
	 * intermediate .s the instant cc1 had written it, and the "as" step
	 * that followed found nothing there.
	 *
	 * The stdio flush goes for the same reason.  The standard says
	 * nothing about buffered data across exec, so glibc is the oracle:
	 * printf() with no newline followed by execl() prints nothing
	 * (measured, glibc 2.39), because the buffer dies with the image.
	 * Flushing here would emit, after the exec'd program's own output,
	 * bytes a real exec had discarded. */
	if (WIFEXITED(status)) _exit(WEXITSTATUS(status));
	/* The child died by a signal; this process is standing in for it, so
	 * end the same way and let *our* parent's waitpid see WIFSIGNALED. */
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
	char *p;
	struct stat st;
	int r;

	/* exec.html gives fexecve() one shall-fail clause of its own:
	 * "[EBADF] The fd argument is not a valid file descriptor open for
	 *  executing."
	 * Nothing below can produce it.  fexecve() recovers a path from the
	 * descriptor and hands it to execve(), and from there the outcome is
	 * whatever NT makes of that path as a process image -- a directory
	 * descriptor reaches RtlCreateUserProcess (src/process/spawn.c) and
	 * comes back as an image-section failure, whose NTSTATUS is about
	 * the *file* and carries nothing about the descriptor.  Measured on
	 * Windows 11 22621, NtCreateSection(SEC_IMAGE) on a directory handle
	 * is STATUS_INVALID_FILE_FOR_SECTION (0xc0000020), which
	 * src/internal/errno.c does not name, so it reaches
	 * RtlNtStatusToDosError -> ERROR_BAD_EXE_FORMAT (193) ->
	 * __errno_from_doserror()'s default arm -> EIO.  Wine gets there by
	 * another route and produces EBADF, which is why the windows-test
	 * legs are red on this clause and the Wine leg is green.
	 *
	 * So the clause is decided here, where the descriptor still exists,
	 * rather than left to a status that is not about it.  A descriptor
	 * that is not open on a regular file is not "open for executing" --
	 * exec.html's own [EACCES] clause for the path-taking members names
	 * "not a regular file" as the condition -- and that verdict does not
	 * depend on which of NT or Wine is underneath.  Anything else stays
	 * with execve(): a regular file that is not an executable is still
	 * ENOEXEC, not EBADF. */
	if (fstat(fd, &st) < 0) { errno = EBADF; return -1; }
	if (!S_ISREG(st.st_mode)) { errno = EBADF; return -1; }

	p = __handle_path(__fd_handle(fd));
	if (!p) return -1;
	r = execve(p, argv, envp);
	__free(p);
	return r;
}
