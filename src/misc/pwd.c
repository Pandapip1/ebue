/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <pwd.h>: NT has no /etc/passwd, but this library has exactly one
 * uid.  getuid()/geteuid() (src/unistd/ids.c) always report 1000 and
 * setuid()/seteuid() are no-ops, so "the current user" is the only
 * user this library can ever be asked about honestly -- and it is
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
 * A SID-based route was considered and rejected: NtOpenProcessToken /
 * NtQueryInformationToken(TokenUser) would hand back a SID, but
 * turning a SID into a display name needs LSA (LsaLookupSids) or
 * advapi32's LookupAccountSid -- neither of which this from-scratch,
 * ntdll-only library links (tools/ntdll.def has no Token/Lsa/Sid
 * exports) -- and it would not change what gets reported anyway:
 * pw_uid is getuid(), a fixed sentinel unrelated to any real Windows
 * SID, not a decoded one. The environment route is simpler, already
 * proven by getlogin(), and produces the identical answer.
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

/* Non-reentrant getpwnam()/getpwuid()/getpwent() share this static
 * storage; none of these functions are required to be thread-safe
 * (getpwnam.html DESCRIPTION: "The getpwnam() function need not be
 * thread-safe."). */
static struct passwd g_pw;
static char g_pwbuf[256 + 2 * 4096];   /* name + dir + shell, PATH_MAX each */

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
 * small.  Never touches the global errno -- callers decide, per
 * function, whether that return travels through errno (getpwnam(),
 * getpwuid()) or is returned directly (the _r variants). */
static int fill_current(struct passwd *pw, char *buf, size_t bufsz)
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
	if (need > bufsz) return ERANGE;

	memcpy(buf, name, nl);
	pw->pw_name = buf;
	buf += nl;
	memcpy(buf, dir, dl);
	pw->pw_dir = buf;
	buf += dl;
	memcpy(buf, shell, sl);
	pw->pw_shell = buf;

	pw->pw_uid = getuid();
	pw->pw_gid = getgid();
	return 1;
}

/* getpwnam.html RETURN VALUE: "If the requested entry was not found,
 * errno shall not be changed." errno is only ever set here on the
 * ERANGE-equivalent case, which cannot happen against g_pwbuf's fixed
 * size unless %USERNAME%/%USERPROFILE%/%ComSpec% together exceed it
 * (each individually capped well under PATH_MAX by NT itself). */
struct passwd *getpwnam(const char *name)
{
	const char *cur = current_name();
	int r;

	if (!name || !cur || strcmp(name, cur) != 0) return 0;
	r = fill_current(&g_pw, g_pwbuf, sizeof g_pwbuf);
	if (r == ERANGE) { errno = ERANGE; return 0; }
	if (r == 0) return 0;
	return &g_pw;
}

struct passwd *getpwuid(uid_t uid)
{
	int r;

	if (uid != getuid()) return 0;
	r = fill_current(&g_pw, g_pwbuf, sizeof g_pwbuf);
	if (r == ERANGE) { errno = ERANGE; return 0; }
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
	r = fill_current(pwd, buffer, bufsize);
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
	r = fill_current(pwd, buffer, bufsize);
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
