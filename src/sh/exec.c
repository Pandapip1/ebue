/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 3: redirections and multi-command pipelines, on top of stage
 * 2's simple-command execution. PATH lookup goes through the existing
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
 * subtly wrong. A word containing command substitution ($(...) or
 * `...`, real syntax since parse.c's word-boundary fix) surfaces as
 * WRDE_CMDSUB here and is reported as "not yet supported": stage 5 is
 * what wires wordexp's command-substitution call-out to this module's
 * own list execution, which is exactly what turns that error into a
 * result.
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
 *   - subshells / brace groups (stage 4; needs the fork() this file's
 *     header comment says a plain simple command does not, and a
 *     pipeline does not either)
 *   - command substitution inside any word, redirection target, or
 *     unquoted here-document body (stage 5) -- reported via wordexp()'s
 *     WRDE_CMDSUB, same as stage 2
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
 * $var in a value expanding more than a strict implementation would;
 * tightening this is exactly the sort of thing stage 5's extraction of
 * wordexp's inlined quote-scan (see test/sh-design.md's stage-5 notes)
 * is for. If expansion fails (most commonly WRDE_CMDSUB -- command
 * substitution in an assignment is not yet supported either), the
 * value falls back to its literal raw text rather than the assignment
 * being silently dropped. On success, *name and *val are __malloc'd
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

	if (eq[1] && wordexp(eq + 1, &we, 0) == 0) {
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
 * not a child's. Always "succeeds" (status 0): 2.9.1 gives no failure
 * mode for a bare assignment. */
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
	if (wordexp(raw, &we, 0)) { *unsupported = 1; return 0; }
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
 * body correctly comes back as this file's usual WRDE_CMDSUB "not yet
 * supported" (stage 5). Returns NULL and sets *unsupported on failure. */
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

	if (wordexp(syn, &we, 0)) { __free(syn); *unsupported = 1; return 0; }
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
};

/* Finds and starts the program named by cmd->words (which must be
 * non-NULL -- the assignment-only case is handled by the caller, see
 * run_stage() below), but does not wait for it: a pipeline needs every
 * stage spawned before it waits for any of them (see this file's
 * header comment on why), and a lone command's caller waits
 * immediately afterward instead. Returns -1 ("cannot execute this
 * yet") for a word that needs command substitution or on OOM, exactly
 * as stage 2 did; otherwise 0 with *out filled in. */
static int spawn_stage(const struct sh_command *cmd, struct stage_result *out)
{
	wordexp_t we;
	const struct sh_word *w;
	int first = 1, rc;
	char *resolved;
	char **envp = 0;
	size_t envn = 0;

	out->pid = -1;

	/* run_stage() never calls this with cmd->words == NULL (that is
	 * exactly the assignment-only case it handles itself), but a
	 * static analyzer cannot see across the call boundary and flags
	 * `we` as read uninitialized on a hypothetical zero-iteration
	 * loop below -- so this makes the invariant an explicit, checked
	 * fact rather than something only a comment promises. */
	if (!cmd->words) { out->status = 0; return 0; }

	for (w = cmd->words; w; w = w->next) {
		rc = wordexp(w->text, &we, first ? 0 : WRDE_APPEND);
		if (rc) {
			if (!first) wordfree(&we);
			return -1; /* most commonly WRDE_CMDSUB -- stage 5 */
		}
		first = 0;
	}
	if (we.we_wordc == 0) { wordfree(&we); out->status = 0; return 0; } /* every word expanded away */

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
		if (env_mutate) exec_assignment_only(cmd, &out->status);
		else out->status = 0;
		return 0;
	}
	return spawn_stage(cmd, out);
}

static int wait_status(pid_t pid)
{
	int wstatus;
	if (waitpid(pid, &wstatus, 0) < 0) return -1;
	return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
}

/* ==== A single simple command, including its own redirections ========== */

static int exec_simple(const struct sh_command *cmd, int *status)
{
	struct redir_state rs;
	int failed = 0;
	struct stage_result sr;
	int st;

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

	if (sr.pid < 0) { *status = sr.status; return 0; }
	st = wait_status(sr.pid);
	if (st < 0) return -1;
	*status = st;
	return 0;
}

