/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* NT has one immutable process identity, represented by the user SID in
 * its primary access token.  SAM and Active Directory account SIDs have
 * the shape S-1-5-21-X-Y-Z-RID.  The final subauthority is the account's
 * RID; the preceding subauthorities identify the issuing machine/domain.
 *
 * A RID alone is not a uid: a domain-joined machine can run a local SAM
 * account and an AD account with the same RID.  Use Cygwin's computational
 * mapping so the two namespaces do not alias:
 *
 *     local account domain       0x30000 + RID
 *     machine's primary domain  0x100000 + RID
 *     other/trusted domain      0xfe500000 + RID when its AD
 *                                trustPosixOffset is unavailable
 *
 * The NTLIBC_USE_KERNEL32 build asks LSA for the local and primary domain
 * SIDs and compares the complete domain part.  The default ntdll-only
 * build cannot call LsaQueryInformationPolicy (advapi32), so it uses the
 * system-provided USERDOMAIN/COMPUTERNAME pair: equal means SAM, unequal
 * means the logged-on domain.  If those values are unavailable it chooses
 * SAM, the only account database present on a non-domain machine.
 *
 * Well-known SIDs use Cygwin's documented fixed mappings.  A failure to
 * open/query the token falls back to the old 1000: POSIX reserves no error
 * return from getuid(), so failure must still produce an ordinary uid_t and
 * must not touch errno.
 *
 * Process groups and sessions are not that, and used to be: every one
 * of getpgrp/getpgid/setpgrp/setsid/getsid answered a hardcoded 1.  The
 * fiction cost more than it saved.  setsid.html's DESCRIPTION and its
 * "[EPERM] The calling process is already a process group leader"
 * together describe a state machine -- the first call sets the process
 * group ID to the process ID, which is exactly the precondition that
 * makes the second one fail -- and a constant can neither enter that
 * state nor report it.  setpgrp.html is starker still: "No errors are
 * defined", so its one specified effect ("sets the process group ID of
 * the calling process to the process ID of the calling process") is the
 * entire call, and it did not happen.
 *
 * So the two ids live in statics below.  NT has no process-group or
 * session object to hang them on -- a console process group
 * (CREATE_NEW_PROCESS_GROUP) is the nearest thing and cannot be joined;
 * src/process/posix_spawn.c's banner has that argument in full -- so
 * this is per-process bookkeeping and nothing more.  It is also exactly
 * as much of the model as a process can observe about *itself*, which
 * is all these six calls specify once there is no second process to
 * name (the clauses that do need one stay N/A; test/posix-unistd-ids.c
 * records which).
 *
 * The initial value, 1, is load-bearing twice over.  POSIX starts a
 * process in the group and session of whoever started it, so it is not
 * a group leader and its first setsid() succeeds; leadership is asked
 * here as `pgid == getpid()` rather than kept as a flag, and 1 is a
 * number no process can answer getpid() with -- NT process ids are
 * multiples of four (0 is the idle process, 4 is System) -- so that
 * comparison is false at startup for every process by construction
 * rather than by luck.  Reading 1 back as one *shared* group would be
 * the wrong reading: nothing here can name another process's group --
 * getpgid()/getsid() validate their pid (pid_exists() below answers the
 * [ESRCH] both pages make a shall-fail) but have no per-pid group to
 * report once it exists, so what comes back is always this process's
 * own.  1 therefore means "the group this process was born into, whose
 * other members this library cannot see".
 *
 * fork() needs no fixup for any of this and gets it right for free: the
 * clone carries these statics over with the rest of the address space
 * (src/process/fork.c), which is fork.html's "the child process shall
 * inherit the process group ID and session membership of the parent"
 * -- and because leadership is a comparison against getpid() rather
 * than a stored flag, a child of a leader correctly stops being one the
 * moment its own pid differs.  __spawn (src/process/spawn.c) cannot do
 * the same: the child is a fresh image with fresh statics, so it starts
 * in the born-into group rather than the caller's.  That divergence
 * from exec.html's inheritance rule is left standing deliberately --
 * closing it means widening the RuntimeData block crt1 reads back, for
 * a value no call in this library can report about another process. */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "libc.h"

