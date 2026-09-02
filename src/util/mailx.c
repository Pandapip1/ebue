/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mailx(1p): a real local mail client over a real mbox-format spool --
 * not the toy this project's own POSIX-utilities plan (see
 * luminous-orbiting-biscuit.md) originally deferred mailx as needing
 * "infrastructure this plan doesn't build".  That infrastructure (an
 * mbox reader/writer, real advisory locking) is genuinely small enough
 * to build here; what is explicitly, permanently out of scope is an
 * MTA/SMTP relay -- a real network mail-transfer agent is a whole
 * separate project, not a missing afternoon of work.
 *
 * ============================================================
 * SCOPE: WHAT "SENDING MAIL" HONESTLY MEANS HERE
 * ============================================================
 *
 * Real mailx(1p) sends by handing the composed message to sendmail(1)
 * (or an equivalent already on PATH), which then does the actual
 * routing/relay/queueing -- mailx itself never touches the wire.  This
 * platform has no MTA at all, static or otherwise, and building one
 * (even a "deliver to a local Unix-domain/named-pipe queue" one) is a
 * separate, much bigger project than mailx itself.  Rather than shell
 * out to a sendmail that doesn't exist, or fabricate a fake one that
 * silently drops mail, this implementation does real *local* delivery:
 * appending the composed message directly onto a real mbox file on
 * disk, on this same machine, using the recipient's own mailbox path.
 *
 * "The recipient's own mailbox" is a real, meaningful concept here for
 * exactly one recipient: src/misc/pwd.c's own header comment establishes
 * that this library's single-real-uid NT/Linux model means there is
 * genuinely, provably exactly one local user this process can ever be
 * running as -- getpwuid(getuid()) is authoritative, not a guess.  So
 * "local delivery" here means: an address that names that one real user
 * (by login name, ignoring any "@host" suffix -- there is no host to
 * resolve against) is delivered for real, by appending to that user's
 * own mailbox file; any other address is refused with a loud, honest
 * "user unknown" diagnostic and a nonzero exit, the same way a real
 * sendmail bounces mail for a name its aliases database doesn't know --
 * never silently dropped, never faked as delivered. This is the direct
 * mail-specific analogue of getpwnam()'s own "any *other* name ... is
 * refused cleanly ... rather than answered with a fabricated record"
 * rule.
 *
 * ============================================================
 * MAILBOX PATHS
 * ============================================================
 *
 * Two distinct mailbox files, matching mailx.html's own DESCRIPTION
 * (Mailboxes / Message Handling):
 *
 *   system mailbox  -- where new mail is delivered (Send Mode's target,
 *                       and Receive Mode's default source).  This
 *                       implementation has no shared, permission-
 *                       separated /var/mail-style spool directory to
 *                       put it in (there is exactly one real user per
 *                       the above, so there is nothing to separate it
 *                       *from* -- and NT has no such directory
 *                       convention at all), so the default is
 *                       `<pw_dir>/mailbox` (pw_dir = getpwuid()'s
 *                       pw_dir, i.e. %USERPROFILE%, per pwd.c), overridable
 *                       with the $MAIL environment variable exactly as
 *                       mailx.html's ENVIRONMENT VARIABLES section
 *                       describes for MAIL ("Determine the name of the
 *                       system mailbox").
 *   mbox            -- the secondary mailbox `-f` reads by default, and
 *                       where a `q` quit-save that started from the
 *                       system mailbox would traditionally migrate read-
 *                       but-kept messages to (see "DEFERRED" below for
 *                       why this implementation does not do that
 *                       migration).  Default `<pw_dir>/mbox` exactly
 *                       per mailx.html's own MBOX description,
 *                       overridable with $MBOX.
 *
 * ============================================================
 * MBOX FORMAT: READ/WRITE, ESCAPING, AND LOCKING
 * ============================================================
 *
 * A message begins at a line that is exactly "From " (capital F, three
 * lowercase letters, one space) followed by an envelope sender and a
 * date, and which is either the first line of the file or immediately
 * preceded by an empty line -- mailx.html's own MBOX FORMAT clause,
 * quoted in parse_mbox() below.  Any body line a sender supplies that
 * itself begins "From " would be indistinguishable from a real
 * boundary, so it is escaped on write by prefixing a single '>' --
 * mailx.html: "mailx shall modify any such user-entered message body
 * lines ... by adding one or more characters to precede the 'F'."  This
 * implementation does not attempt to *unescape* on read (the
 * unescaping-safe "mboxrd" convention needs its own extra encode rule
 * this implementation does not add): a body line that was itself
 * user-typed as ">From ..." and a real escaped "From " line are not
 * distinguished on the way back out.  This is the same lossy corner
 * every traditional "mboxo"-family implementation has always had, not
 * a new gap -- documented rather than silently present.
 *
 * Concurrent-append safety is real, via flock() (include/sys/file.h;
 * see src/file/flock.c's own header for why it is mandatory rather than
 * advisory on the NT backend, and simply POSIX-advisory-but-universally-
 * honored on Linux): every append acquires LOCK_EX on the destination
 * file descriptor before it even inspects the current end-of-file state
 * (ensure_blank_terminated() below), and holds it across the entire
 * append; every read session that might rewrite the file (a `q` with
 * pending deletions) holds LOCK_EX for the whole session. Two mailx
 * processes appending at once are therefore fully serialized -- the
 * second's flock() call blocks until the first's append (state-check
 * and write together) is completely done -- so the file can never end
 * up with two messages' bytes interleaved. test/util-mail.c proves this
 * against real concurrent child processes, not merely asserts it.
 *
 * ============================================================
 * WHAT'S IMPLEMENTED, PRECISELY, AND WHAT IS DEFERRED
 * ============================================================
 *
 * Options: -s subject, -f [as a bare flag; the optional file operand
 * follows normally, matching mailx.html's SYNOPSIS line `mailx -f
 * [-HiNn] [-F] [file]` where -f itself takes no attached argument],
 * -H (headers only), -N (suppress the initial header summary), -u user
 * (refused unless user names the one real local user, per the scope
 * note above), -e (test for mail, per EXIT STATUS's -e clause), -i
 * (ignore SIGINT, real via signal(), same shape as src/util/tee.c's own
 * -i). -n is accepted as a documented no-op (see cat's -u for the
 * precedent of accepting rather than refusing a flag that controls
 * behavior this implementation never has anyway): the "system default
 * start-up file" -n says not to read is $MAILRC/.mailrc, which nothing
 * here ever reads regardless of this flag, so there is truly nothing
 * left for -n to disable. -F (record into a file named after the first
 * recipient) is refused outright (__util_diagf() + nonzero exit),
 * per this project's "an unsupported option must not look like it
 * worked" rule (src/sh/builtin.c's bi_set(), src/util/touch.c's -d) --
 * not implemented, not silently ignored.
 *
 * Send Mode: matches mailx.html's DESCRIPTION exactly for the
 * mechanically-important, scriptable case this task explicitly
 * prioritizes -- reads the entire message body from standard input up
 * to EOF. The `~.`-escape body-command language (User Portability
 * Utilities option, itself separately option-gated in the spec, exactly
 * the kind of optional-marker this project's own OPTS ledger already
 * distinguishes from mandatory behavior -- see
 * [[project_opts_expected_libc_test_distinction]]) is not implemented;
 * real EOF (a script's pipe closing, or Ctrl-D at a terminal) is the
 * only way to end a message body here. If -s is not given and stdin is
 * a terminal, a `Subject: ` prompt is read interactively; if stdin is
 * not a terminal, the Subject header is simply omitted, matching this
 * task's explicit priority (non-interactive scripting correctness
 * first).
 *
 * Receive Mode: real mbox parsing (parse_mbox() below), a real header
 * summary, and an interactive command loop implementing exactly the
 * minimum command set this task named explicitly: p/print (also the
 * default action for a bare message number), n/next (also a blank
 * input line, per mailx.html's `next` description), d/delete,
 * u/undelete, h/headers (redisplay the summary), q/quit (rewrite the
 * mailbox with deleted messages actually removed -- see below for the
 * one deliberate scope-narrowing on this), x/exit/ex (quit leaving the
 * file untouched), plus '=' (show the current message number), '#...'
 * (comment, a no-op) and '?' (a short command list) since they cost
 * nothing extra once the loop exists. A msglist is a bare message
 * number or empty (meaning "the current message", advancing to the
 * next after a print); '*' (all messages) and '$' (the last message)
 * are also accepted. Full msglist ranges/arithmetic (`n-m`, `+`, `-`,
 * `^`) are NOT implemented -- deferred, documented here rather than
 * guessed at, per this task's own explicit "interactive reader can be
 * simpler/more minimal if time is tight" allowance.
 *
 * One deliberate scope-narrowing on `q`: real mailx, when quitting out
 * of the *system* mailbox, migrates messages that were read-but-not-
 * deleted into mbox and leaves only still-unread messages in the system
 * mailbox (mailx.html's `quit` command). This implementation always
 * does the mechanically simpler, still entirely real and correct thing:
 * `q` rewrites whichever file was actually opened (system mailbox, -f
 * file, or -u's mailbox) in place, keeping every message not marked
 * deleted in that same file and discarding only the ones that were.
 * There is no separate "read but kept" vs "still new" distinction
 * tracked across sessions (no Status: header is written), so every
 * message in a freshly opened mailbox displays as a plain, un-annotated
 * entry rather than carrying an O/N/R status letter. Both narrowings
 * are named explicitly here rather than silently approximated.
 *
 * Never calls exit()/_exit() -- __util_mailx_main() runs as a shell
 * built-in too (src/sh/builtin.c's bi_mailx()), same rule as every
 * other utility in src/util/ (see src/util/dd.c's own header for the
 * established precedent this file follows).
 *
 * Spec consulted: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/mailx.html
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "util.h"
#include "ownership_stubs.h"

/* ==================== small shared helpers ==================== */

/* A write() that makes no progress at all (0, an error, or, defensively,
 * an impossible over-long count) is a real error, never retried forever. */
static int write_all(int fd, const char *buf withtok(readable_span(len)),
	size_t len, const char *label)
{
	size_t off = 0;

	while (off < len) {
		ssize_t w = write(fd, buf + off, len - off);
		if (w <= 0 || (size_t)w > len - off) {
			if (w >= 0) errno = EIO;
			__util_diagf("mailx: %s: %s\n", label, strerror(errno));
			return -1;
		}
		off += (size_t)w;
	}
	return 0;
}

/* Reads all of fd into a NUL-terminated heap buffer (the NUL is beyond
 * *outlen, never counted in it -- callers that want a body treat this
 * purely as a byte span). Returns 0/-1; on success *outbuf and *outlen
 * are always valid (an empty stream yields a 0-length, non-NULL buffer). */
static int slurp_fd(int fd, char **outbuf, size_t *outlen)
{
	size_t cap = 65536, len = 0;
	char *buf = malloc(cap);
	if (!buf) { errno = ENOMEM; return -1; }
	for (;;) {
		ssize_t n;
		if (len + 4096 > cap) {
			size_t ncap;
			char *nb;
			if (!__util_size_mul(cap, 2, &ncap)) { free(buf); errno = ENOMEM; return -1; }
			nb = realloc(buf, ncap);
			if (!nb) { free(buf); errno = ENOMEM; return -1; }
			buf = nb;
			cap = ncap;
		}
		__ownership_writable_span(buf + len, cap - len - 1);
		n = read(fd, buf + len, cap - len - 1);
		if (n < 0) { free(buf); return -1; }
		if (n == 0) break;
		len += (size_t)n;
	}
	buf[len] = 0;
	*outbuf = buf;
	*outlen = len;
	return 0;
}

/* Join `dir` and `leaf` with exactly one '/' between them, the same
 * need_slash technique src/util/cp.c's __util_join_basename() uses (a
 * plain '/' works regardless of whether `dir` is an NT-style
 * backslash-separated path or a Linux one -- open() accepts '/'
 * either way on this platform's own backends). */
static int join_path(const char *dir, const char *leaf, char *out, size_t outsz)
{
	size_t dl = strlen(dir);
	int need_slash = dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\';
	int n = snprintf(out, outsz, need_slash ? "%s/%s" : "%s%s", dir, leaf);
	return (n < 0 || (size_t)n >= outsz) ? -1 : 0;
}

/* The one real local user's own record -- getpwuid(getuid()) is
 * authoritative per src/misc/pwd.c's own header (getuid() never
 * disagrees with the token this process is actually running as). NULL
 * (with a diagnostic already printed) only in the "practically never"
 * case pwd.c itself documents: neither %USERNAME% nor %USER% set. */
static struct passwd *current_user(void)
{
	struct passwd *pw = getpwuid(getuid());
	if (!pw) __util_diagf("mailx: cannot determine the current user (see pwd(3)/getlogin(3))\n");
	return pw;
}

/* True if `addr` names the current user -- comparing only the part
 * before an optional "@host" (there is no host to resolve against, so
 * the host part, if any, is simply not examined) against pw->pw_name. */
static int addr_is_current_user(const struct passwd *pw, const char *addr)
{
	const char *at = strchr(addr, '@');
	size_t namelen = at ? (size_t)(at - addr) : strlen(addr);
	return strlen(pw->pw_name) == namelen && strncmp(pw->pw_name, addr, namelen) == 0;
}

static int system_mailbox_path(const struct passwd *pw, char *out, size_t outsz)
{
	const char *mail = getenv("MAIL");
	if (mail && *mail) {
		if (strlen(mail) >= outsz) return -1;
		strcpy(out, mail);
		return 0;
	}
	return join_path(pw->pw_dir, "mailbox", out, outsz);
}

static int secondary_mailbox_path(const struct passwd *pw, char *out, size_t outsz)
{
	const char *mbox = getenv("MBOX");
	if (mbox && *mbox) {
		if (strlen(mbox) >= outsz) return -1;
		strcpy(out, mbox);
		return 0;
	}
	return join_path(pw->pw_dir, "mbox", out, outsz);
}

/* ==================== Send Mode ==================== */

/* Appends body onto `out`, escaping any line that begins "From " (mbox
 * FORMAT's own boundary rule -- see this file's own top-of-file
 * comment) by prefixing a single '>'. Ensures the result ends with
 * exactly one trailing newline (so the next message's own From_ line,
 * whenever it is appended, is unambiguously "preceded by an empty
 * line" once ensure_blank_terminated() adds the separating blank line
 * -- see that function). Returns 0/-1 (malloc failure only). */
static int append_escaped_body(char **out, size_t *outlen, size_t *outcap,
	const char *restrict body, size_t bodylen)
{
	size_t i = 0;
	while (i < bodylen) {
		size_t linestart = i;
		size_t j = i;
		while (j < bodylen && body[j] != '\n') j++;
		size_t linelen = j - linestart;
		int escape = linelen >= 5 && memcmp(body + linestart, "From ", 5) == 0;
		size_t need = linelen + (escape ? 2 : 1) + 1; /* '>'?, line, '\n' */

		if (*outlen + need > *outcap) {
			size_t ncap = *outcap ? *outcap : 4096;
			char *nb;
			while (ncap < *outlen + need) {
				if (!__util_size_mul(ncap, 2, &ncap)) { errno = ENOMEM; return -1; }
			}
			nb = realloc(*out, ncap);
			if (!nb) { errno = ENOMEM; return -1; }
			*out = nb;
			*outcap = ncap;
		}
		if (escape) (*out)[(*outlen)++] = '>';
		{
			size_t k;
			for (k = 0; k < linelen; k++)
				(*out)[*outlen + k] = body[linestart + k];
		}
		*outlen += linelen;
		(*out)[(*outlen)++] = '\n';
		i = (j < bodylen) ? j + 1 : j;
	}
	return 0;
}

/* Before appending a new message, make sure the file's current
 * end-of-file is a clean "preceded by an empty line" boundary for the
 * From_ line about to be written -- i.e. the file is empty, or its last
 * two bytes are both '\n'. Called with the lock already held (see
 * deliver_message()), so this check-then-pad is atomic with respect to
 * every other appender. */
static int ensure_blank_terminated(int fd, const char *label)
{
	struct stat st;
	char tail[2];
	ssize_t n;
	const char *pad;

	if (fstat(fd, &st) < 0) { __util_diagf("mailx: %s: %s\n", label, strerror(errno)); return -1; }
	if (st.st_size == 0) return 0;

	if (lseek(fd, st.st_size >= 2 ? st.st_size - 2 : 0, SEEK_SET) < 0) {
		__util_diagf("mailx: %s: %s\n", label, strerror(errno));
		return -1;
	}
	n = read(fd, tail, st.st_size >= 2 ? 2 : 1);
	if (n < 0) { __util_diagf("mailx: %s: %s\n", label, strerror(errno)); return -1; }

	if (st.st_size >= 2 && n == 2 && tail[0] == '\n' && tail[1] == '\n') return 0;   /* already blank-terminated */
	if (n >= 1 && tail[n - 1] == '\n') pad = "\n";                                  /* one newline short */
	else pad = "\n\n";                                                              /* line not even newline-terminated */

	return write_all(fd, pad, strlen(pad), label);
}

/* Builds and appends one real mbox message: the "From " envelope line,
 * From:/To:/Subject:/Date: headers, a blank separator, then the
 * (escaped) body -- all under one flock(LOCK_EX) held across the
 * inspect-then-write sequence, so two mailx processes delivering at
 * once are fully serialized rather than merely usually-fine. */
static int deliver_message(const char *path, const char *login, const char *to_hdr,
	const char *subject, const char *body, size_t bodylen)
{
	int fd, rc = -1;
	time_t now = time(0);
	char datebuf[64];
	char *msg = 0;
	size_t msglen = 0, msgcap = 0;
	char hdrs[2048];
	int hn;

	{
		struct tm tmv;
		if (!localtime_r(&now, &tmv) || !strftime(datebuf, sizeof datebuf, "%a %b %e %H:%M:%S %Y", &tmv))
			strcpy(datebuf, "Thu Jan  1 00:00:00 1970");
	}

	hn = snprintf(hdrs, sizeof hdrs, "From %s %s\nFrom: %s\nTo: %s\n%s%s%sDate: %s\n\n",
		login, datebuf, login, to_hdr,
		(subject && *subject) ? "Subject: " : "", (subject && *subject) ? subject : "",
		(subject && *subject) ? "\n" : "", datebuf);
	if (hn < 0 || (size_t)hn >= sizeof hdrs) {
		__util_diagf("mailx: message headers too large\n");
		return -1;
	}

	msgcap = (size_t)hn + bodylen + 64;
	msg = malloc(msgcap);
	if (!msg) { __util_diagf("mailx: out of memory\n"); return -1; }
	{
		size_t i;
		for (i = 0; i < (size_t)hn; i++) msg[i] = hdrs[i];
	}
	msglen = (size_t)hn;

	if (append_escaped_body(&msg, &msglen, &msgcap, body, bodylen) < 0) {
		__util_diagf("mailx: out of memory\n");
		free(msg);
		return -1;
	}

	/* O_RDWR, not O_WRONLY: ensure_blank_terminated() below has to
	 * read() the file's own current tail bytes to decide how much
	 * padding (if any) a well-formed boundary needs -- read() on an
	 * O_WRONLY descriptor is a plain EBADF by POSIX definition, not
	 * something O_APPEND changes. O_APPEND still guarantees every
	 * write() lands at the true end of file regardless of the
	 * current offset a read()/lseek() left behind, so combining
	 * O_RDWR with O_APPEND costs nothing here. */
	fd = open(path, O_CREAT | O_APPEND | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		__util_diagf("mailx: %s: %s\n", path, strerror(errno));
		free(msg);
		return -1;
	}
	if (flock(fd, LOCK_EX) < 0) {
		__util_diagf("mailx: %s: %s\n", path, strerror(errno));
		close(fd);
		free(msg);
		return -1;
	}

	__ownership_readable_span(msg, msglen);
	if (ensure_blank_terminated(fd, path) == 0 && write_all(fd, msg, msglen, path) == 0)
		rc = 0;

	flock(fd, LOCK_UN);
	close(fd);
	free(msg);
	return rc;
}

static int do_send(const struct passwd *me, const char *subject, char **rcpts, int nrcpt)
{
	char to_hdr[1024];
	char mbpath[1024];
	char *body;
	size_t bodylen;
	size_t pos = 0;
	int i, r;
	char subjbuf[512];

	for (i = 0; i < nrcpt; i++) {
		if (!addr_is_current_user(me, rcpts[i])) {
			__util_diagf("mailx: %s: user unknown -- local delivery only, and this "
				"machine's one real user is \"%s\" (see src/misc/pwd.c)\n",
				rcpts[i], me->pw_name);
			return 1;
		}
	}

	to_hdr[0] = 0;
	for (i = 0; i < nrcpt; i++) {
		int n = snprintf(to_hdr + pos, sizeof to_hdr - pos, "%s%s", i ? ", " : "", rcpts[i]);
		if (n < 0 || (size_t)n >= sizeof to_hdr - pos) { __util_diagf("mailx: too many/long recipients\n"); return 1; }
		pos += (size_t)n;
	}

	if (!subject) {
		if (isatty(0)) {
			fprintf(stderr, "Subject: ");
			fflush(stderr);
			if (!fgets(subjbuf, sizeof subjbuf, stdin)) subjbuf[0] = 0;
			subjbuf[strcspn(subjbuf, "\n")] = 0;
			subject = subjbuf;
		} else {
			subject = "";
		}
	}

	if (slurp_fd(STDIN_FILENO, &body, &bodylen) < 0) {
		__util_diagf("mailx: standard input: %s\n", strerror(errno));
		return 1;
	}

	if (system_mailbox_path(me, mbpath, sizeof mbpath) < 0) {
		__util_diagf("mailx: system mailbox pathname too long\n");
		free(body);
		return 1;
	}

	r = deliver_message(mbpath, me->pw_name, to_hdr, subject, body, bodylen);
	free(body);
	return r ? 1 : 0;
}

/* ==================== mbox parsing (Receive Mode) ==================== */

struct mbox_msg {
	size_t start, hdr_end, end;      /* [start,end) is the whole raw message, hdr_end is where the body begins */
	size_t env_sender_off, env_sender_len;
	size_t env_date_off, env_date_len;
	size_t from_off, from_len;       /* From: header value, or 0/0 if absent */
	size_t subj_off, subj_len;       /* Subject: header value, or 0/0 if absent */
	size_t date_off, date_len;       /* Date: header value, or 0/0 if absent */
	int deleted;
};

/* Looks for a "Name:" header (case-insensitively, per RFC 822 field-name
 * rules) anywhere in [from,to) and returns its value's span (leading
 * space after the colon skipped, trailing '\r' if any trimmed). */
static void find_header(const char *buf, size_t from, size_t to, const char *name,
	size_t *voff, size_t *vlen)
{
	size_t namelen = strlen(name);
	size_t i = from;

	*voff = 0;
	*vlen = 0;
	while (i < to) {
		size_t linestart = i;
		size_t j = i;
		while (j < to && buf[j] != '\n') j++;
		if (j - linestart > namelen && buf[linestart + namelen] == ':' &&
		    strncasecmp(buf + linestart, name, namelen) == 0) {
			size_t vs = linestart + namelen + 1;
			size_t ve = j;
			while (vs < ve && (buf[vs] == ' ' || buf[vs] == '\t')) vs++;
			while (ve > vs && (buf[ve - 1] == '\r')) ve--;
			*voff = vs;
			*vlen = ve - vs;
			return;
		}
		i = (j < to) ? j + 1 : j;
	}
}

/* mailx.html MBOX FORMAT: "each message ... begins with a line ...
 * 'From' <space> ... preceded by the beginning of the file or an empty
 * line". Scans the whole buffer for such boundaries and fills in
 * `*out`/`*nmsg` (caller frees `*out`). Returns 0 on success, -1 (with
 * a diagnostic already printed) if `buf` is non-empty but never
 * contains a single recognizable boundary at all -- refused outright
 * rather than guessed at, per this file's own header comment. */
static int parse_mbox(const char *buf, size_t len, const char *label, struct mbox_msg **out, size_t *nmsg)
{
	size_t *bounds = 0, nb = 0, cap = 0;
	size_t i = 0;
	int prev_blank = 1;
	struct mbox_msg *msgs;
	size_t k;

	while (i < len) {
		size_t linestart = i, j = i;
		while (j < len && buf[j] != '\n') j++;
		if (prev_blank && j - linestart >= 5 && memcmp(buf + linestart, "From ", 5) == 0) {
			if (nb == cap) {
				size_t ncap = cap ? cap * 2 : 64;
				size_t *nbounds = __util_reallocarray(bounds, ncap, sizeof *bounds);
				if (!nbounds) { free(bounds); errno = ENOMEM; return -1; }
				bounds = nbounds;
				cap = ncap;
			}
			bounds[nb++] = linestart;
		}
		prev_blank = (j == linestart);
		i = (j < len) ? j + 1 : j;
	}

	if (len > 0 && nb == 0) {
		__util_diagf("mailx: %s: not a valid mailbox (no \"From \" message boundary found)\n", label);
		free(bounds);
		return -1;
	}

	msgs = __util_reallocarray(0, nb ? nb : 1, sizeof *msgs);
	if (!msgs) { free(bounds); errno = ENOMEM; return -1; }

	for (k = 0; k < nb; k++) {
		struct mbox_msg *m = &msgs[k];
		size_t envline_end, body;
		memset(m, 0, sizeof *m);
		m->start = bounds[k];
		m->end = (k + 1 < nb) ? bounds[k + 1] : len;

		envline_end = m->start + 5;
		while (envline_end < m->end && buf[envline_end] != '\n') envline_end++;
		{
			size_t p = m->start + 5, sp;
			while (p < envline_end && (buf[p] == ' ' || buf[p] == '\t')) p++;
			sp = p;
			while (sp < envline_end && buf[sp] != ' ') sp++;
			m->env_sender_off = p;
			m->env_sender_len = sp - p;
			while (sp < envline_end && buf[sp] == ' ') sp++;
			m->env_date_off = sp;
			m->env_date_len = envline_end > sp ? envline_end - sp : 0;
		}

		/* Header block runs from just after the From_ line to the
		 * first empty line (or to `end` if none is found). */
		body = (envline_end < m->end) ? envline_end + 1 : envline_end;
		{
			size_t p = body;
			while (p < m->end) {
				size_t ls = p, le = p;
				while (le < m->end && buf[le] != '\n') le++;
				if (le == ls) { p = le + 1; break; }
				p = (le < m->end) ? le + 1 : le;
			}
			m->hdr_end = p;
		}

		find_header(buf, body, m->hdr_end, "From", &m->from_off, &m->from_len);
		find_header(buf, body, m->hdr_end, "Subject", &m->subj_off, &m->subj_len);
		find_header(buf, body, m->hdr_end, "Date", &m->date_off, &m->date_len);
	}

	free(bounds);
	*out = msgs;
	*nmsg = nb;
	return 0;
}

static void print_summary_line(const char *buf, const struct mbox_msg *m, size_t idx, size_t cur)
{
	char sender[64], subj[64];
	size_t l;
	const char *soff;

	l = m->from_len ? m->from_len : m->env_sender_len;
	soff = m->from_len ? buf + m->from_off : buf + m->env_sender_off;
	if (l >= sizeof sender) l = sizeof sender - 1;
	memcpy(sender, soff, l);
	sender[l] = 0;

	l = m->subj_len;
	if (l >= sizeof subj) l = sizeof subj - 1;
	memcpy(subj, buf + m->subj_off, l);
	subj[l] = 0;

	printf("%c%c%3zu  %-20s  %s\n", idx == cur ? '>' : ' ', m->deleted ? 'D' : ' ',
		idx, sender, subj[0] ? subj : "(no subject)");
}

static void print_message(const char *buf, const struct mbox_msg *m)
{
	size_t body_start = m->hdr_end;
	size_t body_len = m->end - body_start;
	size_t written;
	/* Trim exactly one trailing blank-line separator mbox format
	 * guarantees before the next message (or EOF) -- so `p` shows
	 * the message's own content, not the inter-message padding. */
	while (body_len > 0 && buf[body_start + body_len - 1] == '\n') body_len--;
	written = fwrite(buf + m->hdr_end, 1, body_len, stdout);
	(void)written; /* best-effort; stdout errors surface via ferror at exit */
	fputc('\n', stdout);
}

/* Rewrites `path` (already open on `fd`, already locked) to contain
 * only the not-deleted messages from `msgs`, in original order, by
 * concatenating their raw [start,end) byte spans verbatim -- which
 * already include each message's own trailing blank-line separator as
 * originally stored, so the result is a well-formed mbox with no
 * separator bookkeeping needed here (see this file's top-of-file
 * comment). */
static int rewrite_mailbox(int fd, const char *label, const char *buf, const struct mbox_msg *msgs, size_t n)
{
	size_t i;
	if (ftruncate(fd, 0) < 0 || lseek(fd, 0, SEEK_SET) < 0) {
		__util_diagf("mailx: %s: %s\n", label, strerror(errno));
		return -1;
	}
	for (i = 0; i < n; i++) {
		if (msgs[i].deleted) continue;
		if (write_all(fd, buf + msgs[i].start, msgs[i].end - msgs[i].start, label) < 0)
			return -1;
	}
	return 0;
}

/* True if the parsed command word cmd[0..cmdlen) is exactly `a`, or
 * (if given) exactly `b` -- every mailx command below accepts both a
 * one-letter short form and a full spelling (some, like p/print and
 * t/type, accept two of each), so this replaces a repeated
 * cmdlen/strncmp comparison per spelling with one readable call per
 * accepted form. */
static int cmd_is(const char *cmd, int cmdlen, const char *a, const char *b)
{
	if (cmdlen == (int)strlen(a) && !strncmp(cmd, a, (size_t)cmdlen)) return 1;
	return b && cmdlen == (int)strlen(b) && !strncmp(cmd, b, (size_t)cmdlen);
}

/* Interactive command loop implementing exactly the minimum mandatory
 * subset this file's header comment names: p/print, d/delete,
 * u/undelete, n/next, h/headers, q/quit, x/exit, plus '=', '#' and '?'.
 * Returns the exit status. */
static int interactive_loop(int fd, const char *label, char *buf, size_t len, struct mbox_msg *msgs, size_t n)
{
	size_t cur = n ? 1 : 0;
	char line[512];
	(void)len;

	for (;;) {
		if (!isatty(0)) { fprintf(stderr, "& "); fflush(stderr); }
		else { printf("& "); fflush(stdout); }
		if (!fgets(line, sizeof line, stdin)) {
			/* EOF at the prompt behaves like `quit`. */
			return rewrite_mailbox(fd, label, buf, msgs, n) == 0 ? 0 : 1;
		}
		line[strcspn(line, "\n")] = 0;
		{
			char *p = line;
			char *cmd, *arg;
			int cmdlen;
			size_t target;

			while (*p == ' ' || *p == '\t') p++;
			cmd = p;
			while (*p && *p != ' ' && *p != '\t') p++;
			cmdlen = (int)(p - cmd);
			while (*p == ' ' || *p == '\t') p++;
			arg = p;

			if (cmdlen == 0) {
				/* blank line: same as `next` */
				if (cur == 0 || cur > n) { printf("At end of mailbox\n"); continue; }
				print_message(buf, &msgs[cur - 1]);
				if (cur < n) cur++;
				continue;
			}
			if (cmd[0] == '#') continue;
			if (cmdlen == 1 && (cmd[0] == '=')) { printf("%zu\n", cur); continue; }
			if (cmdlen == 1 && cmd[0] == '?') {
				printf("p/print [n]   d/delete [n]   u/undelete [n]\n"
					"n/next        h/headers      q/quit\n"
					"x/exit        =              ?\n");
				continue;
			}

			target = cur;
			if (*arg) {
				if (strcmp(arg, "*") == 0) target = (size_t)-1; /* handled per-command below */
				else if (strcmp(arg, "$") == 0) target = n;
				else {
					char *endp;
					long v = strtol(arg, &endp, 10);
					if (*endp || v < 1 || (size_t)v > n) { printf("%s: no such message\n", arg); continue; }
					target = (size_t)v;
				}
			}

			if (cmd_is(cmd, cmdlen, "p", "print") || cmd_is(cmd, cmdlen, "t", "type")) {
				if (!n) { printf("No messages\n"); continue; }
				if (target == (size_t)-1) { size_t ii; for (ii = 1; ii <= n; ii++) print_message(buf, &msgs[ii - 1]); continue; }
				print_message(buf, &msgs[target - 1]);
				cur = target;
			} else if (cmd_is(cmd, cmdlen, "n", "next")) {
				if (cur == 0 || cur >= n) { printf("At end of mailbox\n"); continue; }
				cur++;
				print_message(buf, &msgs[cur - 1]);
			} else if (cmd_is(cmd, cmdlen, "d", "delete")) {
				if (!n) { printf("No messages\n"); continue; }
				if (target == (size_t)-1) { size_t ii; for (ii = 0; ii < n; ii++) msgs[ii].deleted = 1; }
				else msgs[target - 1].deleted = 1;
			} else if (cmd_is(cmd, cmdlen, "u", "undelete")) {
				if (!n) { printf("No messages\n"); continue; }
				if (target == (size_t)-1) { size_t ii; for (ii = 0; ii < n; ii++) msgs[ii].deleted = 0; }
				else msgs[target - 1].deleted = 0;
			} else if (cmd_is(cmd, cmdlen, "h", "headers")) {
				size_t ii;
				for (ii = 1; ii <= n; ii++) print_summary_line(buf, &msgs[ii - 1], ii, cur);
			} else if (cmd_is(cmd, cmdlen, "q", "quit")) {
				return rewrite_mailbox(fd, label, buf, msgs, n) == 0 ? 0 : 1;
			} else if (cmd_is(cmd, cmdlen, "x", 0) || cmd_is(cmd, cmdlen, "ex", "exit")) {
				return 0;
			} else if (cmdlen > 0 && isdigit((unsigned char)cmd[0])) {
				char *endp;
				long v = strtol(cmd, &endp, 10);
				if (*endp || v < 1 || (size_t)v > n) { printf("%.*s: no such message\n", cmdlen, cmd); continue; }
				print_message(buf, &msgs[v - 1]);
				cur = (size_t)v;
			} else {
				printf("%.*s: unknown command -- try ?\n", cmdlen, cmd);
			}
		}
	}
}

static int do_receive(const struct passwd *me, const char *path, int headers_only, int no_summary)
{
	int fd, want_write;
	char *buf;
	size_t len;
	struct mbox_msg *msgs = 0;
	size_t n = 0;
	int rc;

	want_write = 1;
	fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0 && errno == EACCES) { fd = open(path, O_RDONLY); want_write = 0; }
	if (fd < 0) {
		if (errno == ENOENT) { printf("No mail for %s\n", me->pw_name); return 0; }
		__util_diagf("mailx: %s: %s\n", path, strerror(errno));
		return 1;
	}
	if (flock(fd, want_write ? LOCK_EX : LOCK_SH) < 0) {
		__util_diagf("mailx: %s: %s\n", path, strerror(errno));
		close(fd);
		return 1;
	}

	if (slurp_fd(fd, &buf, &len) < 0) {
		__util_diagf("mailx: %s: %s\n", path, strerror(errno));
		flock(fd, LOCK_UN);
		close(fd);
		return 1;
	}

	if (parse_mbox(buf, len, path, &msgs, &n) < 0) {
		free(buf);
		flock(fd, LOCK_UN);
		close(fd);
		return 1;
	}

	if (n == 0) {
		printf("No mail for %s\n", me->pw_name);
		free(buf);
		free(msgs);
		flock(fd, LOCK_UN);
		close(fd);
		return 0;
	}

	if (!no_summary) {
		size_t i;
		printf("Mailbox %s: %zu message%s\n", path, n, n == 1 ? "" : "s");
		for (i = 1; i <= n; i++) print_summary_line(buf, &msgs[i - 1], i, 1);
	}

	if (headers_only || !want_write) {
		rc = 0;
	} else {
		rc = interactive_loop(fd, path, buf, len, msgs, n);
	}

	free(buf);
	free(msgs);
	flock(fd, LOCK_UN);
	close(fd);
	return rc;
}

static int do_test_for_mail(const struct passwd *me)
{
	char path[1024];
	int fd;
	char c;
	ssize_t n;

	if (system_mailbox_path(me, path, sizeof path) < 0) return 1;
	fd = open(path, O_RDONLY);
	if (fd < 0) return 1;
	n = read(fd, &c, 1);
	close(fd);
	return (n == 1 && c == 'F') ? 0 : 1;
}

/* ==================== option parsing / dispatch ==================== */

int __util_mailx_main(int argc, char **argv)
{
	int i;
	int opt_e = 0, opt_f = 0, opt_H = 0, opt_i = 0, opt_N = 0;
	const char *opt_s = 0, *opt_u = 0;
	struct passwd *me;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		size_t j;

		if (!strcmp(a, "--")) { i++; break; }
		if (a[0] != '-' || a[1] == 0) break;

		if (!strcmp(a, "-s")) {
			if (++i >= argc) { __util_diagf("mailx: -s: option requires an argument\n"); return 1; }
			opt_s = argv[i];
			continue;
		}
		if (!strcmp(a, "-u")) {
			if (++i >= argc) { __util_diagf("mailx: -u: option requires an argument\n"); return 1; }
			opt_u = argv[i];
			continue;
		}
		for (j = 1; a[j]; j++) {
			switch (a[j]) {
			case 'e': opt_e = 1; break;
			case 'f': opt_f = 1; break;
			case 'H': opt_H = 1; break;
			case 'i': opt_i = 1; break;
			case 'N': opt_N = 1; break;
			case 'n': break; /* documented no-op -- see this file's header comment */
			case 'F':
				__util_diagf("mailx: -F: not implemented -- see src/util/mailx.c\n");
				return 1;
			default:
				__util_diagf("mailx: unknown option -%c\n", a[j]);
				return 1;
			}
		}
	}

	me = current_user();
	if (!me) return 1;

	if (opt_i) signal(SIGINT, SIG_IGN);

	if (opt_u && !addr_is_current_user(me, opt_u)) {
		__util_diagf("mailx: -u %s: no such local user -- this machine's one real user is \"%s\"\n",
			opt_u, me->pw_name);
		return 1;
	}

	if (opt_e) return do_test_for_mail(me);

	if (!opt_f && i < argc) {
		/* Send Mode: one or more addresses follow. */
		return do_send(me, opt_s, argv + i, argc - i);
	}

	{
		char path[1024];
		if (opt_f) {
			if (i < argc) {
				if (strlen(argv[i]) >= sizeof path) { __util_diagf("mailx: pathname too long\n"); return 1; }
				strcpy(path, argv[i]);
			} else if (secondary_mailbox_path(me, path, sizeof path) < 0) {
				__util_diagf("mailx: mbox pathname too long\n");
				return 1;
			}
		} else if (system_mailbox_path(me, path, sizeof path) < 0) {
			__util_diagf("mailx: system mailbox pathname too long\n");
			return 1;
		}
		return do_receive(me, path, opt_H, opt_N);
	}
}
