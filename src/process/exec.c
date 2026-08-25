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

/* exec.html DESCRIPTION: "In the cases where the other members of the
 * exec family of functions would fail and set errno to [ENOEXEC], the
 * execlp() and execvp() functions shall execute a command interpreter
 * and the environment of the executed command shall be as if the
 * process invoked the sh utility using execl() as follows:
 *
 *     execl(<shell path>, arg0, file, arg1, ..., (char *)0);
 *
 * where <shell path> is an unspecified pathname for the sh utility,
 * file is the process image file, and for execvp(), where arg0, arg1,
 * and so on correspond to the values passed to execvp() in argv[0],
 * argv[1], and so on."
 *
 * which is why the same page's [ENOEXEC] entry is scoped "The exec
 * functions, *except for execlp() and execvp()*, shall fail if" -- for
 * these two it is not an error at all.  APPLICATION USAGE says the same
 * thing from the other side: "When the execlp() and execvp() functions
 * encounter such a file, they assume the file to be a shell script and
 * invoke a known command interpreter to interpret such files.  This is
 * now required by POSIX.1-2017."
 *
 * Which sh, and why: src/process/interpreter.c.  It is a second image
 * rather than a call into the shell engine linked into this same
 * libc.a, and that is the one real decision here, because
 * test/sh-design.md's "reuse rule" says the shell-specified interfaces
 * "call those functions directly and never spawn an external
 * interpreter".  Why that rule does not reach this case:
 *
 *  - This clause is a process *replacement*, not a string to interpret.
 *    wordexp()'s substitution must hand its result back, so it has to
 *    run in the caller's process; a successful exec has no caller left
 *    to return to.  Running the script inside the caller's address
 *    space would leave the old image underneath the new one -- its
 *    atexit handlers, its signal dispositions, its heap -- which is the
 *    one thing exec.html says a successful exec does not do.
 *  - The interpreter contract is bigger than the engine.  sh(1p)'s $0,
 *    its positional parameters, its exit statuses, and above all
 *    sh/main.c's up-front refusal of everything the engine would
 *    otherwise *misread* rather than diagnose (`case`, the special
 *    parameters that are still literal) live in that main(), not in
 *    libc.a.  Calling __sh_parse()/__sh_exec_list() from here means
 *    either duplicating that check or running a program named "case"
 *    for a script that used one -- the "callers cannot tell" failure
 *    test/sh-design.md calls worse than no shell at all.
 *  - Linkage.  The same note's third reason for the in-process rule is
 *    that the shell "costs nothing to programs that do not use it",
 *    which is an archive-extraction property: a reference from this
 *    file would pull the whole command language into every program that
 *    calls any exec function.
 *
 * Returns only on failure, like every other exec path here. */
static int shell_fallback(const char *path, char *const argv[], char *const envp[])
{
	int enoexec = errno;
	char *shell, **av;
	size_t n = 0, i;

	shell = __find_interpreter();
	if (!shell) { errno = enoexec; return -1; }
	while (argv[n]) n++;

	av = malloc((n + 3) * sizeof *av);
	if (!av) { free(shell); errno = enoexec; return -1; }
	/* arg0, file, arg1, ..., (char *)0 -- the clause's own shape.
	 * arg0 is the caller's, not the shell's path: glibc substitutes
	 * _PATH_BSHELL there, but POSIX.1-2017 names arg0 as "the value
	 * passed to execvp() in argv[0]" and POSIX.1-2024 relaxes the same
	 * slot to "<name> is an unspecified string", so passing argv[0]
	 * through satisfies both.  It is only what the shell prefixes its
	 * diagnostics with (sh/main.c's progname); $0 is the operand after
	 * it either way (sh(1p) OPERANDS, and sh/main.c:508-512).
	 *
	 * What is passed as `file` is the *resolved* path, not the argument
	 * as given.  exec.html says "file is the process image file", and
	 * for a name with no <slash> the process image file is what the
	 * PATH search found; the shell's command_file operand is a pathname
	 * it opens relative to its own current directory, so handing it the
	 * bare name would resolve it a second time, by different rules,
	 * against a different place than the one it was found in.
	 *
	 * An empty argv (n == 0) is not something a conforming caller
	 * produces -- exec.html: "The application shall ensure that the
	 * last member of this array is a null pointer" and arg0 "should
	 * point to a filename string" -- but it must not index argv[0]
	 * here, so the shell gets its own name in that slot. */
	av[0] = n ? argv[0] : (char *)"sh";
	av[1] = (char *)path;
	for (i = 1; i < n; i++) av[i + 1] = argv[i];
	av[(n ? n : 1) + 1] = 0;

	execve(shell, av, envp);

	/* The interpreter could not be run.  The caller asked to execute
	 * `file`, so that is what its errno stays about: "the shell is not
	 * installed" is not a diagnosis of `file`, and [ENOEXEC] is what
	 * this call meant before the fallback existed.  APPLICATION USAGE
	 * expects exactly this residue: "These implementations of execvp()
	 * and execlp() only give the [ENOEXEC] error in the rare case of a
	 * problem with the command interpreter's executable file." */
	free(av);
	free(shell);
	errno = enoexec;
	return -1;
}

int execvpe(const char *file, char *const argv[], char *const envp[])
{
	char *full;
	int use_path = !strchr(file, '/') && !strchr(file, '\\');
	int r, e;
	full = __find_program(file, use_path);
	if (!full) { errno = ENOENT; return -1; }
	r = execve(full, argv, envp);
	/* The one place the p-forms part company with the rest of the
	 * family.  [ENOEXEC] here is NT refusing the file as a process
	 * image: RtlCreateUserProcess answers STATUS_INVALID_IMAGE_NOT_MZ
	 * for a file with no MZ header at all or STATUS_INVALID_IMAGE_FORMAT
	 * for a malformed one, and src/process/spawn.c turns both into
	 * ENOEXEC.  Reading it back off errno rather than plumbing a status
	 * out of __spawn() keeps the condition stated in the terms
	 * exec.html states it in -- "would fail and set errno to [ENOEXEC]"
	 * -- and needs no second channel through execve().
	 *
	 * Not gated on use_path.  The clause is about which *function* was
	 * called, not how the name resolved: "the execlp() and execvp()
	 * functions shall execute a command interpreter", with no exception
	 * for a file argument containing a <slash>.  XCU 2.9.1 makes the
	 * same choice explicitly for the shell's two branches, giving the
	 * <slash> case its own sentence with the same fallback in it. */
	if (r == -1 && errno == ENOEXEC) r = shell_fallback(full, argv, envp);
	e = errno;
	free(full);
	errno = e;
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