#define UID_FALLBACK               ((uid_t)1000)
#define SAM_POSIX_OFFSET           ((uid_t)0x00030000)
#define PRIMARY_POSIX_OFFSET       ((uid_t)0x00100000)
#define NOACCESS_POSIX_OFFSET      ((uid_t)0xfe500000)

enum domain_kind {
	DOMAIN_UNKNOWN,
	DOMAIN_SAM,
	DOMAIN_PRIMARY,
	DOMAIN_TRUSTED
};

static int sid_valid(const SID *sid)
{
	return sid && sid->Revision == SID_REVISION &&
	       sid->SubAuthorityCount > 0 &&
	       sid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES;
}

static int sid_authority(const SID *sid)
{
	int i;
	for (i = 0; i < 5; i++)
		if (sid->IdentifierAuthority.Value[i]) return -1;
	return sid->IdentifierAuthority.Value[5];
}

/* domain is a SID with the account RID removed. */
static int sid_in_domain(const SID *sid, const SID *domain)
{
	size_t n;

	if (!sid_valid(sid) || !sid_valid(domain) ||
	    sid->SubAuthorityCount != domain->SubAuthorityCount + 1)
		return 0;
	if (sid->Revision != domain->Revision ||
	    memcmp(&sid->IdentifierAuthority, &domain->IdentifierAuthority,
	           sizeof sid->IdentifierAuthority) != 0)
		return 0;
	n = (size_t)domain->SubAuthorityCount * sizeof(ULONG);
	return memcmp(sid->SubAuthority, domain->SubAuthority, n) == 0;
}

#ifdef NTLIBC_USE_KERNEL32
/* advapi32 is reached dynamically and only in the explicitly enabled
 * higher-level-DLL build, as required by CONTRIBUTING.md. */
typedef NTSTATUS (NTAPI *lsa_open_policy_fn)(UNICODE_STRING *,
    OBJECT_ATTRIBUTES *, ACCESS_MASK, HANDLE *);
typedef NTSTATUS (NTAPI *lsa_query_policy_fn)(HANDLE, ULONG, PVOID *);
typedef NTSTATUS (NTAPI *lsa_free_memory_fn)(PVOID);
typedef NTSTATUS (NTAPI *lsa_close_fn)(HANDLE);

typedef struct {
	UNICODE_STRING DomainName;
	SID *DomainSid;
} POLICY_ACCOUNT_DOMAIN_INFO;

typedef struct {
	UNICODE_STRING Name;
	SID *Sid;
} POLICY_PRIMARY_DOMAIN_INFO;

static int advapi_proc(PVOID dll, const char *name, PVOID *proc)
{
	STRING s;
	size_t n = strlen(name);
	if (n > 0xffffu) return 0;
	s.Length = s.MaximumLength = (USHORT)n;
	s.Buffer = (char *)name;
	return NT_SUCCESS(LdrGetProcedureAddress(dll, &s, 0, proc));
}

static enum domain_kind lsa_domain_kind(const SID *sid)
{
	static WCHAR dllname_buf[] = {
		'a','d','v','a','p','i','3','2','.','d','l','l',0
	};
	UNICODE_STRING dllname;
	PVOID dll = 0, p;
	lsa_open_policy_fn open_policy;
	lsa_query_policy_fn query_policy;
	lsa_free_memory_fn free_memory;
	lsa_close_fn close_policy;
	OBJECT_ATTRIBUTES oa;
	POLICY_ACCOUNT_DOMAIN_INFO *account = 0;
	POLICY_PRIMARY_DOMAIN_INFO *primary = 0;
	HANDLE policy = 0;
	enum domain_kind kind = DOMAIN_UNKNOWN;

	RtlInitUnicodeString(&dllname, dllname_buf);
	if (!NT_SUCCESS(LdrLoadDll(0, 0, &dllname, &dll))) return kind;
	if (!advapi_proc(dll, "LsaOpenPolicy", &p)) goto out;
	open_policy = (lsa_open_policy_fn)p;
	if (!advapi_proc(dll, "LsaQueryInformationPolicy", &p)) goto out;
	query_policy = (lsa_query_policy_fn)p;
	if (!advapi_proc(dll, "LsaFreeMemory", &p)) goto out;
	free_memory = (lsa_free_memory_fn)p;
	if (!advapi_proc(dll, "LsaClose", &p)) goto out;
	close_policy = (lsa_close_fn)p;