int __sh_exec_command(const struct sh_command *cmd, int *status)
{
	if (cmd->kind != SH_CMD_SIMPLE) return -1; /* stage 4: subshell/brace */
	return exec_simple(cmd, status);
}

/* ==== Pipelines of any length (XCU 2.9.2) =============================== */

int __sh_exec_pipeline(const struct sh_pipeline *pl, int *status)
{
	size_t n = pl->ncommands, i;
	int (*pipes)[2] = 0;
	pid_t *pids = 0;
	int *statuses = 0;
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
		return 0;
	}

	for (i = 0; i < n; i++)
		if (pl->commands[i].kind != SH_CMD_SIMPLE) return -1; /* stage 4 */

	pids = __malloc(n * sizeof *pids);
	statuses = __malloc(n * sizeof *statuses);
	pipes = __malloc((n - 1) * sizeof *pipes);
	if (!pids || !statuses || !pipes) {
		__free(pids); __free(statuses); __free(pipes);
		return -1;
	}

	/* Every pipe is created up front, O_CLOEXEC (see this file's
	 * header comment on why): if creation fails partway through, only
	 * the ones already made need cleaning up -- nothing has been
	 * spawned yet. */
	for (i = 0; i + 1 < n; i++) {
		if (pipe2(pipes[i], O_CLOEXEC) < 0) {
			size_t j;
			for (j = 0; j < i; j++) { close(pipes[j][0]); close(pipes[j][1]); }
			__free(pids); __free(statuses); __free(pipes);
			return -1;
		}
	}

	for (i = 0; i < n; i++) {
		struct redir_state rs;
		struct stage_result sr;
		int failed = 0;

		sr.pid = -1;
		sr.status = 0;
		rs.saves = 0; rs.n = rs.cap = 0;

		if (!abort_unsupported) {
			/* "For each command but the last, the shell shall connect
			 * the standard output of the command to the standard
			 * input of the next command" (2.9.2). This is just two
			 * more save_fd()-protected redirections, applied *before*
			 * the command's own redirs list so that e.g. "cmd 2>&1 |
			 * next" merges cmd's stderr into the pipe (2.7's
			 * left-to-right ordering then makes the explicit "2>&1"
			 * apply on top of this implicit hookup, which is what
			 * makes that merge happen at all). */
			if (i > 0) {
				if (save_fd(&rs, 0)) abort_unsupported = 1;
				else dup2(pipes[i - 1][0], 0);
			}
			if (!abort_unsupported && i + 1 < n) {
				if (save_fd(&rs, 1)) abort_unsupported = 1;
				else dup2(pipes[i][1], 1);
			}

			if (!abort_unsupported) {
				if (apply_redirs(pl->commands[i].redirs, &rs, &failed)) {
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
		 * end" case) or leaks (a never-closed read end) regardless. */
		if (i > 0) close(pipes[i - 1][0]);
		if (i + 1 < n) close(pipes[i][1]);

		pids[i] = sr.pid;
		statuses[i] = sr.status;
	}

	/* Every pipe end in this process has been closed by the loop
	 * above; what remains open is only each spawned child's own
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

	if (abort_unsupported) return -1; /* *status left untouched, per this file's convention */

	*status = pl->bang ? (rc == 0) : rc;
	return 0;
}

int __sh_exec_andor(const struct sh_andor *a, int *status)
{
	int rc = __sh_exec_pipeline(&a->pipeline, status);
	if (rc) return rc;
	for (a = a->next; a; a = a->next) {
		if (a->op == SH_AO_AND && *status != 0) continue;
		if (a->op == SH_AO_OR && *status == 0) continue;
		rc = __sh_exec_pipeline(&a->pipeline, status);
		if (rc) return rc;
	}
	return 0;
}

int __sh_exec_list(const struct sh_list *list, int *status)
{
	const struct sh_list_item *it;
	*status = 0;
	if (!list) return 0;
	for (it = list->items; it; it = it->next) {
		int rc = __sh_exec_andor(it->andor, status);
		if (rc) return rc;
		/* SH_SEP_AMP: true backgrounding is future work -- see this
		 * file's header comment -- so an async item still just runs
		 * synchronously for now, exactly like SH_SEP_SEQ/SH_SEP_END. */
	}
	return 0;
}
