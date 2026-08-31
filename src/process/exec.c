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
	struct stat st;
	if (stat(path, &st) < 0) return -1;
	if (!S_ISREG(st.st_mode) || !(st.st_mode & 0111)) {
		errno = EACCES;
		return -1;
	}
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
 * Which sh, and why: this libc's own, called as a function --
 * __sh_run_script() (src/sh/script.c) -- and not spawned as a second
 * image.  The reverse was tried first and is worth stating, because it
 * is the version a reader will otherwise reinvent: find an sh.exe
 * beside the calling image, else "sh" on PATH, and execve() it.  Both
 * halves are wrong, and the shell's design note had already ruled on
 * both in its "reuse rule" -- the shell "is a set of internal functions
 * compiled into libc.a", its callers "call those functions directly and
 * never spawn an external interpreter".  This clause is not an
 * exception to that rule; it is the case the rule was written for.
 *
 *  - PATH.  Resolving the interpreter through PATH hands whoever can
 *    set PATH arbitrary code execution in *every* process that execs a
 *    script, and the victim did nothing wrong: it called execvp() on a
 *    file it was entitled to run.  It is no answer that the same PATH
 *    already chose `file` -- `file` is a script this process meant to
 *    run, while the interpreter is a native image the process never
 *    named at all, so PATH is being trusted for strictly more than the
 *    caller trusted it for.  include/ntlibc/rpath.h refuses the same
 *    bargain for $ORIGIN DLL search, and the design note refuses it by
 *    name for wordexp().
 *  - Beside the image.  This one is not a vulnerability, it is a
 *    standing maintenance burden that has already come due: "sh.exe is
 *    next to the running program" is true of `make install` and of
 *    nothing else, so every build layout that is not that one -- the
 *    sanitizer objdir, the packaging of the test binaries for a real
 *    Windows runner -- has to be taught to place a copy, or the clause
 *    silently stops working there.  A more careful search does not
 *    remove that burden; it moves it.
 *
 * What made the second image look necessary was the claim that running
 * the script in this process "would leave the old image underneath the
 * new one".  That is true, and it is already true of execve() above:
 * nothing here replaces an address space, because NT has no primitive
 * that does.  Every exec in this file is a stand-in that keeps the
 * caller's image alive, runs the program, and ends the process with the
 * program's status.  __sh_run_script() is that same stand-in with the
 * spawn taken out, so it takes the same measures in the same order --
 * cloexec descriptors closed once the interpreter is committed to, and
 * _exit() rather than exit() so the caller's atexit handlers and stdio
 * buffers die with it, exactly as exec.html requires.
 *
 * The interpreter contract being bigger than the engine was the other
 * objection, and it was a real one: sh(1p)'s operand handling and the
 * up-front refusal of what the engine would otherwise *misread* rather
 * than diagnose (`case`, the still-literal special parameters) lived in
 * sh/main.c, out of reach from here.  They live in src/sh/script.c now,
 * which is where the reuse rule always implied they belonged, and
 * sh/main.c is the one-line main() over them the note describes.  Both
 * callers of the clause get the whole utility, refusals included; there
 * is nothing left to duplicate.
 *
 * What this does cost is linkage: the note's third reason for the
 * in-process rule is that the shell "costs nothing to programs that do
 * not use it", and a reference from this file pulls the command
 * language into every program that calls any exec function.  That is
 * accepted, not overlooked.  The clause requires an interpreter to be
 * *available* to every execvp() caller; the only way to keep it out of
 * the link is to make its availability depend on the filesystem, which
 * is the thing being fixed.
 *
 * Returns only on failure, like every other exec path here. */
static int shell_fallback(const char *path, char *const argv[], char *const envp[]) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int enoexec = errno;
	char **av;
	size_t n = 0, i;
	int argc;

	while (argv[n]) n++;
	av = (char **)malloc((n + 3) * sizeof *av);
	if (!av) { errno = enoexec; return -1; }
	/* arg0, file, arg1, ..., (char *)0 -- the clause's own shape.
	 * arg0 is the caller's, not the shell's path: glibc substitutes
	 * _PATH_BSHELL there, but POSIX.1-2017 names arg0 as "the value
	 * passed to execvp() in argv[0]" and POSIX.1-2024 relaxes the same
	 * slot to "<name> is an unspecified string", so passing argv[0]
	 * through satisfies both.  It is only what the shell prefixes its
	 * diagnostics with (src/sh/script.c's progname); $0 is the operand
	 * after it either way (sh(1p) OPERANDS).
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
	argc = (int)(n ? n : 1) + 1;
	av[argc] = 0;

	/* "the environment of the executed command shall be as if the
	 * process invoked the sh utility using execl()" -- the interpreter
	 * runs with `envp`, which for execvp()/execlp() is the caller's own
	 * environ and for execvpe() is whatever the caller supplied.  The
	 * engine reads the environment through __environ (getenv(),
	 * wordexp()), so pointing that at envp is what an execve() of the
	 * shell would have done to the new image's environ. */
	__environ = (char **)envp;

	/* Past this point the exec has "succeeded" in the only sense this
	 * file ever means it (see execve() above): the interpreter is
	 * committed to and this process is standing in for it.  Only now
	 * may the close-on-exec descriptors go, and only _exit() may end
	 * it -- the caller's atexit handlers and unflushed stdio died with
	 * the image a real exec threw away. */
	__fd_close_all_cloexec();
	_exit(__sh_run_script(argc, av));
	return -1;   /* not reached: _exit() does not return */
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
	char **v = (char **)malloc(cap * sizeof(char *));
	if (!v) return 0;
	v[n++] = (char *)arg0;
	while (v[n-1]) {
		if (n >= cap - 1) {
			size_t nc;
			char **nv;
			if (!__array_next_capacity(cap, n, 2, 8, sizeof *v, &nc)) {
				free((void *)v); errno = ENOMEM; return 0;
			}
			nv = (char **)realloc((void *)v, nc * sizeof *v);
			if (!nv) { free((void *)v); return 0; }
			v = nv;
			cap = nc;
		}
		v[n++] = va_arg(ap, char *);
	}
	if (envout) *envout = va_arg(ap, char **);
	return v;
}

int execl(const char *path, const char *arg0, ...) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	va_list ap; char **v; int r;
	va_start(ap, arg0);
	v = build_argv(arg0, ap, 0);
	va_end(ap);
	if (!v) return -1;
	r = execv(path, v);
	free((void *)v);
	return r;
}

int execle(const char *path, const char *arg0, ...) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	va_list ap; char **v, **env = 0; int r;
	va_start(ap, arg0);
	v = build_argv(arg0, ap, &env);
	va_end(ap);
	if (!v) return -1;
	r = execve(path, v, env);
	free((void *)v);
	return r;
}

int execlp(const char *file, const char *arg0, ...) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	va_list ap; char **v; int r;
	va_start(ap, arg0);
	v = build_argv(arg0, ap, 0);
	va_end(ap);
	if (!v) return -1;
	r = execvp(file, v);
	free((void *)v);
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
	 * is STATUS_INVALID_FILE_FOR_SECTION (0xc0000020).  spawn.c maps that
	 * status to ENOEXEC when it describes process creation, but that still
	 * cannot recover fexecve()'s descriptor-specific EBADF distinction.
	 * Wine gets there by another route and produces EBADF.
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