	memset(&oa, 0, sizeof oa);
	if (!NT_SUCCESS(open_policy(0, &oa, 0x00000001, &policy))) goto out;
	/* POLICY_INFORMATION_CLASS: PrimaryDomain=3, AccountDomain=5. */
	if (!NT_SUCCESS(query_policy(policy, 3, (PVOID *)&primary))) goto close;
	if (!NT_SUCCESS(query_policy(policy, 5, (PVOID *)&account))) goto close;

	/* On a domain controller both policy entries name the AD domain;
	 * primary wins, matching Cygwin's treatment of that case. */
	if (primary->Sid && sid_in_domain(sid, primary->Sid))
		kind = DOMAIN_PRIMARY;
	else if (account->DomainSid && sid_in_domain(sid, account->DomainSid))
		kind = DOMAIN_SAM;
	else
		kind = DOMAIN_TRUSTED;

close:
	if (account) free_memory(account);
	if (primary) free_memory(primary);
	close_policy(policy);
out:
	LdrUnloadDll(dll);
	return kind;
}
#endif

static int ascii_case_equal(const char *a, const char *b)
{
	unsigned char x, y;
	if (!a || !b || !*a || !*b) return 0;
	do {
		x = (unsigned char)*a++;
		y = (unsigned char)*b++;
		if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
		if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
		if (x != y) return 0;
	} while (x);
	return 1;
}

static enum domain_kind current_domain_kind(const SID *sid)
{
	const char *user_domain, *computer;
#ifdef NTLIBC_USE_KERNEL32
	enum domain_kind kind = lsa_domain_kind(sid);
	if (kind != DOMAIN_UNKNOWN) return kind;
#else
	(void)sid;
#endif
	user_domain = getenv("USERDOMAIN");
	computer = getenv("COMPUTERNAME");
	if (user_domain && *user_domain && computer && *computer)
		return ascii_case_equal(user_domain, computer) ?
		       DOMAIN_SAM : DOMAIN_PRIMARY;
	return DOMAIN_SAM;
}

static uid_t sid_uid(const SID *sid)
{
	ULONG rid;
	int authority;
	uid_t uid;

	if (!sid_valid(sid)) return UID_FALLBACK;
	rid = sid->SubAuthority[sid->SubAuthorityCount - 1];
	authority = sid_authority(sid);

	/* SAM, AD and trusted-domain accounts: S-1-5-21-X-Y-Z-RID. */
	if (authority == 5 && sid->SubAuthorityCount == 5 &&
	    sid->SubAuthority[0] == 21) {
		switch (current_domain_kind(sid)) {
		case DOMAIN_PRIMARY: uid = PRIMARY_POSIX_OFFSET + rid; break;
		case DOMAIN_TRUSTED: uid = NOACCESS_POSIX_OFFSET + rid; break;
		default:             uid = SAM_POSIX_OFFSET + rid; break;
		}
		return uid == (uid_t)-1 ? UID_FALLBACK : uid;
	}

	/* Cygwin's fixed mappings for well-known principals. */
	if (authority == 5 &&
	    (sid->SubAuthorityCount == 1 || sid->SubAuthority[0] == 32))
		uid = rid;
	else if (authority == 5 && sid->SubAuthorityCount > 1)
		uid = (uid_t)(0x1000u * sid->SubAuthority[0] + (rid & 0xffffu));
	else if (authority == 16)
		uid = (uid_t)(0x60000u + rid);
	else if (authority >= 0)
		uid = (uid_t)(0x10000u + 0x100u * (unsigned)authority +
		              (rid & 0xffu));
	else
		return UID_FALLBACK;
	return uid == (uid_t)-1 ? UID_FALLBACK : uid;
}

