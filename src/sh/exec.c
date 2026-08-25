/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 3 added redirections and multi-command pipelines on top of
 * stage 2's simple-command execution; stage 4 (see exec_group()'s
 * header comment further down) adds subshells "( list )" and brace
 * groups "{ list; }", plus the minimal `cd` builtin needed to exercise
 * them; stage 5 (see __sh_cmdsub()'s header comment further down) adds
 * command substitution, which is what finally lets wordexp() stop
 * refusing "$(...)"/"`...`" with WRDE_CMDSUB. PATH lookup goes through the existing
 * __find_program() (src/process/find_program.c); starting a process
 * goes through the existing __spawn()/waitpid() (src/process/spawn.c,
 * src/process/wait.c).
 *
 * Word realization reuses the already-public wordexp() (src/wordexp/
 * wordexp.c) rather than re-implementing quote removal/parameter/
 * pathname expansion here: each sh_word.text is, by construction (see
 * sh.h and parse.c's header comment), already in exactly the form
 * wordexp() expects as one input word -- operator characters cannot
 * appear unquoted in it, and quotes/backslashes are untouched. Passing
 * a command's words through wordexp() with WRDE_APPEND therefore does
 * real parameter/tilde/pathname expansion and field splitting for
 * free, correctly (an unquoted "$FOO" containing spaces splits into
 * several argv entries, a glob expands, etc.) -- something a from-
 * scratch reimplementation here would either have to duplicate or get
 * subtly wrong. That reuse is now bidirectional: a word containing
 * command substitution ($(...) or `...`) makes wordexp() call straight
 * back into this file's __sh_cmdsub() (declared in src/internal/libc.h,
 * the only entry point into src/sh/ anything outside it uses), so a
 * substituted command is executed by this same executor, in the same
 * process, with no second interpreter anywhere -- which is the whole
 * point test/sh-design.md's "reuse rule" makes.
 *
 * ---- Redirections (XCU 2.7) -----------------------------------------
 *
 * "If more than one redirection operator is specified with a command,
 * the order of evaluation is from beginning to end" (2.7) -- and the
 * word that follows each operator is expanded and processed *at that
 * point*, not all up front -- so apply_redirs() below walks the
 * sh_redir list once, left to right, applying each one to *this
 * process's own* descriptor table before moving to the next. That
 * matters observably: "cmd >a 2>&1" duplicates fd 1 (already pointing
 * at "a") onto fd 2, so both end up in "a"; "cmd 2>&1 >a" duplicates
 * fd 1 (still whatever it was) onto fd 2 first, then repoints fd 1 at
 * "a" alone, so fd 2 keeps the old stdout and only fd 1 goes to "a".
 *
 * Redirections never need a fork(). A "process" here is always either
 * (a) nothing at all -- an assignment-only or all-expanded-away
 * command, where 2.7's redirection side effects (a bare "> file"
 * truncates "file" even though nothing runs) are produced by applying
 * them directly to the shell's own table and then immediately
 * reverting it, or (b) a __spawn() of a genuinely different NT
 * process, which already gets a private copy of whatever this
 * process's descriptor table looks like at the moment of the call
 * (src/process/spawn.c's __fd_runtime_data(), plus the PEB's std
 * handles for 0/1/2). So redirecting a command is: temporarily rewire
 * *this* process's own fds to what the command should see, spawn (or
 * don't, if nothing needs to run), then put this process's fds back
 * exactly as they were. save_fd()/restore_fds() below is that
 * rewire/undo pair; every redirection operator, and every pipe hookup
 * in a multi-command pipeline, goes through it.
 *
 * ---- Pipelines (XCU 2.9.2) -------------------------------------------
 *
 * The same "no fork needed" reasoning extends to pipelines of any
 * length: connecting one command's stdout to the next one's stdin is
 * just another pair of temporary redirections (of fd 1 and fd 0
 * respectively) applied to this process's table before each __spawn(),
 * exactly like a "> file" would be -- __spawn() does not care whether
 * the handle it copies into the child names a disk file or a pipe. So
 * __sh_exec_pipeline() below never calls fork(): it creates ncommands-1
 * real OS pipes, then loops once wiring each stage's fds, applying that
 * stage's own redirections on top (which is why "cmd1 2>&1 | cmd2"
 * merges cmd1's stderr into the pipe: the pipe hookup happens first,
 * the command's own redirection list is applied -- and evaluated left
 * to right -- after, exactly per 2.7's ordering rule above), spawning
 * it, and restoring. Every command is a real, independent, concurrently
 * running process by the time the loop finishes; only then does this
 * function wait for any of them, which is what lets them actually run
 * concurrently rather than one-at-a-time.
 *
 * Descriptor hygiene is the entire difficulty in a pipeline: this
 * process's own copy of a pipe's read end must be closed the moment
 * the command reading from it has been spawned, and its write end the
 * moment the command writing to it has been spawned -- not later,
 * because a write end left open anywhere (this process included) is a
 * write end a downstream reader's read() will never see EOF from, i.e.
 * an unkillable hang, and a leaked read end is a wasted handle at best.
 * The pipe fds themselves are also created O_CLOEXEC (pipe2(), not
 * pipe()) so they are never among the "everything open and not
 * close-on-exec" set __fd_runtime_data() (src/internal/fd.c) copies
 * into a spawned child by number -- only the fd-0/fd-1 *copies* this
 * file explicitly dup2()s onto the standard descriptors (which dup2()
 * always makes non-close-on-exec; see src/process/exec.c's fork/exec
 * header comment for why cloexec bookkeeping matters so much on this
 * platform) are meant to cross into a child, and exactly those and no
 * others do.
 *
 * ---- Here-documents (XCU 2.7.4) ---------------------------------------
 *
 * The lexer (parse.c) already captured the literal body text and
 * whether the delimiter was quoted; this file's only job is to turn
 * that into an open, readable file descriptor. It is deliberately a
 * real temporary file (tmpfile(), src/stdio/misc.c), not a pipe this
 * process writes into directly: a heredoc body larger than one pipe
 * buffer (65536 bytes here -- src/unistd/pipe.c) written before the
 * reading command exists would block this process forever with nobody
 * yet draining it, which is exactly the kind of hang this file's
 * pipeline discussion above warns about, just self-inflicted instead
 * of a pre-existing bug. A seekable file has no such limit and no such
 * ordering requirement.
 *
 * ---- Deliberately NOT implemented yet, later stages -------------------
 *   - control-flow reserved words (if/while/for/case), functions and
 *     aliases: no grammar for them exists, parser or executor, so they
 *     never reach this file at all -- a word like "if" is an ordinary
 *     WORD token, looked up on PATH like any other command name
 *     (test/sh-design.md item 2)
 *   - '&' actually backgrounding rather than running synchronously
 *     (job control is out of scope for this project entirely -- see
 *     test/sh-design.md -- but *not waiting* for an async list item
 *     is still future work, tracked here rather than silently assumed)
 * __sh_exec_pipeline()/__sh_exec_command() return -1 (with no status
 * written) for any of these; __sh_exec_list()/__sh_exec_andor() stop
 * and propagate that -1 rather than guessing at a status.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <wordexp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "libc.h"
#include "sh.h"

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

/* The "last command substitution performed" 2.9.1 needs (see
 * __sh_cmdsub() near the end of this file, and exec_simple()'s use of
 * these): its exit status, plus a monotonically increasing counter so a
 * caller can tell "a substitution ran and exited 0" from "no
 * substitution ran at all" without either being able to masquerade as
 * the other. File-scope rather than threaded through every expansion
 * helper's signature because the substitution happens *inside*
 * wordexp() (src/wordexp/wordexp.c), several frames below any of them,
 * with no wordexp_t field to carry it back out -- and 2.9.1 asks only
 * for the *last* one, which is exactly what a single slot records. */
static int cmdsub_status;
static unsigned long cmdsub_generation;

/* ---- shell-wide control flow, and $? ---------------------------------
 *
 * XCU 2.8.2: "the exit status of the last command executed".  Every
 * status this executor produces funnels through __sh_exec_pipeline(),
 * so recording it there is enough to give `exit` with no operand the
 * value 2.14 says it must use.  File-scope for the same reason
 * cmdsub_status above is: the built-in that reads it runs several
 * frames below the loop that sets it, and only the *last* value is
 * ever wanted.
 *
 * `flow_exit_pending` is the unwind sh.h documents: `exit` sets it, and
 * every list/and-or loop stops iterating while it is set rather than
 * running the rest of the program.  It is deliberately *not* a longjmp:
 * the redirection, environ and cwd save-and-restore this file is built
 * around all live in ordinary function epilogues, and jumping past them
 * would leave the shell's own fd table and environ permanently wrong
 * for the exit path -- which for `sh -c` is the path that runs the
 * atexit handlers and flushes stdout. */
static int sh_last_status;
static int flow_exit_pending;

void __sh_flow_exit(int status)
{
	flow_exit_pending = 1;
	sh_last_status = status;
}

int __sh_flow_pending(void)
{
	return flow_exit_pending;
}

void __sh_flow_clear(void)
{
	flow_exit_pending = 0;
}

int __sh_last_status(void)
{
	return sh_last_status;
}

static void free_strv(char **v, size_t n)
{
	size_t i;
	if (!v) return;
	for (i = 0; i < n; i++) __free(v[i]);
	__free(v);
}

/* Splits an assignment word's raw text ("NAME=value...", guaranteed by
 * parse.c's is_assignment_word() to have an unquoted '=' right after
 * an unquoted NAME) and expands the value half via wordexp(). Per
 * 2.9.1, only tilde and parameter expansion (plus quote removal) apply
 * to an assignment's value -- no field splitting, no pathname
 * expansion -- but wordexp() does not expose that narrower mode, so
 * this takes its first resulting word and accepts the (documented,
 * rare-in-practice) over-permissiveness of an unquoted glob/multi-word
 * $var in a value expanding more than a strict implementation would.
 * Command substitution in an assignment's value *does* run (2.9.1 step
 * 4 lists it among the expansions an assignment gets), and its status
 * is what 2.9.1's "no command name" rule reports -- see
 * cmdsub_status_rule() below. If expansion fails outright, the value
 * falls back to its literal raw text rather than the assignment being
 * silently dropped. On success, *name and *val are __malloc'd
 * and owned by the caller; returns 0, or -1 on a malformed assignment word
 * (should not happen given is_assignment_word) or OOM.
 */
