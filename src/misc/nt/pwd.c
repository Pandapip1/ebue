/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT's <pwd.h> backend. Pure relocation, zero behavior change: this
 * file used to be the platform-SHARED src/misc/pwd.c, silently also
 * governing native Linux's getpwnam()/getpwuid()/etc even though
 * Linux genuinely has a queryable /etc/passwd this single-user
 * synthesis never consulted. src/misc/linux/pwd.c (the NSS pass, see
 * its own header) is Linux's real, file-backed sibling now; this file
 * is unchanged apart from moving into src/misc/nt/, following this
 * tree's existing linux/+nt/ split convention (e.g. src/dlfcn/
 * {linux,nt}/plat_dlfcn.c).
 *
 * <pwd.h>: NT has no /etc/passwd, but this library has exactly one
 * current uid.  getuid()/geteuid() (src/unistd/ids.c) map the process
 * token's SAM/AD SID and RID, and setuid()/seteuid() can only retain it,
 * so "the current user" is the only user this library can ever be asked
 * about honestly -- and it is
 * genuinely knowable:
 *
 *   pw_name  -- %USERNAME%, falling back to %USER%, exactly the
 *               lookup getlogin() (src/unistd/ids.c) already uses, so
 *               the two agree by construction.
 *   pw_uid / pw_gid -- getuid()/getgid(), not a separate constant, so
 *               there is no way for this file to drift out of sync
 *               with src/unistd/ids.c.
 *   pw_dir   -- %USERPROFILE%, NT's real analogue of a home directory
 *               (it is what Explorer, cmd.exe's "cd %USERPROFILE%",
 *               and every other NT program that wants "the user's
 *               directory" already use).
 *   pw_shell -- %ComSpec%, the same value src/stdlib/system.c's
 *               find_shell() and src/stdio/misc.c treat as "the
 *               shell" on this OS, for the same reason (see those
 *               files' header comments).  Unlike system()'s
 *               find_shell(), this does not re-validate it with
 *               access(X_OK): struct passwd's pw_shell is documented
 *               metadata, not a promise the shell is currently
 *               runnable, and POSIX does not require that check
 *               either (getpwnam.html has no such ERRORS clause).
 *
 * pw_uid comes from the token SID.  Turning that SID into a display name
 * would separately require LsaLookupSids or LookupAccountSid, so pw_name
 * retains the environment route already used by getlogin().
 *
 * Any *other* name or uid is refused cleanly (NULL / *result = NULL,
 * errno untouched, per getpwnam.html/getpwuid.html's "requested entry
 * was not found" case) rather than answered with a fabricated record
 * -- there is no database to enumerate a second entry out of.
 *
 * getpwent()/setpwent()/endpwent() (XSI, getpwent.html): implemented,
 * not stubbed out.  Unlike setrlimit() (include/sys/resource.h,
 * undefined-ok) these do not need to misrepresent anything: ntlibc's
 * user database genuinely contains exactly one entry, so "rewind,
 * then yield that one entry, then EOF" is the honest enumeration of
 * it, not a fabrication of a multi-user system that is not there.
 *
 * In the practically-never case that neither %USERNAME% nor %USER% is
 * set, pw_name is unknowable and every one of these functions reports
 * "not found" (or, for getpwent(), end-of-file) rather than invent a
 * name -- consistent with getlogin() also returning NULL in that
 * case.
 */
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ownership_stubs.h"

/* Non-reentrant getpwnam()/getpwuid()/getpwent() share this static
 * storage; none of these functions are required to be thread-safe
 * (getpwnam.html DESCRIPTION: "The getpwnam() function need not be
 * thread-safe."). */
/* Grown on demand rather than fixed, because POSIX gives the non-_r
 * forms NO WAY to report that this buffer was too small.  getpwnam.html
 * lists [ERANGE] only for getpwnam_r()/getpwuid_r(), where it means
 * "insufficient storage was supplied via buffer and bufsize" -- an
 * argument the non-_r forms do not have.  The standard is therefore
 * assuming the implementation makes its own storage adequate, and the
 * only conforming answers left if it does not are "not found" (a lie
 * about a user who exists) or an errno the ERRORS list forbids.  Sizing
 * it correctly avoids the choice.  See fill_shared(). */
static struct passwd g_pw;
static char *g_pwbuf;
static size_t g_pwbufsz;

/* current_name(): the same %USERNAME%-then-%USER% lookup getlogin()
 * (src/unistd/ids.c) uses, so getpwnam(getlogin()) and
 * getpwuid(getuid())->pw_name round-trip through the identical
 * source.  Returns NULL if genuinely unknowable. */
static const char *current_name(void)
{
	const char *n = getenv("USERNAME");
	if (!n || !*n) n = getenv("USER");
	if (n && !*n) n = 0;
	return n;
}

/* fill_current(): pack the current user's record into buf (bufsz
 * bytes).  Returns 1 on success, 0 if the user's name is unknowable
 * (treated as "not found" by every caller), or ERANGE if buf is too
 * small -- in which case *needp is set to the size that would do.  Never touches the global errno -- callers decide, per
 * function, whether that return travels through errno (getpwnam(),
 * getpwuid()) or is returned directly (the _r variants). pw is
 * required: `pw->pw_name = buf;` and friends are unconditional once
 * name is known and bufsz suffices, with no NULL check of pw itself
 * anywhere in this function, and every real caller (fill_shared()'s
 * &g_pw, getpwnam_r()/getpwuid_r()'s own forwarded pwd, itself now
 * required at those two functions' own contract) passes a real
 * struct. buf is deliberately NOT marked -- `if (need > bufsz)
 * return ERANGE;` guards every dereference of it, and a caller
 * genuinely may pass NULL together with bufsz == 0 (getpwnam_r()/
 * getpwuid_r()'s own contract), which this correctly answers ERANGE
 * for rather than crashing on. needp is left unmarked too, guarded by
 * its own `if (needp)`. */
static int fill_current(struct passwd *pw, char *buf, size_t bufsz, size_t *needp)
    __attribute__((nonnull(1)));
static int fill_current(struct passwd *pw, char *buf, size_t bufsz, size_t *needp)
{
	const char *name = current_name();
	const char *dir, *shell;
	size_t nl, dl, sl, need;

	if (!name) return 0;

	dir = getenv("USERPROFILE");
	if (!dir || !*dir) dir = "/";
	shell = getenv("ComSpec");
	if (!shell || !*shell) shell = "cmd.exe";

	nl = strlen(name) + 1;
	dl = strlen(dir) + 1;
	sl = strlen(shell) + 1;
	need = nl + dl + sl;
	if (need > bufsz) { if (needp) *needp = need; return ERANGE; }

	__ownership_writable_span(buf, nl);
	memcpy(buf, name, nl);
	pw->pw_name = buf;
	buf += nl;
	__ownership_writable_span(buf, dl);
	memcpy(buf, dir, dl);
	pw->pw_dir = buf;
	buf += dl;
	__ownership_writable_span(buf, sl);
	memcpy(buf, shell, sl);
	pw->pw_shell = buf;

	pw->pw_uid = getuid();
	pw->pw_gid = getgid();
	return 1;
}

/* fill_current() into the shared buffer, growing it if it does not fit.
 *
 * Returns 1 on success, 0 for "not found".  It never reports ERANGE,
 * which is the whole point: getpwnam()/getpwuid() used to do
 *
 *     r = fill_current(&g_pw, g_pwbuf, sizeof g_pwbuf);
 *     if (r == ERANGE) { errno = ERANGE; return 0; }
 *
 * against a fixed 8448-byte buffer, setting an errno that is not in
 * their ERRORS list -- reachable by any program that sets a long enough
 * %USERNAME%, %USERPROFILE% or %ComSpec%, which needs no unusual NT
 * configuration at all.
 *
 * An allocation failure is reported as "not found" with errno left
 * alone.  That is not a happy answer, but [ENOMEM] is not in these
 * functions' ERRORS list either, and RETURN VALUE requires errno to be
 * untouched when the entry was not found -- so of the answers available
 * it is the only conforming one.
 *
 * The returned strings point into this buffer and a later call may
 * reallocate it; getpwnam.html permits exactly that ("the return value
 * may point to a static area which is overwritten by a subsequent call
 * to getpwent(), getpwnam(), or getpwuid()"). */
static int fill_shared(void)
{
	size_t need = 0;
	int r = fill_current(&g_pw, g_pwbuf, g_pwbufsz, &need);
	char *nb;

	if (r != ERANGE) return r;
	nb = realloc(g_pwbuf, need);
	if (!nb) return 0;
	g_pwbuf = nb;
	g_pwbufsz = need;
	r = fill_current(&g_pw, g_pwbuf, g_pwbufsz, &need);
	return r == ERANGE ? 0 : r;
}

/* getpwnam.html RETURN VALUE: "If the requested entry was not found,
 * errno shall not be changed."  These functions now never set errno at
 * all: the only errno they ever set was the [ERANGE] that is not in
 * their ERRORS list, and fill_shared() removes that case. */
struct passwd *getpwnam(const char *name)
{
	const char *cur = current_name();
	int r;

	if (!name || !cur || strcmp(name, cur) != 0) return 0;
	r = fill_shared();
	if (r == 0) return 0;
	return &g_pw;
}

struct passwd *getpwuid(uid_t uid)
{
	int r;

	if (uid != getuid()) return 0;
	r = fill_shared();
	if (r == 0) return 0;
	return &g_pw;
}

/* getpwnam_r()/getpwuid_r() (Thread-Safe Functions option; ntlibc has
 * no feature-test gate for it, same as the rest of this library's
 * _r functions).  Return value is the error number itself (0 on
 * success or clean not-found), never routed through errno; *result
 * is set to NULL in both the error and not-found cases. */
int getpwnam_r(const char *name, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **result)
{
	const char *cur;
	int r;

	*result = 0;
	if (!name) return 0;
	cur = current_name();
	if (!cur || strcmp(name, cur) != 0) return 0;
	r = fill_current(pwd, buffer, bufsize, 0);
	if (r == ERANGE) return ERANGE;
	if (r == 0) return 0;
	*result = pwd;
	return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **result)
{
	int r;

	*result = 0;
	if (uid != getuid()) return 0;
	r = fill_current(pwd, buffer, bufsize, 0);
	if (r == ERANGE) return ERANGE;
	if (r == 0) return 0;
	*result = pwd;
	return 0;
}

/* getpwent()/setpwent()/endpwent(): the one-entry "database" this
 * library actually has.  g_pwent_done tracks whether the single entry
 * has already been handed out this pass; setpwent() rewinds it,
 * endpwent() "closes" it the same way (there is nothing else to
 * release). */
static int g_pwent_done;

void setpwent(void)
{
	g_pwent_done = 0;
}

void endpwent(void)
{
	g_pwent_done = 0;
}

/* getpwent.html RETURN VALUE: "On end-of-file, getpwent() shall
 * return a null pointer and shall not change the setting of errno."
 * That is exactly what happens once the one entry has been given out,
 * and also (indistinguishably, but honestly -- there genuinely is
 * nothing further to enumerate) if the current user's name could not
 * be determined at all. */
struct passwd *getpwent(void)
{
	if (g_pwent_done) return 0;
	g_pwent_done = 1;
	return getpwuid(getuid());
}