static uid_t detect_uid(void)
{
	union {
		ULONG_PTR align;
		UCHAR bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
	} buf;
	TOKEN_USER *user = (TOKEN_USER *)buf.bytes;
	SID *sid;
	HANDLE token;
	ULONG got = 0;
	NTSTATUS st;
	uintptr_t start, end, sp;
	size_t sidlen;

	st = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token);
	if (!NT_SUCCESS(st)) return UID_FALLBACK;
	st = NtQueryInformationToken(token, TokenUser, buf.bytes, sizeof buf.bytes,
	                             &got);
	NtClose(token);
	if (!NT_SUCCESS(st) || got > sizeof buf.bytes || got < sizeof(TOKEN_USER))
		return UID_FALLBACK;

	sid = user->User.Sid;
	start = (uintptr_t)buf.bytes;
	end = start + got;
	sp = (uintptr_t)sid;
	if (!sid || sp < start || sp > end || end - sp < 8) return UID_FALLBACK;
	if (!sid_valid(sid)) return UID_FALLBACK;
	sidlen = 8 + (size_t)sid->SubAuthorityCount * sizeof(ULONG);
	if (sidlen > end - sp) return UID_FALLBACK;
	return sid_uid(sid);
}

static uid_t cached_uid = (uid_t)-1;

uid_t getuid(void)
{
	/* The primary token is immutable through this API.  Caching also keeps
	 * the ntdll-only environment fallback stable if callers change env. */
	uid_t uid = cached_uid;
	if (uid == (uid_t)-1) cached_uid = uid = detect_uid();
	return uid;
}
uid_t geteuid(void) { return getuid(); }
gid_t getgid(void) { return 1000; }
gid_t getegid(void) { return 1000; }

/* Which ids exist at all, as opposed to which ids may be assumed.
 *
 * setuid.html ERRORS separates two shall-fail answers, and a stub that
 * returns 0 gives neither:
 *   "[EINVAL] The value of the uid argument is invalid and not supported
 *    by the implementation."
 *   "[EPERM] The process does not have appropriate privileges and uid
 *    does not match the real user ID or the saved set-user-ID."
 * seteuid.html, setgid.html and setegid.html carry the identical pair
 * for their own id, and setreuid.html/setregid.html the equivalent
 * ("[EINVAL] The value of the ruid or euid argument is invalid or
 * out-of-range").
 *
 * Answering 0 to setuid(0) is the dangerous one.  Every
 * privilege-dropping idiom in Unix software is
 *     if (setuid(pw->pw_uid) != 0) abort();
 * and a call that reports success without dropping anything turns
 * "refuse to run privileged" into "run believing the drop happened".
 * sysconf(_SC_SAVED_IDS) is -1 here (src/unistd/sysconf.c), so there is
 * no saved set-user-ID for a request to match either: the [EPERM]
 * precondition holds for every id but the current one.
 *
 * The [EINVAL]/[EPERM] line is the one POSIX itself supplies: (uid_t)-1
 * is setreuid()'s "leave this one alone" marker, not an assumable uid.
 * Every other uid_t is supported because Cygwin's trusted-domain offsets
 * legitimately occupy the upper half of the 32-bit space.  Reserving
 * that half, as the old fixed-1000 model did, would reject a real current
 * uid produced by the mapping above.
 *
 * Note the asymmetry with getgroups() below: this is a *choice about the
 * id space*, documented here so it is not mistaken for a platform fact.
 */
static int id_supported(uid_t id)
{
	return id != (uid_t)-1;
}

/* The shared body of all six: reject what is not an id, refuse what is
 * an id but not ours, and grant the request that is already true. */
static int set_one_id(uid_t id, uid_t self)
{
	if (!id_supported(id)) { errno = EINVAL; return -1; }
	if (id != self) { errno = EPERM; return -1; }
	return 0;
}

/* setreuid.html DESCRIPTION: "If ruid or euid is -1, the corresponding
 * effective or real user ID of the current process shall be left
 * unchanged" -- so (uid_t)-1 is the one value in the reserved half that
 * these two must accept, and it asks for nothing. */
static int set_two_ids(uid_t r, uid_t e, uid_t self)
{
	if (r != (uid_t)-1 && set_one_id(r, self) < 0) return -1;
	if (e != (uid_t)-1 && set_one_id(e, self) < 0) return -1;
	return 0;
}