static int split_assignment(const char *raw, char **name, char **val)
{
	const char *eq = strchr(raw, '=');
	size_t nlen;
	wordexp_t we;
	int have_we = 0;
	int rc = 0;

	if (!eq) return -1;
	nlen = (size_t)(eq - raw);
	*name = __malloc(nlen + 1);
	if (!*name) return -1;
	memcpy(*name, raw, nlen);
	(*name)[nlen] = 0;

	if (eq[1] && __wordexp_sh(eq + 1, &we, 0) == 0) {
		have_we = 1;
		*val = xstrdup(we.we_wordc ? we.we_wordv[0] : "");
	} else {
		*val = xstrdup(eq[1] ? eq + 1 : "");
	}
	if (!*val) { __free(*name); rc = -1; }
	if (have_we) wordfree(&we);
	return rc;
}

/* Replaces an existing "name=" entry in *vp (freeing the old string),
 * or appends a new one, growing *vp and *cap as needed. Takes
 * ownership of `entry` either way. */
static int env_set(char ***vp, size_t *n, size_t *cap, char *entry, size_t namelen)
{
	char **v = *vp;
	size_t i;
	for (i = 0; i < *n; i++) {
		if (strncmp(v[i], entry, namelen) == 0 && v[i][namelen] == '=') {
			__free(v[i]);
			v[i] = entry;
			return 0;
		}
	}
	if (*n + 1 >= *cap) {
		size_t nc = *cap ? *cap * 2 : 16;
		char **nv = __malloc((nc + 1) * sizeof *nv);
		if (!nv) { __free(entry); return -1; }
		memcpy(nv, v, *n * sizeof *nv);
		__free(v);
		v = *vp = nv;
		*cap = nc;
	}
	v[(*n)++] = entry;
	v[*n] = 0;
	return 0;
}

/* Builds a private envp for a command with an assignment prefix: a
 * full copy of the current environment (never the shell's own
 * `environ` -- test/sh-design.md is explicit that a substituted/run
 * command must never be able to clobber the caller's environ) with
 * each assignment applied on top. *out_n receives the entry count
 * (excluding the NULL terminator); returns NULL on OOM. */
static char **build_child_envp(const struct sh_word *assigns, size_t *out_n)
{
	size_t n = 0, cap, i;
	char **v;
	const struct sh_word *a;

	for (n = 0; __environ && __environ[n]; n++) continue;
	cap = n + 8;
	v = __malloc((cap + 1) * sizeof *v);
	if (!v) return 0;
	for (i = 0; i < n; i++) {
		v[i] = xstrdup(__environ[i]);
		if (!v[i]) { free_strv(v, i); return 0; }
	}
	v[n] = 0;

	for (a = assigns; a; a = a->next) {
		char *name, *val, *entry;
		size_t nlen, vlen;
		if (split_assignment(a->text, &name, &val)) { free_strv(v, n); return 0; }
		nlen = strlen(name);
		vlen = strlen(val);
		entry = __malloc(nlen + 1 + vlen + 1);
		if (!entry) { __free(name); __free(val); free_strv(v, n); return 0; }
		memcpy(entry, name, nlen);
		entry[nlen] = '=';
		memcpy(entry + nlen + 1, val, vlen + 1);
		__free(name);
		__free(val);
		if (env_set(&v, &n, &cap, entry, nlen)) { free_strv(v, n); return 0; }
	}
	*out_n = n;
	return v;
}

/* 2.9.1: a simple command with no cmd_word at all -- only variable
 * assignments -- is still valid, and those assignments affect the
 * *current* execution environment (this process's real environment),
 * not a child's. Always "succeeds" (status 0) as far as this function
 * is concerned: 2.9.1 gives no failure mode for a bare assignment. Its
 * caller may still overwrite that with the status of a command
 * substitution performed while expanding one of the values -- see
 * cmdsub_status_rule() below, which is where 2.9.1's "no command name,
 * but the command contained a command substitution" rule lives. */
static int exec_assignment_only(const struct sh_command *cmd, int *status)
{
	const struct sh_word *a;
	for (a = cmd->assigns; a; a = a->next) {
		char *name, *val;
		if (split_assignment(a->text, &name, &val)) continue;
		setenv(name, val, 1);
		__free(name);
		__free(val);
	}
	*status = 0;
	return 0;
}

/* ==== Redirections (XCU 2.7) ============================================ */

/* One entry of a redir_state: how to put fd `fd` in this process's own
 * table back the way it was before some redirection touched it.
 * `have` is 0 when `fd` was not open at all beforehand, in which case
 * "restore" means close it; otherwise `dup` is an F_DUPFD_CLOEXEC
 * duplicate of the original handle (close-on-exec so that *this* saved
 * copy, which lives in the table for as long as the command it is
 * protecting is being spawned, is never itself handed to that child --
 * see this file's header comment on why cloexec bookkeeping matters
 * here). */
struct redir_save {
	int fd;
	int have;
	int dup;
};

struct redir_state {
	struct redir_save *saves;
	size_t n, cap;
};

/* Records fd's current state so it can be restored later, unless it is
 * already recorded (a command that touches the same fd number twice --
 * "cmd >a >b" -- must restore to what fd 1 was *before either*
 * redirection, not to the intermediate state after the first one).
 * Returns -1 only on OOM growing the table; a plain "fd was not open"
 * is not an error here, it is exactly what `have = 0` records. */
static int save_fd(struct redir_state *rs, int fd)
{
	size_t i;
	for (i = 0; i < rs->n; i++) if (rs->saves[i].fd == fd) return 0;
	if (rs->n == rs->cap) {
		size_t nc = rs->cap ? rs->cap * 2 : 4;
		struct redir_save *ns = __malloc(nc * sizeof *ns);
		if (!ns) return -1;
		if (rs->saves) { memcpy(ns, rs->saves, rs->n * sizeof *ns); __free(rs->saves); }
		rs->saves = ns;
		rs->cap = nc;
	}
	rs->saves[rs->n].fd = fd;
	rs->saves[rs->n].dup = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	rs->saves[rs->n].have = rs->saves[rs->n].dup >= 0;
	rs->n++;
	return 0;
}

/* Undoes every save_fd() since the matching {0} initialization, in no
 * particular order (each entry names a distinct fd, so they are
 * independent), and frees the bookkeeping array. Safe to call on an
 * all-zero (never touched) redir_state. */
static void restore_fds(struct redir_state *rs)
{
	size_t i;
	for (i = 0; i < rs->n; i++) {
		if (rs->saves[i].have) {
			dup2(rs->saves[i].dup, rs->saves[i].fd);
			close(rs->saves[i].dup);
		} else {
			close(rs->saves[i].fd);
		}
	}
	__free(rs->saves);
	rs->saves = 0;
	rs->n = rs->cap = 0;
}

/* The fd a redirection operator applies to when the syntax did not
 * name one explicitly ("[n]<word" / "[n]>word" -- sh.h's sh_redir.fd
 * is -1 in that case): 0 for every input-flavoured operator, 1 for
 * every output-flavoured one (2.7.1 "Redirecting Input" / 2.7.2
 * "Redirecting Output" / 2.7.3 "Appending Redirected Output"). */
static int default_redir_fd(enum sh_redir_op op)
{
	switch (op) {
	case SH_R_GREAT:
	case SH_R_DGREAT:
	case SH_R_GREATAND:
	case SH_R_CLOBBER:
		return 1;
	default:
		return 0;
	}
}

/* Expands a redirection target word (2.7's general rule: "the word
 * that follows the redirection operator shall be subjected to tilde
 * expansion, parameter expansion, command substitution, arithmetic
 * expansion, and quote removal. Pathname expansion shall not be
 * performed on the word by a non-interactive shell"). wordexp() does
 * not expose a "no field splitting, no globbing" mode, so -- exactly
 * like split_assignment() above for an assignment's value -- this
 * takes wordexp()'s first resulting word and accepts the same
 * documented, rare-in-practice over-permissiveness. Returns NULL and
 * sets *unsupported on a wordexp() failure (WRDE_CMDSUB, most
 * commonly -- stage 5) or OOM; the caller propagates that as this
 * file's usual -1 "cannot execute this yet". */
static char *expand_redir_word(const char *raw, int *unsupported)
{
	wordexp_t we;
	char *r;
	if (__wordexp_sh(raw, &we, 0)) { *unsupported = 1; return 0; }
	r = xstrdup(we.we_wordc ? we.we_wordv[0] : "");
	wordfree(&we);
	if (!r) *unsupported = 1;
	return r;
}

/* 2.7.4: "If no part of word is quoted ... all lines of the
 * here-document shall be expanded for parameter expansion, command
 * substitution, and arithmetic expansion. In this case, the <backslash>
 * in the input behaves as the <backslash> inside double-quotes."  So an
 * unquoted-delimiter heredoc body gets exactly the same three
 * expansions, with exactly the same backslash rule, as an unquoted
 * "..." word -- but wordexp() only understands text written in real
 * shell syntax, and a heredoc body is literal: a bare '"' in it is
 * just a data byte, never a quote. This synthesizes a double-quoted
 * WORD wordexp() can parse instead of reimplementing double-quote
 * expansion a second time from scratch (the same reuse this file's
 * other expansion helpers rely on): wrap the whole body in '"', and
 * escape only the '"' bytes not already escaped by an odd-length run
 * of backslashes, since those are the only bytes that would otherwise
 * prematurely end the synthetic quoted word. Every other byte,
 * backslash runs included, is passed through unchanged -- whatever
 * wordexp()'s double-quote backslash handling does with them is
 * exactly the behavior 2.7.4 asks for. A $(...) or `...` inside the
 * body -- which 2.7.4 requires to be expanded, and which is the one
 * place a heredoc can run a command at all -- therefore runs for real,
 * through the same wordexp() call-out as any other double-quoted
 * word. Returns NULL and sets *unsupported on failure. */