int setuid(uid_t u) { return set_one_id(u, getuid()); }
int seteuid(uid_t u) { return set_one_id(u, geteuid()); }
int setgid(gid_t g) { return set_one_id((uid_t)g, (uid_t)getgid()); }
int setegid(gid_t g) { return set_one_id((uid_t)g, (uid_t)getegid()); }
int setreuid(uid_t r, uid_t e) { return set_two_ids(r, e, getuid()); }
int setregid(gid_t r, gid_t e) { return set_two_ids((uid_t)r, (uid_t)e, (uid_t)getgid()); }
/* The supplementary group list this library reports is one entry long
 * and holds the effective group ID -- getgroups.html leaves it
 * implementation-defined whether the effective gid appears there, and
 * with one identity there is nothing else to put in it.  gidsetsize is
 * an argument, though, and gets read as one: [EINVAL] is a *shall*-fail
 * for a gidsetsize "non-zero and less than the number of group IDs that
 * would have been returned", which every negative value is, given a
 * count of 1.  gidsetsize 0 asks for the count alone and must not touch
 * grouplist -- callers pass a null pointer for that form. */
int getgroups(int n, gid_t *g)
{
	const int held = 1;
	if (n != 0 && n < held) { errno = EINVAL; return -1; }
	if (n != 0) g[0] = getegid();
	return held;
}
/* The group and session this process is in; see the banner for why they
 * start at 1 and why a plain static is the whole of the model. */
static pid_t pgid = 1;
static pid_t sid = 1;

pid_t getpgrp(void) { return pgid; }

/* Does a process with this process ID exist, as far as this process can
 * tell?  getpgid.html and getsid.html both make "[ESRCH] There is no
 * process with a process ID equal to pid" a *shall*-fail, and that
 * clause is about the existence of a process rather than about
 * sessions: it binds this per-process implementation exactly as much as
 * any other.  The two getters below can only report this process's own
 * group and session, but they do so only for a pid that names something.
 *
 * Existence is decided the way kill() and getpriority() already decide
 * it (src/signal/signal.c, src/misc/resource.c), in three steps:
 *
 *   - pid 0 and this process's own pid are the caller, which exists by
 *     construction -- "If pid is equal to 0, getpgid() shall return the
 *     process group ID of the calling process".  Answered without an NT
 *     call, so the common form of both calls stays free of one.
 *   - a pid in the child table (src/process/children.c) is a process
 *     this one created and has not yet reaped.  This arm is not an
 *     optimisation of the NtOpenProcess below, because the two answer
 *     different questions: POSIX existence lasts until wait() collects
 *     the pid -- an exited-but-unreaped child is still a process, and
 *     the table entry src/process/wait.c holds open is precisely that
 *     state -- while openability by CLIENT_ID is a property of the NT
 *     process *object*, which the two platforms disagree about once it
 *     has exited (see wait.c's reopen-by-pid discussion: Wine and
 *     Windows differ on whether an exited pid can be opened at all).
 *     Consulting the table first is what keeps a zombie child from
 *     being reported nonexistent on the platform that says no.
 *   - anything else is put to the object manager by CLIENT_ID.
 *     STATUS_INVALID_CID -- and any other failure that is not a refusal
 *     -- means there is no such process.  STATUS_ACCESS_DENIED means
 *     the process is there and merely not ours to open, which is
 *     existence, not [ESRCH]; it cannot become [EPERM] here either,
 *     since that clause of both pages is about a process in a
 *     *different session* and there is only the one.
 *
 * A negative pid names no process (kill() answers ESRCH for one too),
 * and is rejected without troubling NT: NtOpenProcess takes an unsigned
 * CLIENT_ID, so sign-extending -1 into it would ask about pid
 * 0xffffffffffffffff instead of reporting the error.
 */
static int pid_exists(pid_t p)
{
	HANDLE h;
	NTSTATUS st;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;

	if (p == 0 || p == getpid()) return 1;
	if (p < 0) return 0;
	if (__child_find((int)p)) return 1;

	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	cid.UniqueProcess = (HANDLE)(ULONG_PTR)p;
	cid.UniqueThread = 0;
	st = NtOpenProcess(&h, PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid);
	if (!NT_SUCCESS(st)) return st == STATUS_ACCESS_DENIED;
	NtClose(h);
	return 1;
}

pid_t getpgid(pid_t p)
{
	if (!pid_exists(p)) { errno = ESRCH; return -1; }
	return pgid;
}
/* setpgid()'s [ESRCH] asks a *narrower* question than pid_exists()
 * above, so it gets its own helper rather than that one.  getpgid.html
 * and getsid.html fail for a pid that names no process at all;
 * setpgid.html fails for a pid that names no process *of the caller's*:
 * "[ESRCH] The value of the pid argument does not match the process ID
 * of the calling process or of a child process of the calling process."
 * A pid belonging to some unrelated process therefore has two different
 * right answers on this page and that one -- getpgid() must answer for
 * it, setpgid() must refuse it -- so reusing pid_exists() here would be
 * wrong in exactly the case the clause is about, not merely wasteful.
 *
 * The narrowing is what removes the NT call: pid_exists()'s third arm
 * puts an unknown pid to the object manager because "does this process
 * exist" is a question about the whole machine, but "is this a child of
 * mine" is answerable entirely from this process's own bookkeeping.
 * The child table (src/process/children.c) *is* that set: __child_add()
 * records every pid fork()/__spawn() creates and __child_remove() drops
 * it when wait() collects it, which is precisely POSIX's lifetime for
 * "a child process of the calling process" -- an exited-but-unreaped
 * child is still one, and a reaped one is not.  So setpgid() makes no
 * NT call for any argument.
 *
 * pid 0 is the caller by DESCRIPTION ("if pid is 0, the process ID of
 * the calling process shall be used"), and is answered before the table
 * is consulted for a second reason: __child_find(0) would match the
 * first *free* slot, since a free slot is one whose pid is 0.
 *
 * The two failures are checked pid-first because pgid cannot be fully
 * resolved until pid is: "if pgid is 0, the process ID of the indicated
 * process shall be used", and there is no indicated process to take it
 * from when pid names nothing of ours.
 *
 * [EINVAL] is then the range check its clause opens with -- "The value
 * of the pgid argument is less than 0, or is not a value supported by
 * the implementation" -- and only that.  The local pgid/sid state below
 * models the transitions this process can observe through setpgrp() and
 * setsid(); it does not provide a registry for changing another
 * process's group or joining an arbitrary group.  A non-negative
 * setpgid() request therefore remains a no-op after validation.
 * posix_spawn()'s POSIX_SPAWN_SETPGROUP reaches the opposite conclusion
 * from the same sentence (src/process/posix_spawn.c refuses any pgroup
 * but getpgrp()'s) because it has to decide, at process creation,
 * whether it can honour a flag it was handed; nothing about that binds
 * the plain no-op case here.
 */
static int pid_is_self_or_child(pid_t p)
{
	if (p == 0 || p == getpid()) return 1;
	if (p < 0) return 0;
	return __child_find((int)p) != 0;
}

int setpgid(pid_t pid, pid_t group)
{
	if (!pid_is_self_or_child(pid)) { errno = ESRCH; return -1; }
	if (group < 0) { errno = EINVAL; return -1; }
	return 0;
}
/* setpgrp.html DESCRIPTION: "If the calling process is not already a
 * session leader, setpgrp() sets the process group ID of the calling
 * process to the process ID of the calling process."  RETURN VALUE:
 * "Upon completion, setpgrp() shall return the process group ID."
 * ERRORS: "No errors are defined."
 *
 * Only the process-group half is done.  The page permits the System V
 * reading in which the call creates a session too ("If setpgrp()
 * creates a new session, then the new session has no controlling
 * terminal"), and that reading loses here: it would make setpgrp() a
 * setsid() that cannot fail, spending the one group-leadership
 * transition setsid()'s [EPERM] is defined in terms of, for an effect
 * the DESCRIPTION does not require of it. */
pid_t setpgrp(void)
{
	pid_t self = getpid();
	if (sid != self) pgid = self;
	return pgid;
}

/* setsid.html DESCRIPTION: "The setsid() function shall create a new
 * session, if the calling process is not a process group leader.  Upon
 * return the calling process shall be the session leader of this new
 * session, shall be the process group leader of a new process group,
 * and shall have no controlling terminal.  The process group ID of the
 * calling process shall be set equal to the process ID of the calling
 * process."  ERRORS: "[EPERM] The calling process is already a process
 * group leader ..."
 *
 * "Already a process group leader" is pgid == pid, false for a fresh
 * process and true of every process that has been through here once, so
 * the second call fails -- which is the whole of what the clause can
 * mean where no other process's group can be named.
 *
 * "Shall have no controlling terminal" is not modelled: the console is
 * the only terminal on this platform and nothing in src/termios/ or
 * src/unistd/isatty.c distinguishes a controlling one from any other,
 * so there is no state to drop.  tcgetsid()/tcgetpgrp() keep answering
 * for it, and follow this session and group rather than a constant. */