static char *expand_heredoc(const char *body, int *unsupported)
{
	size_t n = strlen(body), i;
	char *syn = __malloc(2 * n + 3);
	size_t o = 0;
	int run = 0;
	wordexp_t we;
	char *r;

	if (!syn) { *unsupported = 1; return 0; }
	syn[o++] = '"';
	for (i = 0; i < n; i++) {
		if (body[i] == '\\') {
			run++;
		} else {
			if (body[i] == '"' && (run % 2) == 0) syn[o++] = '\\';
			run = 0;
		}
		syn[o++] = body[i];
	}
	syn[o++] = '"';
	syn[o] = 0;

	if (__wordexp_sh(syn, &we, 0)) { __free(syn); *unsupported = 1; return 0; }
	__free(syn);
	r = xstrdup(we.we_wordc ? we.we_wordv[0] : "");
	wordfree(&we);
	if (!r) *unsupported = 1;
	return r;
}

/* Materializes a here-document body (already expanded, or not, by the
 * caller per heredoc_quoted) as a real seekable file -- see this
 * file's header comment for why a pipe would risk a self-inflicted
 * hang instead. tmpfile() (src/stdio/misc.c) already creates-then-
 * unlinks so the data is gone the moment every handle to it closes;
 * F_DUPFD_CLOEXEC here gets an independent descriptor that survives
 * this function's own fclose() while staying close-on-exec (nothing
 * should inherit *this* handle -- the caller dup2()s it onto the
 * command's real target fd, and dup2()'s target is what actually ends
 * up non-close-on-exec and visible to the child). Returns a read-only
 * fd positioned at the start of the body, or -1 on any failure. */
static int heredoc_open(const char *text)
{
	FILE *f = tmpfile();
	size_t len;
	int fd, dfd;

	if (!f) return -1;
	len = strlen(text);
	if (len && fwrite(text, 1, len, f) != len) { fclose(f); return -1; }
	if (fflush(f)) { fclose(f); return -1; }
	fd = fileno(f);
	if (fd < 0 || lseek(fd, 0, SEEK_SET) < 0) { fclose(f); return -1; }
	dfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	fclose(f);
	return dfd;
}

/* Applies one redirection to this process's own descriptor table
 * (already save_fd()-protected by the caller before this is called, so
 * every branch here is free to dup2()/close() fd outright). Returns 0
 * on success, -1 with *unsupported set for "cannot execute this yet"
 * (command substitution in the target word or heredoc body -- stage
 * 5), or 1 for a genuine redirection failure (bad path, permission
 * denied, duplicating a closed fd, ...) that the caller reports as the
 * *command's* exit status per 2.8.1, not as this file's -1. */