pid_t setsid(void)
{
	pid_t self = getpid();
	if (pgid == self) { errno = EPERM; return -1; }
	sid = pgid = self;
	return pgid;
}

pid_t getsid(pid_t p)
{
	if (!pid_exists(p)) { errno = ESRCH; return -1; }
	return sid;
}
/* There is nothing for the chown family to set -- NT has no POSIX owner
 * or group, and st_uid/st_gid report this process's current IDs -- but
 * "there is no ownership to change" is not "there is no path to resolve".
 *
 * chown.html ERRORS, all shall-fail:
 *   "[ENOENT] A component of path does not name an existing file or path
 *    is an empty string."
 *   "[ENOTDIR] A component of the path prefix names an existing file
 *    that is neither a directory nor a symbolic link to a directory ..."
 * lchown.html repeats both.  fchown.html: "[EBADF] The fildes argument
 * is not an open file descriptor."  chown.html's fchownat() section adds
 * [EBADF] for a dirfd that is neither AT_FDCWD nor a valid descriptor
 * and [ENOTDIR] for one that is not a directory.
 *
 * chown("does-not-exist", ...) returning 0 is not a statement about
 * ownership, it is a statement that the file exists, and it is false: an
 * installer chowning a list of files it has just laid down loses its
 * only report that one of them is missing, and `chown()` failing with
 * ENOENT is a standard existence probe.  So the path is resolved and the
 * object opened for FILE_READ_ATTRIBUTES, which is exactly the evidence
 * those clauses ask for and nothing more; the handle is closed again
 * without a write of any kind.
 *
 * __ntpath_at() produces the empty-path [ENOENT], the dirfd [EBADF]/
 * [ENOTDIR] and the path-prefix [ENOTDIR] itself (src/internal/path.c),
 * so only the final open is left to this function.
 *
 * fchownat()'s [EINVAL] for an unrecognised flag is a *may*-fail on
 * chown.html, unlike unlinkat()'s, and accepting the bits is the
 * behaviour test/posix-unistd-ids.c pins; only AT_SYMLINK_NOFOLLOW is
 * read out of them.  FILE_OPEN_FOR_BACKUP_INTENT, and neither
 * FILE_DIRECTORY_FILE nor FILE_NON_DIRECTORY_FILE, so that the call
 * works on a directory and on a regular file alike. */
static int chown_resolve(int dirfd, const char *path, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	OBJECT_ATTRIBUTES *oa;
	HANDLE h;
	NTSTATUS st;
	ULONG options;

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT |
		(flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0);
	oa = &np.oa;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, oa, &io, FILE_SHARE_VALID_FLAGS, options);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	NtClose(h);
	return 0;
}

int fchownat(int d, const char *p, uid_t u, gid_t g, int f)
{
	(void)u; (void)g;
	return chown_resolve(d, p, f);
}
int chown(const char *p, uid_t u, gid_t g) { return fchownat(AT_FDCWD, p, u, g, 0); }
int lchown(const char *p, uid_t u, gid_t g) { return fchownat(AT_FDCWD, p, u, g, AT_SYMLINK_NOFOLLOW); }
int fchown(int f, uid_t u, gid_t g) { (void)u; (void)g; return __fd_get(f) ? 0 : -1; }
/* nice() used to be `(void)incr; return 0;` here, among identity calls it
 * has nothing to do with.  It moved to src/misc/resource.c, beside the
 * one piece of state getpriority()/setpriority() already keep this
 * process's nice value in, so that the two interfaces cannot disagree
 * about it. */
int chroot(const char *p) { (void)p; errno = EPERM; return -1; }
int issetugid(void) { return 0; }
char *getlogin(void)
{
	extern char *getenv(const char *);
	char *u = getenv("USERNAME");
	return u ? u : getenv("USER");
}
int getlogin_r(char *buf, size_t n)
{
	char *l = getlogin();
	size_t i;
	if (!l) return ENXIO;
	for (i = 0; l[i] && i + 1 < n; i++) buf[i] = l[i];
	if (l[i]) return ERANGE;
	buf[i] = 0;
	return 0;
}