static int apply_one_redir(const struct sh_redir *r, int fd, int *unsupported)
{
	char *word;
	int newfd = -1;

	switch (r->op) {
	case SH_R_LESS:
		word = expand_redir_word(r->word, unsupported);
		if (!word) return *unsupported ? -1 : 1;
		newfd = open(word, O_RDONLY);
		__free(word);
		break;

	case SH_R_GREAT:
	case SH_R_CLOBBER:
		/* set -C ("noclobber") is not implemented -- this shell has no
		 * `set` builtin at all yet -- so plain '>' never refuses an
		 * existing file in the first place, which makes '>|' (whose
		 * only documented job, 2.7.2, is overriding noclobber) behave
		 * identically to '>' here. That is a real, deliberate gap,
		 * not the two operators being silently treated as the same
		 * thing: the day `set -C` exists, only '>' needs to grow the
		 * "refuse an existing regular file" check. */
		word = expand_redir_word(r->word, unsupported);
		if (!word) return *unsupported ? -1 : 1;
		newfd = open(word, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		__free(word);
		break;

	case SH_R_DGREAT:
		word = expand_redir_word(r->word, unsupported);
		if (!word) return *unsupported ? -1 : 1;
		newfd = open(word, O_WRONLY | O_CREAT | O_APPEND, 0666);
		__free(word);
		break;

	case SH_R_LESSGREAT:
		/* 2.7.7: opened for both reading and writing; created if it
		 * does not exist; deliberately no O_TRUNC. */
		word = expand_redir_word(r->word, unsupported);
		if (!word) return *unsupported ? -1 : 1;
		newfd = open(word, O_RDWR | O_CREAT, 0666);
		__free(word);
		break;

	case SH_R_LESSAND:
	case SH_R_GREATAND: {
		char *end;
		long n;
		word = expand_redir_word(r->word, unsupported);
		if (!word) return *unsupported ? -1 : 1;
		if (strcmp(word, "-") == 0) {
			/* 2.7.6: "file descriptor n ... shall be closed." Closing
			 * an fd that was not even open is not treated as a
			 * failure -- there is nothing left to do either way. */
			__free(word);
			close(fd);
			return 0;
		}
		n = strtol(word, &end, 10);
		if (word[0] && !*end && n >= 0 && n < FD_MAX) {
			int ok = dup2((int)n, fd) >= 0;
			__free(word);
			return ok ? 0 : 1; /* EBADF: duplicating a closed fd */
		}
		/* 2.7.6: "If word evaluates to something else, the behavior
		 * is unspecified" -- treated as a redirection error rather
		 * than silently ignored. */
		__free(word);
		return 1;
	}

	case SH_R_DLESS:
	case SH_R_DLESSDASH: {
		const char *body = r->heredoc ? r->heredoc : "";
		char *expanded = 0;
		if (!r->heredoc_quoted) {
			expanded = expand_heredoc(body, unsupported);
			if (!expanded) return -1;
			body = expanded;
		}
		newfd = heredoc_open(body);
		__free(expanded);
		break;
	}

	default:
		return 1;
	}

	if (newfd < 0) return 1;
	if (newfd != fd) {
		if (dup2(newfd, fd) < 0) { close(newfd); return 1; }
		close(newfd);
	}
	return 0;
}

/* Walks `redirs` left to right (2.7: "the order of evaluation is from
 * beginning to end"), save_fd()-protecting each target fd the first
 * time it is touched and applying the redirection. Stops at the first
 * -1 (unsupported) or failure, since a redirection error means the
 * command that owns this list will not run at all -- there is no
 * reason to keep evaluating redirections for a command that is about
 * to fail (2.8.1: for an ordinary utility this "shall not exit" the
 * shell, it just fails that one command). Returns -1 (caller
 * propagates as "cannot execute this yet") or 0 with *failed telling
 * the caller whether the command should actually run. */
static int apply_redirs(const struct sh_redir *redirs, struct redir_state *rs, int *failed)
{
	const struct sh_redir *r;
	int unsupported = 0;

	*failed = 0;
	for (r = redirs; r; r = r->next) {
		int fd = r->fd >= 0 ? r->fd : default_redir_fd(r->op);
		int rc;
		if (save_fd(rs, fd)) return -1; /* OOM */
		rc = apply_one_redir(r, fd, &unsupported);
		if (unsupported) return -1;
		if (rc) { *failed = 1; return 0; }
	}
	return 0;
}

/* ==== Spawning one already-redirected simple command ==================== */

struct stage_result {
	pid_t pid;   /* >= 0: a real process was spawned; caller must waitpid() it */
	int status;  /* meaningful only when pid < 0: the result is already final */
	int had_name; /* 2.9.1 step 2 actually produced a command name (so the
	               * "no command name, but the command contained a command
	               * substitution" status rule below does not apply) */
};

/* ==== Built-in utilities: dispatch, not a strcmp chain =================
 *
 * Stage 4's `cd` was matched here with a raw strcmp() on the
 * *unexpanded* first word, and this comment used to say why that was
 * deliberately narrow ("this builtin exists to make stage 4's
 * subshell/brace tests exercisable, not to be a general-purpose builtin
 * dispatcher").  Stage 6a builds the dispatcher: src/sh/builtin.c owns
 * the table (`cd` included) and every implementation, and this file
 * consults it from spawn_stage() below -- after wordexp() has produced
 * the argv, which is the only string XCU 2.9.1 ("Command Search and
 * Execution") ever names as the command name.  So `c=cd; $c /tmp` and
 * `'cd' /tmp` are both a `cd` now, as 2.9.1 requires and as the raw
 * match could not express.
 *
 * `env_mutate` still gates the same thing it always did, just via the
 * table's `env_effect` column instead of a name: a built-in that
 * changes the shell execution environment (XCU 2.12) must not actually
 * do so when this invocation is one stage of a multi-command pipeline,
 * which 2.12 places in a subshell environment.  See run_stage()'s
 * comment below, which already argued this at length for `cd` and now
 * argues it for a column.
 */

/* Finds and starts the program named by cmd->words (which must be
 * non-NULL -- the assignment-only case is handled by the caller, see
 * run_stage() below), but does not wait for it: a pipeline needs every
 * stage spawned before it waits for any of them (see this file's
 * header comment on why), and a lone command's caller waits
 * immediately afterward instead. Returns -1 ("cannot execute this
 * yet") for a word that needs command substitution or on OOM, exactly
 * as stage 2 did; otherwise 0 with *out filled in. */
static int spawn_stage(const struct sh_command *cmd, struct stage_result *out, int env_mutate)
{
	wordexp_t we;
	const struct sh_word *w;
	int first = 1, rc;
	char *resolved;
	char **envp = 0;
	size_t envn = 0;
	const struct sh_builtin *bi;

	out->pid = -1;
	out->had_name = 0;

	/* run_stage() never calls this with cmd->words == NULL (that is
	 * exactly the assignment-only case it handles itself), but a
	 * static analyzer cannot see across the call boundary and flags
	 * `we` as read uninitialized on a hypothetical zero-iteration
	 * loop below -- so this makes the invariant an explicit, checked
	 * fact rather than something only a comment promises. */
	if (!cmd->words) { out->status = 0; return 0; }

	for (w = cmd->words; w; w = w->next) {
		rc = __wordexp_sh(w->text, &we, first ? 0 : WRDE_APPEND);
		if (rc) {
			if (!first) wordfree(&we);
			return -1; /* see exec_builtin_cd() above on what a nonzero
			            * wordexp() means here */
		}
		first = 0;
	}
	if (we.we_wordc == 0) { wordfree(&we); out->status = 0; return 0; } /* every word expanded away -- 2.9.1: "no command name results" */
	out->had_name = 1;

	/* 2.9.1 step 1d: "If the command name matches the name of a
	 * utility listed in ... special built-in utilities, that special
	 * built-in utility shall be invoked", and step 1e does the same
	 * for a regular built-in, both *before* any PATH search.  So this
	 * lookup goes here, above __find_program(), not beside it.
	 *
	 * An assignment prefix on a built-in is not applied: 2.9.1 says
	 * such assignments "shall affect the current execution
	 * environment" for special built-ins and are unspecified for
	 * regular ones, and this shell's only variable store is the real
	 * `environ` -- so applying them would leak into the shell for a
	 * regular built-in with no way to undo it.  Refusing to run the
	 * command at all (the -1 convention) is the honest answer, since
	 * a silently-unapplied assignment is exactly the class of silent
	 * wrongness sh/main.c's refusal list exists to prevent. */
	bi = __sh_builtin_lookup(we.we_wordv[0]);
	if (bi) {
		struct sh_builtin_ctx ctx;
		if (cmd->assigns) { wordfree(&we); return -1; }
		if (!env_mutate && bi->env_effect) {
			/* A pipeline stage's `cd` would change *this* process's
			 * working directory with nothing to put it back; 2.12
			 * puts that stage in a subshell environment, so not
			 * doing it is indistinguishable from doing it in a
			 * subshell that is then discarded. */
			out->status = 0;
			wordfree(&we);
			return 0;
		}
		ctx.argc = (int)we.we_wordc;
		ctx.argv = we.we_wordv;
		ctx.env_mutate = env_mutate;
		ctx.last_status = sh_last_status;
		ctx.status = 0;
		rc = bi->fn(&ctx);
		out->status = ctx.status;
		wordfree(&we);
		return rc;
	}

	if (cmd->assigns) {
		envp = build_child_envp(cmd->assigns, &envn);
		if (!envp) { wordfree(&we); return -1; }
	}

	resolved = __find_program(we.we_wordv[0], 1);
	if (!resolved) {
		out->status = 127; /* command not found -- matches system()'s exit-127 clause */
		wordfree(&we);
		free_strv(envp, envn);
		return 0;
	}

	out->pid = __spawn(resolved, we.we_wordv, envp);
	__free(resolved);
	wordfree(&we);
	free_strv(envp, envn);
	if (out->pid < 0) { out->status = 126; out->pid = -1; } /* found but could not execute */
	return 0;
}

/* Runs one simple command's non-redirection half: either the
 * assignment-only case (2.9.1) or a real spawn_stage(). `env_mutate`
 * distinguishes the two contexts an assignment-only command can appear
 * in: as the whole of a one-command "pipeline" (the ordinary case,
 * where 2.9.1 says the assignment affects *this* shell's own
 * environment -- exec_assignment_only() does exactly that), or as one
 * stage of a multi-command pipeline. POSIX permits (2.9.2, and every
 * historical shell chooses to) run every pipeline command but possibly
 * the last in a subshell environment; this implementation genuinely
 * spawns a separate process for every stage that has a program to run,
 * but an assignment-only stage has none, so there is no subshell for
 * the assignment to be scoped into the way a real command's env would
 * be. Actually setenv()-ing it here would leak the assignment into
 * this (the real shell's) process, which no pipeline stage may ever
 * do. Treating it as a no-op instead is indistinguishable from any
 * outside observer's point of view from doing it in a subshell that is
 * then discarded -- nothing downstream of the pipeline can tell the
 * difference -- and it is what keeps this file's "no fork()" pipeline
 * design correct rather than merely convenient. */
static int run_stage(const struct sh_command *cmd, struct stage_result *out, int env_mutate)
{
	if (!cmd->words) {
		out->pid = -1;
		out->had_name = 0;
		if (env_mutate) exec_assignment_only(cmd, &out->status);
		else out->status = 0;
		return 0;
	}
	return spawn_stage(cmd, out, env_mutate);
}

static int wait_status(pid_t pid)
{
	int wstatus;
	if (waitpid(pid, &wstatus, 0) < 0) return -1;
	return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
}

/* ==== A single simple command, including its own redirections ========== */

/* XCU 2.9.1: "If there is a command name, execution shall continue as
 * described in Command Search and Execution. If there is no command
 * name, but the command contained a command substitution, the command
 * shall complete with the exit status of the last command substitution
 * performed. Otherwise, the command shall complete with a zero exit
 * status."
 *
 * `gen0` is cmdsub_generation as it stood *before* any of this
 * command's expansions ran -- which has to include the redirection
 * words (apply_redirs() expands those, and "> $(...)" is a command with
 * no command name that "contained a command substitution" just as much
 * as "$(...)" alone is), so every caller samples it before its own
 * apply_redirs(), not just before run_stage(). */
static int cmdsub_status_rule(const struct stage_result *sr, unsigned long gen0)
{
	if (!sr->had_name && cmdsub_generation != gen0) return cmdsub_status;
	return sr->status;
}

static int exec_simple(const struct sh_command *cmd, int *status)
{
	struct redir_state rs;
	int failed = 0;
	struct stage_result sr;
	int st;
	unsigned long gen0 = cmdsub_generation;

	rs.saves = 0; rs.n = rs.cap = 0;

	if (apply_redirs(cmd->redirs, &rs, &failed)) { restore_fds(&rs); return -1; } /* unsupported */
	if (failed) {
		/* 2.8.1: a redirection error for an ordinary (non-special-
		 * built-in) utility "shall not exit" a non-interactive shell
		 * -- it fails only this command. The exact nonzero value is
		 * implementation-defined; 1 matches common practice (bash,
		 * dash). */
		restore_fds(&rs);
		*status = 1;
		return 0;
	}

	if (run_stage(cmd, &sr, 1)) { restore_fds(&rs); return -1; }
	restore_fds(&rs);

	if (sr.pid < 0) { *status = cmdsub_status_rule(&sr, gen0); return 0; }
	st = wait_status(sr.pid);
	if (st < 0) return -1;
	*status = st;
	return 0;
}

/* ==== Subshells and brace groups (XCU 2.9.4, XCU 2.12) =================
 *
 * "( compound-list )" -- "Execute compound-list in a subshell
 * environment; see Shell Execution Environment [2.12]. Variable
 * assignments and built-in commands that affect the environment shall
 * not remain in effect after the list finishes." (2.9.4, "Grouping
 * Commands")
 *
 * "{ compound-list ; }" -- "Execute compound-list in the current
 * process environment." (2.9.4)
 *
 * 2.12 spells out what a "subshell environment" is: "A subshell
 * environment shall be created as a duplicate of the shell environment
 * ... Changes made to the subshell environment shall not affect the
 * shell environment. ... commands that are grouped with parentheses ...
 * shall be executed in a subshell environment." Of the objects 2.12
 * lists as part of that environment, this implementation's shell
 * language can actually change exactly three: shell parameters (set by
 * assignment, all of which -- see exec_assignment_only() above -- are
 * this process's real environ, there being no separate unexported-
 * variable table), the working directory (only via the `cd` builtin
 * just above), and open files. Traps, umask, ulimit and aliases are not
 * implemented at all yet (sh.h's banner), so there is nothing to
 * isolate there.
 *
 * ---- Standalone: save-and-restore, not fork() ---------------------------
 *
 * A brace group's own redirections and its body both need to run
 * without a fork() at all -- that part is identical in shape to
 * exec_simple() above (apply_redirs()/restore_fds() bracket a plain
 * __sh_exec_list()) and needs no environment isolation since 2.9.4 says
 * a brace group's changes *are* supposed to remain in effect.
 *
 * A standalone subshell needs the same redirection bracketing, plus
 * genuine isolation of variables and cwd. The obvious tool is fork():
 * exec.c's own header comment used to say exactly that ("needs the
 * fork() this file's header comment says a plain simple command does
 * not"). It is deliberately not used here. Two things earn that:
 *
 *  1. Nothing about this shell's supported subset needs the *process*
 *     boundary a fork() buys, only the *state* isolation 2.12 actually
 *     asks for. A standalone "( list )" blocks its caller until it
 *     finishes -- there is no job control, no backgrounding that
 *     actually backgrounds (SH_SEP_AMP still runs synchronously, see
 *     __sh_exec_list()'s comment), and no command substitution reading
 *     its output concurrently (command substitution captures to a
 *     seekable temporary file precisely so that the reader only starts
 *     once __sh_exec_list() here returns -- see __sh_cmdsub() below). So
 *     nothing else in this process ever needs to run *while* the
 *     subshell's body runs, which is the one thing save-and-restore
 *     cannot give you and a real child process can.
 *  2. fork()'s cost on this platform is real and recent: src/process/
 *     fork.c's RtlCloneUserProcess wrapper is the exact machinery
 *     4a37d08 ("fork: keep every open descriptor's handle valid across
 *     the clone...") had to fix days ago -- a cloexec descriptor's
 *     handle surviving the clone while its *fd-table entry* did not,
 *     which froze a handle number NT then silently reissued to the next
 *     thing that asked, corrupting an unrelated process handle two
 *     frames away. That bug is fixed, but it is exactly the class of
 *     platform-specific sharp edge this file's stage-3 header comment
 *     already chose to avoid entirely for redirections and pipelines
 *     ("Redirections never need a fork()") rather than merely work
 *     around. A save/restore subshell here keeps that same discipline:
 *     one fewer path through RtlCloneUserProcess to keep correct.
 *
 * What has to be saved, precisely, per 2.12's own list intersected with
 * what this shell can actually change:
 *   - Shell parameters: every name=value pair in `environ`, since (as
 *     exec_assignment_only() notes) this implementation gives shell
 *     variables and exported variables no separate existence -- a
 *     snapshot-and-restore of environ *is* a snapshot-and-restore of
 *     "shell parameters" here. Done via setenv()/unsetenv() only (see
 *     env_snapshot_restore() below), never by swapping __environ's
 *     storage directly, because src/env/setenv.c privately tracks which
 *     entries were added by putenv() (and must therefore never be
 *     freed) in a static table this file cannot see or replicate;
 *     driving the whole restore through the public API keeps that
 *     bookkeeping correct no matter what a caller outside this shell
 *     did to `environ` before ever reaching __sh_exec_list().
 *   - Working directory: getcwd()/chdir(), the only way it can change
 *     within this language (the `cd` builtin above).
 *   - Open files: exactly what apply_redirs()/restore_fds() already
 *     give every simple command for its own cmd->redirs -- bracketing
 *     the subshell's redirections (attached to the compound command
 *     itself) the same way already restores this process's descriptor
 *     table to what it was. Nothing *inside* the body can leave a
 *     lasting fd change either: every simple command's own redirections
 *     are already save/restore-protected the same way (exec_simple()),
 *     and there is no `exec` builtin (sh.h's banner) that could apply a
 *     redirection permanently. So this file's ordinary redir_state
 *     machinery, reused unchanged, is the entire "open files" story.
 * $? is not separately saved/restored at all: it is never a shell
 * variable stored anywhere this file can see (no $?  expansion exists
 * yet either), only the *out-parameter* every __sh_exec_*() already
 * threads through by reference, so "cmd; (false); echo after" getting
 * $?  in "after" from `cmd`'s status rather than the subshell's is
 * simply what *not* writing through `status` until the subshell's own
 * final status is known already gives, for free.
 *
 * ---- As one stage of a multi-command pipeline: still no fork() ---------
 *
 * An earlier version of this file forked here: every other stage in a
 * multi-command pipeline is a real concurrently-running OS process
 * connected through a real (64KiB-buffered) pipe, precisely so that no
 * stage's output has to fit in memory and no stage can stall another,
 * and running a compound-command stage's body *in this process* while
 * the rest of the pipeline is still being wired up risked exactly the
 * self-inflicted hang this file's header comment already warns a
 * heredoc-via-pipe would risk. fork() sidestepped that -- but on this
 * platform fork() means src/process/fork.c's RtlCloneUserProcess
 * wrapper, which stock Wine (the interpreter CI's `test` legs actually
 * run under -- see .github/workflows/ci.yml) does not implement at
 * all: it is not a slow path or a missing edge case, it is `wine: Call
 * to unimplemented function ntdll.dll.RtlCloneUserProcess, aborting`,
 * which takes the whole process down. sh.exe is not one of this
 * project's `*-win.c` tests (the ones the Makefile's TEST_RUN already
 * excludes from every Wine leg for exactly this reason -- see
 * test/fork-win.c) precisely because it was never expected to fork;
 * stage 4's fork_group_stage() broke that quietly, and every `test`
 * leg in CI run 32700969420 failed as a result.
 *
 * The fix is to never fork here either, by not running a
 * compound-command stage's body *while the rest of the pipeline is
 * still being wired up* in the first place. __sh_exec_pipeline() below
 * now spawns every real (SH_CMD_SIMPLE) stage first, exactly as stage
 * 3 already did, and only *afterward* -- once every real process in
 * the pipeline already exists and is concurrently draining/filling its
 * own pipe ends -- runs each compound-command stage's body in this
 * process, left to right, via exec_group_stage_inline() below. By the
 * time any deferred stage's body runs, whichever neighbor stage
 * produces its input or consumes its output is either a real spawned
 * process (already running concurrently, so blocking past one pipe
 * buffer is fine -- the real process on the other end keeps draining/
 * filling it) or a deferred stage strictly to its own left, which (by
 * the same left-to-right order) has therefore already finished and
 * closed its end before this one starts reading. Either way there is
 * always something already able to make progress on the other end of
 * every pipe a deferred stage touches, so no fixed-size pipe buffer
 * can wedge this process against itself.
 *
 * That reasoning has exactly one hole: two (or more) compound-command
 * stages *directly adjacent* in the same pipeline, with no real
 * spawned stage between them (e.g. "{ a; } | { b; }"). Both sides are
 * deferred; whichever runs first is, by definition, not yet running
 * concurrently with the other, so if it writes (or waits to read) more
 * than one pipe buffer before its neighbor's turn comes, it blocks
 * forever with nothing on the other end to unblock it -- silently,
 * with no unimplemented-function trap to report it. Genuine
 * concurrency (another process, another thread) is the only fix, and
 * this file deliberately has neither available to it here. Rather than
 * risk that hang, __sh_exec_pipeline() below detects two adjacent
 * non-simple stages up front and refuses the whole pipeline via the
 * same "not yet supported" -1 convention this file's own header
 * comment above describes -- a clean,
 * documented failure a caller can report, never a hang or a Wine
 * abort. "( a ) | { b; }" and its permutations are therefore not yet
 * supported by this shell; every other placement of "( ... )"/"{ ...
 * }" in a pipeline, including any number of them separated by at least
 * one ordinary command, works exactly as before.
 *
 * 2.12's "each command of a multi-command pipeline is in a subshell
 * environment" is not qualified by "(...)" vs "{...}", so a deferred
 * brace-group pipeline stage still needs the same environ/cwd
 * save-and-restore this file's standalone-subshell case above uses --
 * exec_group_stage_inline() applies it unconditionally (unlike
 * exec_group()'s is_subshell-gated version), same as the fork() this
 * replaces used to give every pipeline stage for free by virtue of
 * being a different process. See test_exec_group_pipeline_stage() in
 * test/sh-engine.c, which checks specifically that a brace group's assignment
 * does not leak out when used as a pipeline stage even though a
 * standalone brace group's does.
 */

/* Snapshot of every name=value pair currently in `environ`, deep-copied
 * so it survives whatever setenv()/unsetenv()/putenv() calls the
 * subshell body makes to the live one. */
/* A subshell environment (XCU 2.12) gets its own copy of the
 * positional parameters: "( set -- x )" must not renumber the caller's,
 * any more than "( cd / )" moves it.  2.12's object list does not name
 * them explicitly, but 2.9.1's "the environment of the shell" and the
 * whole point of a subshell -- changes to it "shall not affect the
 * shell execution environment" -- cover them, and every shell agrees.
 *
 * take-then-replace rather than a dedicated copy routine: the take
 * leaves the live list empty and hands this frame the only pointer to
 * the caller's array, and __sh_params_replace() then builds the
 * subshell's own copy out of it.  One owner per array at every moment,
 * which is what makes nesting safe.  Returns -1 on OOM with the
 * caller's list already restored. */
static int params_subshell_enter(struct sh_params *saved)
{
	__sh_params_take(saved);
	if (__sh_params_replace(saved->v, saved->n) < 0) {
		__sh_params_install(saved);
		return -1;
	}
	return 0;
}

struct env_snapshot {
	char **names;
	char **vals;
	size_t n;
};

static void free_env_snapshot(struct env_snapshot *es)
{
	size_t i;
	for (i = 0; i < es->n; i++) { __free(es->names[i]); __free(es->vals[i]); }
	__free(es->names);
	__free(es->vals);
	es->names = 0; es->vals = 0; es->n = 0;
}

/* Returns 0 on success, -1 on OOM (nothing left half-allocated either way). */
static int env_snapshot_take(struct env_snapshot *es)
{
	size_t n, i;
	es->names = 0; es->vals = 0; es->n = 0;
	for (n = 0; __environ && __environ[n]; n++) continue;
	if (!n) return 0;
	es->names = __malloc(n * sizeof *es->names);
	es->vals = __malloc(n * sizeof *es->vals);
	if (!es->names || !es->vals) { __free(es->names); __free(es->vals); es->names = 0; es->vals = 0; return -1; }
	for (i = 0; i < n; i++) {
		const char *e = __environ[i];
		const char *eq = strchr(e, '=');
		size_t nlen = eq ? (size_t)(eq - e) : strlen(e);
		es->names[i] = __malloc(nlen + 1);
		if (es->names[i]) { memcpy(es->names[i], e, nlen); es->names[i][nlen] = 0; }
		es->vals[i] = xstrdup(eq ? eq + 1 : "");
		if (!es->names[i] || !es->vals[i]) { es->n = i + 1; free_env_snapshot(es); return -1; }
	}
	es->n = n;
	return 0;
}

static int name_in_snapshot(const struct env_snapshot *es, const char *name)
{
	size_t i;
	for (i = 0; i < es->n; i++) if (strcmp(es->names[i], name) == 0) return 1;
	return 0;
}

/* Restores `environ` to exactly the state `es` recorded: every name the
 * body added gets unsetenv()'d, every name/value `es` remembers gets
 * setenv(..., 1)'d back (whether the body changed it, left it alone, or
 * deleted it) -- driven entirely through the public setenv()/getenv()/
 * unsetenv() API so src/env/setenv.c's own putenv()-ownership
 * bookkeeping (a static table this file has no access to) stays
 * correct, per this function group's header comment above. */
static void env_snapshot_restore(const struct env_snapshot *es)
{
	size_t n, i;
	char **cur;

	for (n = 0; __environ && __environ[n]; n++) continue;
	cur = n ? __malloc(n * sizeof *cur) : 0;
	if (n && cur) {
		for (i = 0; i < n; i++) {
			const char *e = __environ[i];
			const char *eq = strchr(e, '=');
			size_t nlen = eq ? (size_t)(eq - e) : strlen(e);
			char *nm = __malloc(nlen + 1);
			if (nm) { memcpy(nm, e, nlen); nm[nlen] = 0; }
			cur[i] = nm;
		}
		for (i = 0; i < n; i++)
			if (cur[i] && !name_in_snapshot(es, cur[i])) unsetenv(cur[i]);
		for (i = 0; i < n; i++) __free(cur[i]);
		__free(cur);
	}
	/* n && !cur is OOM listing what to remove: best effort continues
	 * below and still gets every remembered value put back, even though
	 * a variable the body added with no snapshot counterpart cannot be
	 * removed in that case. */
	for (i = 0; i < es->n; i++) setenv(es->names[i], es->vals[i], 1);
}

/* Runs `cmd`'s own redirections (2.9.4: "each [may be followed by]
 * redirections ... shall apply to all the commands within the compound
 * command that do not explicitly override that redirection") bracketing
 * its body, and -- for SH_CMD_SUBSHELL only -- environ/cwd
 * save-and-restore around that per this function group's header
 * comment. Same -1/0 convention as every other __sh_exec_*() helper. */

/* ==== Compound commands (XCU 2.9.4) ====================================
 *
 * Stage 6b.  `if`, `while`, `until` and `for` are the constructs
 * test/sh-design.md's item 2 names; `case` and function definitions are
 * not here yet and still lex as ordinary WORDs, so sh/main.c still
 * refuses them by name.
 *
 * Three properties are shared by all four and are the reason they live
 * next to each other rather than in the executor's simple-command path:
 *
 *  - **They run in this process, with no subshell.**  2.12's list of
 *    what a subshell environment scopes ("Grouping Commands" in 2.9.4
 *    is the construct that asks for one, via "( )") does not include
 *    the control-flow constructs, so a `cd` inside a `for` body moves
 *    the shell, and an assignment in an `if` branch survives the `fi`.
 *    exec_group() supplies the subshell semantics for "( ... )" and
 *    deliberately is not reused here.
 *
 *  - **The condition's status is not the construct's status.**  Every
 *    one of the four says so explicitly and differently -- an `if` with
 *    no branch taken is 0 "if none was executed", a `while` whose body
 *    never runs is 0 "if none was executed", a `for` over an empty word
 *    list is 0.  Each of these therefore keeps the condition's status in
 *    a local and only ever writes *status from a body.  Letting the
 *    condition fall through would make `if false; then ...; fi` report
 *    1, which is the single most common way to get this wrong.
 *
 *  - **A pending `exit` has to get out.**  These are loops and
 *    branches around __sh_exec_list(), which is exactly what sh.h's
 *    control-flow comment means by "however many nested lists" -- so
 *    each checks __sh_flow_pending() at every point it would otherwise
 *    iterate again or evaluate another condition.  Without that,
 *    `while true; do exit 0; done` never terminates: the body returns
 *    normally with the flag set and the loop asks the condition again.
 */

/* 2.9.4 "The if Conditional Construct": "The if compound-list shall be
 * executed; if its exit status is zero, the then compound-list shall be
 * executed and the command shall complete.  Otherwise, each elif
 * compound-list shall be executed, in turn ... Otherwise, the else
 * compound-list shall be executed."  Exit status: "the exit status of
 * the then or else compound-list that was executed, or zero, if none
 * was executed." */
static int exec_if(const struct sh_command *cmd, int *status)
{
	const struct sh_ifarm *a;

	*status = 0;
	for (a = cmd->arms; a; a = a->next) {
		int cond_status;
		int rc = __sh_exec_list(a->cond, &cond_status);
		if (rc) return rc;
		if (__sh_flow_pending()) { *status = cond_status; return 0; }
		if (cond_status == 0) return __sh_exec_list(a->body, status);
	}
	if (cmd->else_body) return __sh_exec_list(cmd->else_body, status);
	return 0;
}

/* 2.9.4 "The while Loop": "compound-list-1 shall be executed, and if it
 * has a non-zero exit status, the while command shall complete.
 * Otherwise, the compound-list-2 shall be executed, and the process
 * shall repeat."  "The until Loop" is the same sentence with the test
 * inverted -- "if it has a zero exit status, the until command
 * completes" -- which is `cmd->until`.  Exit status for both: "the exit
 * status of the last compound-list-2 executed, or zero if none was
 * executed", which is why *status starts at 0 and is only ever written
 * by the body. */
static int exec_loop(const struct sh_command *cmd, int *status)
{
	*status = 0;
	for (;;) {
		int cond_status, rc;

		rc = __sh_exec_list(cmd->cond, &cond_status);
		if (rc) return rc;
		if (__sh_flow_pending()) { *status = cond_status; return 0; }
		if (cmd->until ? (cond_status == 0) : (cond_status != 0)) return 0;

		rc = __sh_exec_list(cmd->body, status);
		if (rc) return rc;
		if (__sh_flow_pending()) return 0;
	}
}

/* 2.9.4 "The for Loop": "the list of words following in shall be
 * expanded to generate a list of items.  Then, the variable name shall
 * be set to each item, in turn, and the compound-list executed each
 * time.  If no items result from the expansion, the compound-list shall
 * not be executed."  Exit status: "the exit status of the last command
 * that executes.  If there are no items, the exit status shall be
 * zero."
 *
 * The words are expanded exactly the way a simple command's arguments
 * are -- one wordexp() per source word with WRDE_APPEND -- so the item
 * list is whatever that call produces and this file never tokenises
 * anything itself (see this file's header on why that reuse is the
 * point).  "for f in *.c" therefore iterates the matched files without
 * being a special case here.
 *
 * One of XCU 2.6's rules is consequently still *not* delivered, and it
 * belongs to src/wordexp/wordexp.c, which states it: the result of a
 * parameter expansion is not field-split, so "for f in $LIST" is one
 * item and not $LIST's fields.  It is not worked around here on
 * purpose: `cmd $LIST` goes through the identical call and gets the
 * identical answer, and a `for` that disagreed with a simple command
 * about what a word expands to would be a worse defect than one
 * consistent documented gap.  test/sh-engine.c pins it, so it inverts
 * visibly when wordexp() grows the behaviour -- which is exactly what
 * happened to the *other* gap this comment used to list: an empty field
 * is deleted now (2.6), so "for f in $UNSET" runs the body zero times
 * rather than once with an empty item.
 *
 * Setting `name` is setenv(), which is what every assignment in this
 * shell already is: the only variable store any expansion here can see
 * is the real `environ` (test/sh-design.md, sh/main.c's header).  The
 * loop variable is therefore *exported* to children, where a real shell
 * would leave it unexported unless something exported it.  That is the
 * pre-existing deviation the whole variable story has, not a new one
 * this construct introduces -- `X=1; cmd` already behaves the same way
 * -- and it is stated here rather than left for someone to find. */
static int exec_for(const struct sh_command *cmd, int *status)
{
	wordexp_t we;
	const struct sh_word *w;
	int first = 1, rc = 0;
	size_t i;

	*status = 0;

	/* 2.9.4: "Omitting: in word ... shall be equivalent to: in "$@"".
	 * Stage 7 gives this shell positional parameters (XCU 2.5.1), so
	 * the equivalence is now something it can actually deliver, and
	 * this used to be the -1 "cannot execute this node" refusal.
	 *
	 * It reads src/sh/param.c's list directly rather than expanding
	 * the literal text "\"$@\"" through __wordexp_sh().  The two agree
	 * -- that is the point of "$@" retaining its fields -- but going
	 * through the expander would make a `for f; do` depend on the
	 * quoting of a string this file synthesised, where reading the
	 * list says what 2.9.4 says: one iteration per positional
	 * parameter, in order, whatever bytes are in it. */
	if (!cmd->have_in) {
		int n = __sh_param_count(), k;
		for (k = 1; k <= n; k++) {
			if (setenv(cmd->name, __sh_param_get(k), 1) < 0) return -1;
			rc = __sh_exec_list(cmd->body, status);
			if (rc) break;
			if (__sh_flow_pending()) break;
		}
		return rc;
	}

	if (!cmd->words) return 0; /* `for f in ; do` -- no items, exit 0 */

	for (w = cmd->words; w; w = w->next) {
		if (__wordexp_sh(w->text, &we, first ? 0 : WRDE_APPEND)) {
			if (!first) wordfree(&we);
			return -1; /* same meaning as in spawn_stage() */
		}
		first = 0;
	}

	for (i = 0; i < we.we_wordc; i++) {
		if (setenv(cmd->name, we.we_wordv[i], 1) < 0) { rc = -1; break; }
		rc = __sh_exec_list(cmd->body, status);
		if (rc) break;
		if (__sh_flow_pending()) break;
	}
	wordfree(&we);
	return rc;
}

/* The body of any compound command, run in this process.  Both callers
 * -- exec_group() for a standalone "(...)"/"{...}"/if/while/for, and
 * exec_group_stage_inline() for one as a pipeline stage -- used to read
 * cmd->body directly, which was right when the only two compound kinds
 * stored their whole body there.  Routing through one dispatcher
 * instead means a kind added later cannot be wired into one of those
 * two paths and silently forgotten in the other; the default arm makes
 * that a reported -1 rather than a silent "ran an empty list, exit 0". */
static int exec_compound(const struct sh_command *cmd, int *status)
{
	switch (cmd->kind) {
	case SH_CMD_SUBSHELL:
	case SH_CMD_BRACE:
		return __sh_exec_list(cmd->body, status);
	case SH_CMD_IF:
		return exec_if(cmd, status);
	case SH_CMD_LOOP:
		return exec_loop(cmd, status);
	case SH_CMD_FOR:
		return exec_for(cmd, status);
	default:
		return -1;
	}
}

static int exec_group(const struct sh_command *cmd, int *status)
{
	struct redir_state rs;
	int failed = 0;
	int rc;
	struct env_snapshot es;
	struct sh_params ps;
	char *oldcwd = 0;
	int is_subshell = cmd->kind == SH_CMD_SUBSHELL;

	rs.saves = 0; rs.n = rs.cap = 0;
	if (apply_redirs(cmd->redirs, &rs, &failed)) { restore_fds(&rs); return -1; }
	if (failed) { restore_fds(&rs); *status = 1; return 0; }

	if (is_subshell) {
		oldcwd = getcwd(0, 0);
		if (env_snapshot_take(&es)) { restore_fds(&rs); return -1; }
		if (params_subshell_enter(&ps)) {
			env_snapshot_restore(&es);
			free_env_snapshot(&es);
			if (oldcwd) __free(oldcwd);
			restore_fds(&rs);
			return -1;
		}
	}

	rc = exec_compound(cmd, status);

	if (is_subshell) {
		/* "( exit 3 )" exits *the subshell*, not the shell (2.9.4
		 * runs the compound-list in a subshell environment, 2.14's
		 * `exit` exits "the shell" it is running in).  The status is
		 * already in *status; consuming the pending unwind here is
		 * what keeps the rest of the caller's program running -- and
		 * a brace group deliberately does not do this, because
		 * "{ exit 3; }" runs "in the current process environment"
		 * (2.9.4) and really is the shell exiting. */
		if (__sh_flow_pending()) __sh_flow_clear();
		__sh_params_install(&ps);
		env_snapshot_restore(&es);
		free_env_snapshot(&es);
		if (oldcwd) { chdir(oldcwd); __free(oldcwd); }
	}
	restore_fds(&rs);
	return rc;
}

/* ==== Command substitution (XCU 2.6.3) =================================
 *
 * "The shell shall expand the command substitution by executing
 * command in a subshell environment (see Shell Execution Environment)
 * and replacing the command substitution (the text of command plus the
 * enclosing "$()" or backquotes) with the standard output of the
 * command, removing sequences of one or more <newline> characters at
 * the end of the substitution." (2.6.3)
 *
 * Two halves, and both of them already exist in this file:
 *
 *  - "in a subshell environment" is the *same* environment 2.12
 *    requires for "( list )", which exec_group() above already
 *    implements as environ/cwd snapshot-and-restore plus the ordinary
 *    redir_state bracketing (see that function group's header comment
 *    for why save-and-restore rather than fork(), and for the exact
 *    intersection of 2.12's object list with what this shell's language
 *    can actually change). This function reuses env_snapshot_take()/
 *    env_snapshot_restore()/save_fd()/restore_fds() unchanged rather
 *    than growing a second, subtly different notion of isolation.
 *
 *  - "with the standard output of the command" is the one genuinely new
 *    piece: the list's stdout has to end up somewhere this process can
 *    read back, rather than at an fd a spawned child would inherit
 *    unchanged.
 *
 * ---- Why a temporary file and not a pipe -----------------------------
 *
 * A pipe is the obvious capture, and it is wrong here for exactly the
 * reason this file's here-document comment already gives for the
 * mirror-image case: __sh_exec_list() runs to completion *in this
 * process* before this function ever gets control back, so there is
 * nobody to drain a pipe while the substituted commands are filling it.
 * A substitution producing more than one pipe buffer (65536 bytes --
 * src/unistd/pipe.c) would wedge this process against itself forever,
 * silently. Draining it concurrently needs a second thread or a fork(),
 * and this file's stage-4 header comment records at length why fork()
 * is deliberately not used anywhere in this executor (stock Wine, which
 * CI's `test` legs run under, aborts on ntdll.RtlCloneUserProcess).
 *
 * tmpfile() (src/stdio/misc.c) has neither problem: it is seekable, so
 * the writers finish first and the read happens after, with no ordering
 * requirement and no size limit beyond the filesystem's; and it is
 * created-then-unlinked, so the capture is gone the moment the last
 * handle to it closes even if this process dies mid-substitution.
 *
 * The trade-off accepted: the substituted command's output makes a
 * round trip through %TEMP% rather than staying in memory, so a
 * substitution needs a writable temp directory (everything else in this
 * file needs only a writable cwd), and the output is not visible to
 * anything until the whole list finishes. The latter costs nothing --
 * 2.6.3's result is the *complete* output, so it could not be produced
 * incrementally anyway -- and the former is the same dependency
 * here-documents already have.
 *
 * ---- What is NOT isolated -------------------------------------------
 *
 * fd 2 is deliberately left alone: 2.6.3 replaces the substitution with
 * the command's *standard output*, and every shell lets a substituted
 * command's stderr through to the shell's own, which is what makes a
 * failing "$(...)" say why.
 */

/* Reads everything left in `fd` into a freshly __malloc'd,
 * NUL-terminated buffer. Returns NULL on OOM or a read error. A capture
 * containing null bytes comes back truncated at the first one as far as
 * any string caller can see -- 2.6.3: "If the output contains any null
 * bytes, the behavior is unspecified." */
static char *slurp_fd(int fd)
{
	char *buf = 0;
	size_t len = 0, cap = 0;

	for (;;) {
		ssize_t n;
		if (len + 4096 + 1 > cap) {
			size_t nc = cap ? cap * 2 : 8192;
			char *nb;
			while (len + 4096 + 1 > nc) nc *= 2;
			nb = __malloc(nc);
			if (!nb) { __free(buf); return 0; }
			if (buf) memcpy(nb, buf, len);
			__free(buf);
			buf = nb;
			cap = nc;
		}
		n = read(fd, buf + len, 4096);
		if (n < 0) { __free(buf); return 0; }
		if (n == 0) break;
		len += (size_t)n;
	}
	if (!buf) { buf = __malloc(1); if (!buf) return 0; }
	buf[len] = 0;
	return buf;
}

int __sh_cmdsub(const char *program, char **out, int *status)
{
	struct sh_list *list;
	struct redir_state rs;
	struct env_snapshot es;
	struct sh_params ps;
	char *oldcwd;
	char *buf;
	FILE *tf;
	int tfd, rc, st = 0;
	size_t len;

	*out = 0;
	list = __sh_parse(program, 0, 0);
	if (!list) return -1;

	tf = tmpfile();
	if (!tf) { __sh_list_free(list); return -1; }
	tfd = fileno(tf);
	if (tfd < 0) { fclose(tf); __sh_list_free(list); return -1; }

	/* Anything this process has buffered for its own stdout belongs on
	 * the *real* stdout, not in the capture -- fd 1 is about to point
	 * somewhere else, and stdio's buffer does not know that. */
	fflush(stdout);

	rs.saves = 0; rs.n = rs.cap = 0;
	if (save_fd(&rs, 1) || dup2(tfd, 1) < 0) {
		restore_fds(&rs);
		fclose(tf);
		__sh_list_free(list);
		return -1;
	}

	oldcwd = getcwd(0, 0);
	if (env_snapshot_take(&es)) {
		__free(oldcwd);
		restore_fds(&rs);
		fclose(tf);
		__sh_list_free(list);
		return -1;
	}
	if (params_subshell_enter(&ps)) {
		env_snapshot_restore(&es);
		free_env_snapshot(&es);
		__free(oldcwd);
		restore_fds(&rs);
		fclose(tf);
		__sh_list_free(list);
		return -1;
	}

	rc = __sh_exec_list(list, &st);

	/* 2.6.3 runs the substituted command "in a subshell environment",
	 * so "$(exit 3)" is a substitution whose status is 3, never the
	 * calling shell exiting. */
	if (__sh_flow_pending()) __sh_flow_clear();
	__sh_params_install(&ps);
	env_snapshot_restore(&es);
	free_env_snapshot(&es);
	if (oldcwd) { chdir(oldcwd); __free(oldcwd); }
	fflush(stdout);	/* while fd 1 is still the capture */
	restore_fds(&rs);
	__sh_list_free(list);

	if (rc) { fclose(tf); return -1; }

	if (lseek(tfd, 0, SEEK_SET) < 0) { fclose(tf); return -1; }
	buf = slurp_fd(tfd);
	fclose(tf);
	if (!buf) return -1;

	/* 2.6.3: "removing sequences of one or more <newline> characters at
	 * the end of the substitution. Embedded <newline> characters before
	 * the end of the output shall not be removed". */
	len = strlen(buf);
	while (len && buf[len - 1] == '\n') len--;
	buf[len] = 0;

	*out = buf;
	*status = st;
	cmdsub_status = st;
	cmdsub_generation++;
	return 0;
}

int __sh_exec_command(const struct sh_command *cmd, int *status)
{
	if (cmd->kind == SH_CMD_SIMPLE) return exec_simple(cmd, status);
	return exec_group(cmd, status);
}

/* ==== Pipelines of any length (XCU 2.9.2) =============================== */

/* Runs a compound-command pipeline stage's body in this process, once
 * every real (SH_CMD_SIMPLE) stage in the pipeline has already been
 * spawned -- see exec_group()'s header comment above ("As one stage of
 * a multi-command pipeline: still no fork()") for why that ordering is
 * what makes doing this in-process safe here. The caller has already
 * dup2()'d this stage's pipe ends onto fd 0/1 for it, exactly as it
 * does for a spawned simple-command stage; this function applies
 * `cmd`'s own redirections on top of that (same left-to-right ordering
 * apply_redirs() always gives), runs the body with environ/cwd
 * save-and-restore around it (2.12 applies subshell-environment
 * semantics to every pipeline stage regardless of "(...)" vs "{...}",
 * unlike the standalone case exec_group() handles), and restores this
 * process's fds and environ/cwd before returning -- there being no
 * child process whose exit already discarded all of that for free.
 * Returns 0 with *status set to the body's own exit status (the
 * pipeline loop's existing per-stage struct stage_result is not needed
 * here: this always finishes before returning, unlike a spawned stage,
 * so there is no pid to wait for later), or -1 if `cmd`'s own
 * redirections fail to apply (the caller folds that into the same
 * abort_unsupported path a spawn_stage() OOM would take) or if the
 * body itself hits something this shell cannot execute (propagated the
 * same way __sh_exec_command()'s other callers already propagate it). */
static int exec_group_stage_inline(const struct sh_command *cmd, int *status)
{
	struct redir_state rs;
	int failed = 0;
	struct env_snapshot es;
	struct sh_params ps;
	char *oldcwd;
	int rc;

	rs.saves = 0; rs.n = rs.cap = 0;
	if (apply_redirs(cmd->redirs, &rs, &failed)) { restore_fds(&rs); return -1; }
	if (failed) { restore_fds(&rs); *status = 1; return 0; }

	oldcwd = getcwd(0, 0);
	if (env_snapshot_take(&es)) { __free(oldcwd); restore_fds(&rs); return -1; }
	if (params_subshell_enter(&ps)) {
		env_snapshot_restore(&es);
		free_env_snapshot(&es);
		__free(oldcwd);
		restore_fds(&rs);
		return -1;
	}

	rc = exec_compound(cmd, status);

	/* 2.12 puts every stage of a multi-command pipeline in a subshell
	 * environment regardless of "(...)" vs "{...}", so an `exit` in
	 * this stage's body exits that subshell only -- unlike the
	 * standalone brace group exec_group() above deliberately lets
	 * through. */
	if (__sh_flow_pending()) __sh_flow_clear();
	__sh_params_install(&ps);
	env_snapshot_restore(&es);
	free_env_snapshot(&es);
	if (oldcwd) { chdir(oldcwd); __free(oldcwd); }
	restore_fds(&rs);
	return rc;
}

/* Wires this stage's fd 0/1 onto its neighboring pipe ends exactly as
 * 2.9.2 requires (see __sh_exec_pipeline()'s inline comment at its
 * call sites below), save_fd()-protected so the caller's restore_fds()
 * undoes just this. Returns -1 (nothing left half-wired: save_fd()
 * either fully recorded a slot before failing or didn't touch `rs` at
 * all) on OOM, 0 otherwise. Split out because both pass 1 (a
 * SH_CMD_SIMPLE stage, wired and spawned immediately) and pass 2 (a
 * deferred compound-command stage, wired again once it is finally its
 * turn to run -- see exec_group()'s header comment above) need
 * identical wiring, just at different times. */
static int wire_stage_stdio(struct redir_state *rs, int (*pipes)[2], size_t n, size_t i)
{
	if (i > 0) {
		if (save_fd(rs, 0)) return -1;
		dup2(pipes[i - 1][0], 0);
	}
	if (i + 1 < n) {
		if (save_fd(rs, 1)) return -1;
		dup2(pipes[i][1], 1);
	}
	return 0;
}

int __sh_exec_pipeline(const struct sh_pipeline *pl, int *status)
{
	size_t n = pl->ncommands, i;
	int (*pipes)[2] = 0;
	pid_t *pids = 0;
	int *statuses = 0;
	unsigned char *deferred = 0; /* 1 iff commands[i] is a compound
	                                command still waiting for pass 2 */
	int abort_unsupported = 0;
	int rc;

	/* The grammar (sh.h's sh_pipeline comment, XCU 2.10.2's pipeline
	 * production) never produces an empty pipeline -- a pipe_sequence
	 * is always one command at minimum -- so this cannot happen via
	 * __sh_parse(). It is still checked explicitly, rather than left
	 * as an unstated invariant: every allocation and index below is
	 * sized off n or n-1, and a static analyzer (rightly) cannot see
	 * the parser's guarantee across this function's own boundary --
	 * only that an unchecked n==0 would underflow n-1 to SIZE_MAX. */
	if (n == 0) return -1;

	if (n == 1) {
		/* Routes back through __sh_exec_command so a lone subshell or
		 * brace group ("(echo hi)", "{ echo hi; }") still reports
		 * stage 4's -1 rather than this file assuming SH_CMD_SIMPLE. */
		rc = __sh_exec_command(&pl->commands[0], status);
		if (rc) return rc;
		if (pl->bang) *status = (*status == 0);
		sh_last_status = *status;
		return 0;
	}

	/* Two compound-command stages directly adjacent, with no real
	 * spawned stage between them, is the one case this file's header
	 * comment above ("still no fork()") cannot make safe: neither side
	 * can run concurrently with the other without a fork() this file
	 * deliberately no longer uses, so whichever runs first could block
	 * forever on more than one pipe buffer with nothing on the other
	 * end to drain or fill it. Refused up front, before anything is
	 * allocated or spawned, via the same -1 "not yet supported"
	 * convention as an unexpanded command substitution -- a clean,
	 * reported failure instead of a silent hang. Every other placement
	 * of "(...)"/"{...}" in a pipeline is unaffected. */
	for (i = 0; i + 1 < n; i++) {
		if (pl->commands[i].kind != SH_CMD_SIMPLE && pl->commands[i + 1].kind != SH_CMD_SIMPLE)
			return -1;
	}

	pids = __malloc(n * sizeof *pids);
	statuses = __malloc(n * sizeof *statuses);
	pipes = __malloc((n - 1) * sizeof *pipes);
	deferred = __malloc(n * sizeof *deferred);
	if (!pids || !statuses || !pipes || !deferred) {
		__free(pids); __free(statuses); __free(pipes); __free(deferred);
		return -1;
	}
	memset(deferred, 0, n * sizeof *deferred);

	/* Every pipe is created up front, O_CLOEXEC (see this file's
	 * header comment on why): if creation fails partway through, only
	 * the ones already made need cleaning up -- nothing has been
	 * spawned yet. */
	for (i = 0; i + 1 < n; i++) {
		if (pipe2(pipes[i], O_CLOEXEC) < 0) {
			size_t j;
			for (j = 0; j < i; j++) { close(pipes[j][0]); close(pipes[j][1]); }
			__free(pids); __free(statuses); __free(pipes); __free(deferred);
			return -1;
		}
	}

	/* Pass 1: spawn every real (SH_CMD_SIMPLE) stage, in order, so
	 * that by the time pass 2 below runs any compound-command stage's
	 * body, every stage that could possibly be on the other end of one
	 * of its pipes already exists and is running concurrently. A
	 * compound-command stage is left entirely untouched here -- no
	 * fd 0/1 wiring, no redirections, no closing of its neighboring
	 * pipe ends -- because it is not this process's turn to use them
	 * yet; pass 2 below does all of that when it finally is. */
	for (i = 0; i < n; i++) {
		struct redir_state rs;
		struct stage_result sr;
		int failed = 0;
		unsigned long gen0 = cmdsub_generation;

		sr.pid = -1;
		sr.status = 0;
		sr.had_name = 1;

		if (pl->commands[i].kind != SH_CMD_SIMPLE) {
			deferred[i] = 1;
			continue;
		}

		rs.saves = 0; rs.n = rs.cap = 0;
		if (!abort_unsupported) {
			/* "For each command but the last, the shell shall connect
			 * the standard output of the command to the standard
			 * input of the next command" (2.9.2), applied *before*
			 * the command's own redirs list so that e.g. "cmd 2>&1 |
			 * next" merges cmd's stderr into the pipe (2.7's
			 * left-to-right ordering then makes the explicit "2>&1"
			 * apply on top of this implicit hookup, which is what
			 * makes that merge happen at all). */
			if (wire_stage_stdio(&rs, pipes, n, i)) {
				abort_unsupported = 1;
			} else if (apply_redirs(pl->commands[i].redirs, &rs, &failed)) {
				abort_unsupported = 1;
			} else if (failed) {
				/* 2.8.1: this stage fails without running, same
				 * as a lone command's redirection error -- but
				 * the rest of the pipeline still runs normally
				 * (its reader just sees an immediate EOF from
				 * this stage's never-written pipe end). */
				sr.status = 1;
			} else if (run_stage(&pl->commands[i], &sr, 0)) {
				abort_unsupported = 1;
			}
			restore_fds(&rs);
		}

		/* Close this process's own copies of the pipe ends this stage
		 * has now handed off to a spawned child (or would have, had
		 * one run): the write end pipes[i][1] is done with the moment
		 * command i has been spawned, the read end pipes[i-1][0] the
		 * moment command i has. Unconditional, whether or not this
		 * stage actually ran -- an aborted or a redirection-failed
		 * stage must free these exactly like a successful one, or the
		 * pipeline hangs (this file's header comment's "leaked write
		 * end" case) or leaks (a never-closed read end) regardless. A
		 * deferred stage's own neighboring ends are deliberately left
		 * open here (and skipped entirely, via the `continue` above,
		 * before ever reaching this point) -- pass 2 below still needs
		 * them. */
		if (i > 0) close(pipes[i - 1][0]);
		if (i + 1 < n) close(pipes[i][1]);

		pids[i] = sr.pid;
		statuses[i] = sr.pid < 0 ? cmdsub_status_rule(&sr, gen0) : sr.status;
	}

	/* Pass 2: run every compound-command stage's body, left to right,
	 * now that pass 1 has spawned every real stage in the pipeline.
	 * See this file's header comment above for why left-to-right order
	 * is what keeps this safe: whichever neighbor produces a deferred
	 * stage's input or consumes its output is by now either a real,
	 * already-running process, or an earlier deferred stage that (by
	 * this same order) has already finished and closed its end. */
	for (i = 0; i < n; i++) {
		struct redir_state rs;
		int st = 0;

		if (!deferred[i]) continue;

		if (!abort_unsupported) {
			rs.saves = 0; rs.n = rs.cap = 0;
			if (wire_stage_stdio(&rs, pipes, n, i)) {
				abort_unsupported = 1;
			} else if (exec_group_stage_inline(&pl->commands[i], &st)) {
				abort_unsupported = 1;
			}
			restore_fds(&rs);
		}

		if (i > 0) close(pipes[i - 1][0]);
		if (i + 1 < n) close(pipes[i][1]);

		pids[i] = -1;
		statuses[i] = st;
	}

	/* Every pipe end in this process has been closed by the two
	 * passes above; what remains open is only each spawned child's own
	 * inherited copy, which is exactly what lets a downstream reader
	 * see EOF once its writers actually finish. */
	for (i = 0; i < n; i++) {
		if (pids[i] < 0) continue;
		rc = wait_status(pids[i]);
		statuses[i] = rc; /* -1 here would be a waitpid() failure on a
		                   * pid this process itself just created --
		                   * not expected, but if it somehow happens,
		                   * propagating it as this stage's status is
		                   * safer than fabricating a fake success. */
	}

	rc = statuses[n - 1];
	__free(pids);
	__free(statuses);
	__free(pipes);
	__free(deferred);

	if (abort_unsupported) return -1; /* *status left untouched, per this file's convention */

	*status = pl->bang ? (rc == 0) : rc;
	sh_last_status = *status;
	return 0;
}

int __sh_exec_andor(const struct sh_andor *a, int *status)
{
	int rc = __sh_exec_pipeline(&a->pipeline, status);
	if (rc) return rc;
	for (a = a->next; a; a = a->next) {
		/* An `exit` anywhere in the and-or list ends it, whatever the
		 * status would have selected next -- see sh.h's control-flow
		 * comment.  Checked before the short-circuit tests so that
		 * "exit 0 && cmd" runs no cmd. */
		if (flow_exit_pending) return 0;
		if (a->op == SH_AO_AND && *status != 0) continue;
		if (a->op == SH_AO_OR && *status == 0) continue;
		rc = __sh_exec_pipeline(&a->pipeline, status);
		if (rc) return rc;
	}
	return 0;
}

/* Nesting depth of __sh_exec_list().  A pending `exit` must survive
 * every *inner* return -- that is the whole point of the unwind -- but
 * must not survive the outermost one, or the next program this process
 * runs (a later system()/wordexp() command substitution, or a second
 * __sh_exec_list() in a test) would find the flag still set and execute
 * nothing at all, silently.  Clearing it exactly where the unwind has
 * nowhere left to unwind to is what makes the flag a control-flow
 * signal rather than a latch. */
static unsigned exec_list_depth;

int __sh_exec_list(const struct sh_list *list, int *status)
{
	const struct sh_list_item *it;
	int rc = 0;
	*status = 0;
	if (!list) return 0;
	exec_list_depth++;
	for (it = list->items; it; it = it->next) {
		rc = __sh_exec_andor(it->andor, status);
		if (rc) break;
		if (flow_exit_pending) break;
		/* SH_SEP_AMP: true backgrounding is future work -- see this
		 * file's header comment -- so an async item still just runs
		 * synchronously for now, exactly like SH_SEP_SEQ/SH_SEP_END. */
	}
	if (--exec_list_depth == 0) flow_exit_pending = 0;
	return rc;
}
