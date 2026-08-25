/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * `test/POSIX-COVERAGE.md`'s audit so far has only ever asked "for a
 * header ntlibc *has*, are every function's clauses covered?".  That
 * question cannot find a header POSIX requires that ntlibc simply does
 * not have -- a full-source bootstrap found exactly that kind of gap
 * for <pwd.h>.  This file is the first pass at the other question, for
 * four headers ntlibc did not implement at all when it was written:
 *
 *   <dlfcn.h>    dynamic loading
 *   <sys/mman.h> memory mapping
 *   <termios.h>  terminal control
 *   <spawn.h>    posix_spawn()
 *
 * All four have since landed, so the list above is history and not a
 * statement about the tree: <dlfcn.h> (src/dlfcn/dlfcn.c,
 * exercised through the real header at the end of this file),
 * <spawn.h> (src/process/posix_spawn.c and siblings, clause-audited in
 * test/posix-spawn.c), and <termios.h> (include/termios.h and
 * src/termios/termios.c, clause-audited in test/posix-termios.c).
 * Their sections here are kept for the argument they make about what
 * NT can and cannot do, with the fences that the implementations
 * refuted removed rather than narrowed.
 *
 * <sys/mman.h> now joins them: include/sys/mman.h and src/mman/mman.c
 * ship, clause-audited in test/posix-mman.c, so this file includes the
 * real header and its local mman scaffolding is gone, with the fences
 * the implementation refuted removed rather than narrowed.
 *
 * Both <sys/mman.h> and <termios.h> are included like any other shipped
 * header.  This file does not add or modify any header.
 *
 * Every specified clause gets a real test, written as if it were going
 * to run, even where it cannot possibly pass today.  Three fences, same
 * convention as test/posix-sysmisc.c:
 *
 *   #if 0 / * BUG: <requirement + citation> * /     -- not used in this
 *   file: nothing here exists yet for there to be a behavioural bug in.
 *   #if 0 / * N/A: <requirement + citation + why NT can't> * / --
 *   genuinely impossible on this platform, not just "not written yet".
 *   #if 0 / * UNIMPL: <requirement + citation> * /  -- absent, but
 *   implementable; the fence names the NT mechanism that would do it.
 *
 * Each header gets a short unfenced section first, wherever ntlibc
 * already has *something* real to point the clause at under a
 * different name -- those assertions run and are counted like any
 * other test.  <dlfcn.h>, <spawn.h> and <termios.h> each have one;
 * <sys/mman.h> never did, because when this file was written nothing in
 * src/ mapped to it even partially -- that is no longer true, and
 * test/posix-mman.c is the section it never had.
 */
#include "test-policy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <termios.h>
#include "ntlibc/rpath.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Internal: spawn a program as a child (src/process/spawn.c), declared
 * locally the way test/misc.c and test/posix-alloc.c already do. */
int __spawn(const char *path, char *const argv[], char *const envp[]);

extern char **environ;

/* rpath.c requires the image to define this even when, as here, it is
 * populated with two entries, the first deliberately nonexistent:
 * ntlibc_rpath_load() only consults it for a dllname with no path
 * component, and almost every call below gives one -- but
 * test_dlerror_null_after_successful_dlopen() needs a bare name whose
 * lookup misses an earlier entry before hitting a later one, which is
 * the only shape that reaches rpath.c's per-entry error recording on a
 * call that then succeeds. A directory that does not exist is the
 * portable way to build that shape, and it changes nothing for the
 * path-qualified loads, which never consult this array at all. */
const char *const __rpath[] = { "no-such-rpath-dir", "C:\\Windows\\System32", 0 };

/* ============================================================
 * <dlfcn.h> -- dlopen/dlsym/dlclose/dlerror
 *
 * ntlibc already had the loading primitive dlopen() sits on top of:
 * ntlibc_rpath_load()/ntlibc_rpath_sym()/ntlibc_rpath_error()
 * (include/ntlibc/rpath.h, implemented in src/internal/rpath.c) wrap
 * exactly the two ntdll entry points a real dlopen()/dlsym() would
 * also use -- LdrLoadDll()/LdrGetProcedureAddress(). <dlfcn.h> and
 * src/dlfcn/dlfcn.c (a sibling agent's later pass) now exist and are
 * exercised directly below, unfenced, alongside this original
 * lower-level demonstration -- see dlfcn.c's own header comment for
 * how RTLD_* mode/scope semantics, dlclose()'s refcounting, and
 * dlerror()'s single-shot contract were each resolved on top of these
 * same rpath.c primitives, and include/dlfcn.h for the fuller design
 * rationale (RTLD_LOCAL's genuine N/A status, the $ORIGIN-vs-plain-
 * search decision for a bare filename, and dlopen(NULL, ...)'s limits
 * on a -nostdlib image).
 * ==============================================================
 */

/* ---- what already works, unfenced: the real loader plumbing ---- */
static void test_dl_underlying_mechanism(void)
{
	ntlibc_dll_t *dll;
	void *sym;

	/* dlopen.html RETURN VALUE: "If dlopen() is unable to load the
	 * shared object, it returns a null pointer and sets an error
	 * condition." ntlibc_rpath_load() already does exactly this for a
	 * DLL that plainly does not exist. */
	dll = ntlibc_rpath_load("C:\\this-dll-does-not-exist-at-all.dll");
	CHECK(dll == 0);

	/* dlerror.html DESCRIPTION: "returns a null-terminated character
	 * string ... that describes the last error that occurred." */
	CHECK(strcmp(ntlibc_rpath_error(), "no error") != 0);
	CHECK(strstr(ntlibc_rpath_error(), "this-dll-does-not-exist-at-all.dll") != 0);

	/* Loading a real, always-present module by an explicit path
	 * component (bypassing __rpath, see rpath.h) -- the same
	 * "resolve a module, then resolve a symbol in it" shape
	 * dlopen()+dlsym() give. ntdll.dll is guaranteed mapped in every
	 * NT process already, so this needs no companion DLL the way
	 * test/rpath.c's end-to-end delay-load case does. */
	dll = ntlibc_rpath_load("C:\\Windows\\System32\\ntdll.dll");
	if (dll) {
		/* dlsym.html RETURN VALUE: a non-null address for a symbol
		 * that exists in the handle. */
		sym = ntlibc_rpath_sym(dll, "RtlAllocateHeap");
		CHECK(sym != 0);

		/* dlsym.html RETURN VALUE: NULL, diagnosable via dlerror(),
		 * for a symbol that does not exist in the handle. */
		sym = ntlibc_rpath_sym(dll, "no_such_export_in_ntdll_at_all");
		CHECK(sym == 0);
		CHECK(strcmp(ntlibc_rpath_error(), "no error") != 0);
	}
	/* If ntdll could not even be resolved by an absolute path (would
	 * mean LdrLoadDll itself is broken), don't fail this file over an
	 * environment problem outside this test's scope -- every other
	 * assertion above already exercised the failure path fully. */
}

/* ---- the dlfcn.h surface itself ----
 *
 * RTLD_LAZY/RTLD_NOW/RTLD_GLOBAL/RTLD_LOCAL now come from <dlfcn.h>
 * itself (included above) rather than being redeclared locally the way
 * the other three not-implemented-at-all headers in this file still
 * do -- dlopen()/dlsym()/dlclose()/dlerror() are real now, so this is
 * no longer "declared locally, never included from include/" the way
 * this file's own header comment describes for the other sections. */

/* dlopen.html DESCRIPTION -- dlopen(path, RTLD_NOW) must load `path`
 * and make its symbols available to dlsym(). NT mechanism:
 * ntlibc_rpath_load() already *is* this, modulo the mode argument --
 * LdrLoadDll() resolves the target's own import table eagerly
 * regardless of mode, so RTLD_NOW is trivially satisfiable (dlopen
 * always behaves as if RTLD_NOW was given) and RTLD_LAZY can only ever
 * be honoured as a no-op alias for it: ntdll has no per-import
 * lazy-binding stub mechanism to defer to (unlike this project's own
 * delay-load machinery in crt/delayload2.c, which is opt-in per import
 * and not something LdrLoadDll's ordinary resolution path uses). */
static void test_dlopen_now_lazy(void)
{
	void *h1 = dlopen("C:\\Windows\\System32\\ntdll.dll", RTLD_NOW);
	void *h2 = dlopen("C:\\Windows\\System32\\ntdll.dll", RTLD_LAZY);
	CHECK(h1 != 0);
	CHECK(h2 != 0);   /* RTLD_LAZY honoured as RTLD_NOW; still a load that succeeds */
	if (h1) dlclose(h1);
	if (h2) dlclose(h2);
}

#if NTLIBC_TEST(NA, posix_dl_dlopen_rtld_local_scoping) /* N/A: dlopen.html DESCRIPTION -- RTLD_LOCAL: "symbols ... are
	not made available to resolve references in subsequently
	loaded shared objects" (the default, when RTLD_GLOBAL is not
	given). The NT loader has no notion of a module-scoped symbol
	table at all: every DLL's export directory
	(src/internal/pe.c's own ntlibc_pe_find_export, and the
	LdrGetProcedureAddress a real dlsym() would use) is reachable
	from any handle on that module process-wide the moment
	LdrLoadDll() maps it, and there is no ntdll call that narrows a
	module's own exports out of a *different* module's own
	resolution (that is what makes delayload.h's approach --
	binding one specific import slot to one specific resolved
	address, rather than asking the loader to search -- necessary
	here in the first place, see src/internal/delayload.c). RTLD_LOCAL
	is thus not "not written yet": there is no loader primitive to
	write it against, only a wholesale reimplementation of PE import
	resolution the way rpath.c/pe.c narrowly does for one
	self-contained case. RTLD_GLOBAL, being what LdrLoadDll always
	does, is not a gap at all -- it is the only mode NT offers. */
static void test_dlopen_rtld_local_scoping(void)
{
	void *h = dlopen("C:\\Windows\\System32\\ntdll.dll", RTLD_NOW | RTLD_LOCAL);
	CHECK(h != 0);
	/* A second, unrelated module loaded afterwards must NOT be able
	 * to resolve ntdll's exports through its own dlsym() as a side
	 * effect of ntdll having been RTLD_LOCAL-loaded first -- this is
	 * exactly the isolation NT's loader does not provide. */
	if (h) dlclose(h);
}
#endif

/* dlclose.html DESCRIPTION -- "decrements the reference count ... If
 * the reference count drops to 0 ... the object is unloaded." NT
 * mechanism: LdrUnloadDll() (src/internal/nt.h) against the loader
 * data table entry's own LoadCount field, which LdrLoadDll() already
 * maintains -- see ntlibc_rpath_unload()'s own comment
 * (src/internal/rpath.c) for the Wine-source confirmation that
 * LdrLoadDll()/LdrUnloadDll() refcount this without any help from
 * ntlibc. */
static void test_dlclose_refcounts(void)
{
	void *h1 = dlopen("C:\\Windows\\System32\\ntdll.dll", RTLD_NOW);
	void *h2 = dlopen("C:\\Windows\\System32\\ntdll.dll", RTLD_NOW);
	CHECK(h1 != 0 && h2 != 0);
	/* Same module, so LdrLoadDll() must have handed back the same
	 * base address and merely bumped the refcount. */
	CHECK(h1 == h2);
	CHECK(dlclose(h1) == 0);
	/* Still mapped -- refcount was 2, only one dlclose() so far. */
	CHECK(dlsym(h2, "RtlAllocateHeap") != 0);
	CHECK(dlclose(h2) == 0);
}

/* dlerror.html DESCRIPTION -- "If a call ... to dlerror() ... returns
 * non-NULL, then a subsequent call ... shall return NULL, unless an
 * intervening call to dlopen() or dlsym() returned NULL and set the
 * error condition." This is the single-shot-consumption contract
 * implementations most often get wrong -- and it is exactly the one
 * clause ntlibc_rpath_error() (src/internal/rpath.c) deliberately does
 * NOT implement (see test_dl_underlying_mechanism() above, which
 * relies on it staying sticky). src/dlfcn/dlfcn.c's dlerror() layers
 * the single-shot contract on top using ntlibc_rpath_error_seq()
 * instead, without changing ntlibc_rpath_error() itself. */
static void test_dlerror_consumed_once(void)
{
	void *h = dlopen("C:\\this-does-not-exist.dll", RTLD_NOW);
	CHECK(h == 0);
	CHECK(dlerror() != 0);     /* first call after the failure: non-NULL */
	CHECK(dlerror() == 0);     /* second consecutive call: NULL */
	CHECK(dlerror() == 0);     /* still NULL -- no error is pending, not "half-consumed" */
}

/* ============================================================
 * <sys/mman.h> -- mmap/munmap/mprotect/msync/mlock
 *
 * The argument this section made, when nothing in src/ mapped to this
 * header even partially: the only memory facility here was
 * RtlAllocateHeap-backed malloc() (src/malloc/malloc.c), a different
 * abstraction entirely -- a sub-allocator over one growable heap, not
 * page-granular address-space control -- and the ntdll calls POSIX's
 * model maps onto were mostly undeclared.
 *
 * include/sys/mman.h and src/mman/mman.c now ship, and
 * test/posix-mman.c is the clause audit against the real header.  All
 * seven fences that stood here are gone, and NOT because they were
 * merely duplicated: every one of them was WRONG, in the same way, and
 * the way is worth recording.
 *
 * Each fenced body called
 *
 *     mmap(0, n, prot, MAP_PRIVATE, -1, (off_t_local)0)
 *
 * and asserted `p != MAP_FAILED`.  That call must FAIL.  POSIX Issue 7
 * -- the edition this tree speaks, 48 citations across include/ and src/
 * against 4 for Issue 8 -- has no anonymous mapping at all: mmap.html
 * does not mention MAP_ANONYMOUS or MAP_ANON anywhere, DESCRIPTION
 * requires a mapping to be "between the address space of the process ...
 * and the memory object represented by the file descriptor fildes", and
 * ERRORS makes "[EBADF] The fildes argument is not a valid open file
 * descriptor" a SHALL FAIL.  Measured against glibc rather than derived:
 * that exact call returns MAP_FAILED with errno EBADF, and succeeds only
 * once MAP_ANONYMOUS is added.
 *
 * So seven fences asserted success for a case the standard requires to
 * fail, and they did it because the local scaffolding below them was
 * written from memory of what mmap() looks like on Linux rather than
 * from the page.  Nothing about the fences' NT reasoning was wrong --
 * NtCreateSection/NtMapViewOfSection/PAGE_WRITECOPY are all real and
 * the mprotect fence's translation table was exactly right -- but the
 * expected values were derived, not observed, and a fence with a wrong
 * expected value is worse than no fence: it certifies the wrong thing
 * the day someone un-fences it.
 *
 * The clauses moved to test/posix-mman.c and gained MAP_ANONYMOUS, which
 * moves them toward the specification, not away from it.  <sys/mman.h>
 * ships MAP_ANONYMOUS as a documented non-POSIX extension behind
 * _BSD_SOURCE/_GNU_SOURCE, the same gate <signal.h> puts on sigorset().
 * File-backed mmap() is refused with [ENODEV] and stays fenced -- in
 * test/posix-mman.c, retagged as a decline-for-now with the
 * MEM_RESERVE_PLACEHOLDER route and its Windows 10 1803+ floor named.
 * ==============================================================
 */

/* ============================================================
 * <termios.h> -- the best partial mapping in this group, and the one
 * that has since been built.
 *
 * The argument this section made, when <termios.h> did not exist: ntlibc
 * already has isatty() (src/unistd/isatty.c), which is exactly the gate
 * a real tcgetattr() would need first (termios.html ERRORS: "[ENOTTY]
 * The file associated with fildes is not a terminal."). Beyond that
 * gate, NT consoles have a real, partial analogue via kernel32's
 * GetConsoleMode()/SetConsoleMode() (reached the same
 * LdrLoadDll("kernel32.dll") way as SetConsoleCtrlHandler in
 * src/signal/signal.c) -- but the match is genuinely partial, not a
 * blanket yes or no: canonical-mode and echo control have a real
 * console-mode bit each; baud rate, parity, stop bits, and flow
 * control do not exist for a console handle at all (those belong to
 * an actual serial port, reached through an entirely different
 * kernel32 API -- GetCommState()/SetCommState() over a COM port
 * handle -- which ntlibc's isatty() does not and should not
 * recognise as a tty in the termios sense).
 *
 * That argument was taken up.  include/termios.h and
 * src/termios/termios.c ship, and they use exactly the mechanisms the
 * two UNIMPL fences here used to name: GetConsoleMode()/SetConsoleMode()
 * (src/termios/termios.c:191 and :231) for c_lflag's ICANON/ECHO/ISIG,
 * and FlushConsoleInputBuffer() (:290) for tcflush()'s input side.  Both
 * fences are therefore gone; the clauses are audited for real in
 * test/posix-termios.c, against the shipped header, which is where a
 * clause belongs once the header exists.
 *
 * What is left in this section is N/A, and shipping the header did not
 * make any of it less N/A.  A console has no baud rate, no transmit
 * queue, no line discipline, and no reprogrammable special-character
 * table; "the function exists and the clause is still inapplicable" is
 * exactly what N/A is for.  These bodies now use the real struct
 * termios, so each one is a fence that could be lifted the day the
 * platform grew the concept -- which is the only thing a fence is good
 * for.
 * ==============================================================
 */

/* ---- the real prerequisite: isatty() already gates correctly ---- */
static void test_termios_isatty_prerequisite(void)
{
	/* unistd.h isatty.html: "shall test whether fildes ... is
	 * associated with a terminal device." A real tcgetattr() would
	 * fail ENOTTY for exactly the fds isatty() already says are not
	 * a tty for. stdin under `make check`'s runner is redirected
	 * (tools/run-tests.py), not a console, and a definitely-invalid
	 * fd is never a tty either -- both must read as "not a tty". */
	CHECK(isatty(1000) == 0);
	CHECK(errno == EBADF || errno == ENOTTY);
}

/* No local scaffolding below.  struct termios_local and the local
 * ICANON/ECHO/ISIG/TCSANOW/TC*FLUSH defines that used to stand here
 * existed only because <termios.h> was absent; every one of them is now
 * the shipped header's, included at the top of this file.  Keeping them
 * would have been worse than redundant: each fenced body passed a
 * `struct termios_local *` to `int tcgetattr(int, struct termios *)`,
 * so un-fencing one would not have compiled. */

/* Not fenced: tcgetattr()/tcsetattr() round-tripping c_lflag's
 * ICANON/ECHO.  This carried an UNIMPL fence naming ENABLE_LINE_INPUT
 * and ENABLE_ECHO_INPUT as the console-mode bits that correspond to
 * them, and saying "only the wrapper is missing".  The wrapper was
 * written: src/termios/termios.c maps ICANON/ECHO/ISIG onto
 * ENABLE_LINE_INPUT/ENABLE_ECHO_INPUT/ENABLE_PROCESSED_INPUT through
 * GetConsoleMode() (:191) and SetConsoleMode() (:231).  There is no gap
 * left to fence, and duplicating the clause here against a local struct
 * would only make it easier to forget which copy is real --
 * test/posix-termios.c tests it against the shipped header. */

#if NTLIBC_TEST(NA, posix_dl_termios_cc_special_chars) /* N/A: termios.html struct termios DESCRIPTION -- c_cc[] special
	characters (VINTR, VEOF, VERASE, VKILL, ...): "control
	character values". A real console's Ctrl-C handling is fixed
	kernel behaviour wired to SetConsoleCtrlHandler
	(src/signal/signal.c already uses exactly this), not a
	per-character table SetConsoleMode or any other kernel32 call
	can reprogram -- there is no console-mode bit or API that lets a
	process choose, say, Ctrl-X as its new interrupt character the
	way VINTR does on a real tty line discipline. ENABLE_PROCESSED_
	INPUT only turns Ctrl-C handling on or off wholesale; it does
	not parameterise which character triggers it. */
static void test_termios_cc_special_chars(void)
{
	struct termios t;
	CHECK(tcgetattr(0, &t) == 0);
	t.c_cc[VINTR] = 24; /* VINTR := Ctrl-X instead of the default Ctrl-C */
	CHECK(tcsetattr(0, TCSANOW, &t) == 0);
	/* A real terminal would now deliver SIGINT on Ctrl-X, not
	 * Ctrl-C; nothing exists to observe that without a live
	 * interactive session, so only the (impossible) set is
	 * asserted. */
}
#endif

#if NTLIBC_TEST(NA, posix_dl_termios_baud_rate) /* N/A: termios.html cfgetispeed.html/cfsetospeed.html etc. --
	baud rate is a property of a physical (or virtual) serial line's
	clocking, not of a console session at all. A Windows console
	handle has no bit rate, parity, or stop-bit configuration --
	those exist only for an actual COM-port device opened by name
	(kernel32 GetCommState()/SetCommState() over a DCB, a completely
	separate device class from HANDLEs isatty() calls a tty). Since
	ntlibc's isatty() (src/unistd/isatty.c) only recognises
	__FD_CONSOLE as a terminal -- correctly, since that is what
	POSIX programs mean by "the controlling terminal" on this
	platform -- there is no reachable fd for which cfsetospeed()
	could mean anything at all, not merely one ntlibc has not wired
	up yet. */
static void test_termios_baud_rate(void)
{
	struct termios t;
	CHECK(tcgetattr(0, &t) == 0);
	CHECK(cfsetispeed(&t, B9600) == 0);
	CHECK(cfsetospeed(&t, B9600) == 0);
	CHECK(cfgetispeed(&t) == B9600);
	CHECK(cfgetospeed(&t) == B9600);
}
#endif

/* Not fenced: tcflush(TCIFLUSH), "discard[s] data received but not
 * read".  The fence that stood here named FlushConsoleInputBuffer() as
 * "a real, exact match for the input side"; src/termios/termios.c:290
 * calls exactly that.  Implemented, not implementable.  The output side
 * is a different clause with a different answer, and keeps its N/A
 * fence immediately below. */

#if NTLIBC_TEST(NA, posix_dl_tcflush_output) /* N/A: tcflush.html DESCRIPTION -- TCOFLUSH/TCIOFLUSH:
	"discard[s] data written ... but not transmitted". A console
	handle has no transmit buffer to discard from in the first place
	-- WriteConsole()/WriteFile() to a console completes only once
	the characters are already placed in the screen buffer (there is
	no serial-line transmission queue sitting between the write call
	and the display the way there is for a real UART), so there is
	nothing an NT console API could discard that the write has not
	already finished doing. */
static void test_tcflush_output(void)
{
	CHECK(tcflush(1, TCOFLUSH) == 0);
	CHECK(tcflush(1, TCIOFLUSH) == 0);
}
#endif

#if NTLIBC_TEST(NA, posix_dl_tcdrain_noop_only) /* N/A: tcdrain.html DESCRIPTION -- "wait until all output
	written ... has been transmitted." Same reasoning as TCOFLUSH
	above: a console write is synchronously complete (the data is
	already in the screen buffer) by the time WriteConsole() returns,
	so there is no separate "still in flight" state for tcdrain() to
	wait out -- it could only ever be a no-op that returns
	immediately, never a genuine wait, on this fd class. */
static void test_tcdrain_noop_only(void)
{
	CHECK(tcdrain(1) == 0);
}
#endif

#if NTLIBC_TEST(NA, posix_dl_tcsendbreak_unsupported) /* N/A: tcsendbreak.html DESCRIPTION -- "transmit[s] a
	continuous stream of zero-valued bits for a specific duration"
	(a break condition, a physical-layer concept for an actual
	serial line, per POSIX's own Rationale on this page: "on
	terminals that do not support the break condition, this
	function shall not do anything"). A Windows console is not a
	serial line at all -- there is no signalling layer underneath it
	for a break condition to exist on -- so this is squarely the
	"terminal does not support the break condition" case the spec
	itself already permits as a legal, portable no-op, not a gap. */
static void test_tcsendbreak_unsupported(void)
{
	CHECK(tcsendbreak(0, 0) == 0);  /* legal no-op per the page's own Rationale */
}
#endif

#if NTLIBC_TEST(NA, posix_dl_termios_cflag_serial_bits) /* N/A: termios.html struct termios DESCRIPTION -- c_cflag's
	CS5/CS6/CS7/CS8 (character size), PARENB/PARODD (parity),
	CSTOPB (stop bits), and the XSI CRTSCTS/hardware-flow-control
	bit are all properties of a physical serial line's wire
	encoding. None of them have any meaning for a console session:
	console I/O is already framed as whole UTF-16 code units through
	ReadConsole()/WriteConsole(), there is no per-byte wire framing
	for a "character size" to describe, and there are no RTS/CTS
	lines on a console handle for hardware flow control to gate. */
static void test_termios_cflag_serial_bits(void)
{
	struct termios t;
	CHECK(tcgetattr(0, &t) == 0);
	t.c_cflag = (t.c_cflag & ~(tcflag_t)CSIZE) | CS8;
	CHECK(tcsetattr(0, TCSANOW, &t) == 0);
}
#endif

/* ============================================================
 * <spawn.h> -- posix_spawn()/posix_spawnp()
 *
 * <spawn.h> was absent when this file was written, and this section
 * argued the shape an implementation would take: __spawn()
 * (src/process/spawn.c) already inherits the *whole* non-close-on-exec
 * fd table by index, not just fds 0-2 (src/internal/fd.c's
 * __fd_runtime_data() walks every slot up to FD_MAX), so a file-actions
 * replay reduces to performing the equivalent open()/dup2()/close() in
 * the parent immediately before calling __spawn().
 *
 * That is now the shipped implementation -- include/spawn.h,
 * src/process/posix_spawn.c, src/process/spawn_file_actions.c,
 * src/process/spawnattr.c -- and test/posix-spawn.c is the clause audit
 * of it.  What remains here is the section's own historical argument
 * plus the one gap that outlived it (POSIX_SPAWN_SETSIGMASK with a
 * non-empty mask, fenced UNIMPL below); the unfenced demonstration
 * below still runs, as a check that the __spawn() inheritance the whole
 * design rests on behaves the way the argument claimed.
 * ==============================================================
 */

/* ---- what already works, unfenced: fd remap via __spawn()'s
 * existing whole-table inheritance, i.e. what
 * posix_spawn_file_actions_adddup2()'s effect already reduces to ---- */
static void test_spawn_fd_remap_via_existing_inheritance(const char *self)
{
	int p[2];
	char *argv[3];
	int saved2, pid, status;
	char buf[16];
	ssize_t n;

	CHECK(pipe(p) == 0);
	if (p[0] < 0) return;

	/* Emulate posix_spawn_file_actions_adddup2(&fa, p[1], 2): the
	 * child's fd 2 should become the pipe's write end. __spawn()
	 * inherits by table index, so doing the dup2() here, in the
	 * parent, immediately before the call reproduces exactly that --
	 * the same technique posix_spawn_file_actions replay would use
	 * one layer down. Fd 2 is saved and restored around the call so
	 * this test does not permanently disturb its own stderr. */
	saved2 = dup(2);
	CHECK(saved2 >= 0);
	CHECK(dup2(p[1], 2) >= 0);
	close(p[1]);

	argv[0] = (char *)self;
	argv[1] = (char *)"--spawn-fd-child";
	argv[2] = 0;
	pid = __spawn(self, argv, environ);

	if (saved2 >= 0) { dup2(saved2, 2); close(saved2); }

	CHECK(pid >= 0);
	if (pid >= 0) {
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
	n = read(p[0], buf, sizeof buf - 1);
	close(p[0]);
	CHECK(n > 0);
	if (n > 0) { buf[n] = 0; CHECK(strstr(buf, "child-fd2") != 0); }
}

struct spawn_file_action_local { int kind; int fd, newfd; const char *path; int flags; };
struct spawn_attr_local { unsigned short flags; int pgroup; };
#define POSIX_SPAWN_SETPGROUP    0x01
#define POSIX_SPAWN_SETSIGDEF    0x02
#define POSIX_SPAWN_SETSIGMASK   0x04
#define POSIX_SPAWN_SETSCHEDPARAM 0x08
#define POSIX_SPAWN_SETSCHEDULER  0x10
#define POSIX_SPAWN_RESETIDS      0x20
#define POSIX_SPAWN_USEVFORK      0x40

/* No fence for posix_spawn_file_actions_t.  This file used to carry an
 * UNIMPL fence here arguing that the object was implementable directly
 * on top of __spawn() by replaying each recorded action against the
 * *parent's* fd table immediately before the call and undoing it after,
 * safe because ntlibc has no threads to race the table.  That is now
 * exactly what src/process/posix_spawn.c does, action for action, and
 * src/process/spawn_file_actions.c is the recording half -- so
 * posix_spawn.html DESCRIPTION step 3 ("The file actions ... shall be
 * performed in the order in which they were added") is implemented, not
 * merely implementable, and there is no gap left to fence.  The clauses
 * are tested for real in test/posix-spawn.c, against the shipped
 * <spawn.h>, rather than duplicated here against local declarations. */


#if NTLIBC_TEST(NA, posix_dl_spawn_resetids) /* N/A: posix_spawn.html DESCRIPTION -- POSIX_SPAWN_RESETIDS:
	"reset the effective user ID ... to the real user ID, and the
	effective group ID ... to the real group ID". NT's access-token
	model has no distinct real/effective/saved-set-id triple the way
	POSIX credentials do (a token simply has a set of SIDs and
	privileges; there is no setuid-binary-style "effective differs
	from real" state a spawned child could reset in the first
	place), so there is no NT operation this flag could ever be
	wired to -- not a missing wrapper, a missing concept. */
static void test_spawn_resetids(void)
{
	struct spawn_attr_local attr;
	attr.flags = POSIX_SPAWN_RESETIDS;
	CHECK(attr.flags == POSIX_SPAWN_RESETIDS);
}
#endif

#if NTLIBC_TEST(NA, posix_dl_spawn_setschedparam) /* N/A in this audit file: posix_spawn.html DESCRIPTION --
	POSIX_SPAWN_SETSCHEDPARAM/POSIX_SPAWN_SETSCHEDULER: apply a
	scheduling policy/priority to the child before it starts
	running. NT mechanism: __spawn() already creates the child
	*suspended* (RtlCreateUserProcess(...) followed by a separate
	NtResumeThread(info.Thread, 0), src/process/spawn.c) specifically
	so its very first instruction has not executed yet -- exactly
	the window a real implementation needs. kernel32's
	SetPriorityClass()/SetThreadPriority() (or ntdll's
	NtSetInformationProcess()/NtSetInformationThread() directly,
	avoiding the kernel32 reach-out other N/A entries in this file
	need) on info.Process/info.Thread, called in that same window
	before the existing NtResumeThread() call, is a real, unused
	hook point that is already half-built by accident of how
	__spawn() has to create the process in the first place. The executable
	behavior is covered by posix_spawn_setschedparam_applied; this block
	only checks a local flag constant and is not a BUG test. */
static void test_spawn_setschedparam(void)
{
	struct spawn_attr_local attr;
	attr.flags = POSIX_SPAWN_SETSCHEDPARAM | POSIX_SPAWN_SETSCHEDULER;
	CHECK(attr.flags & POSIX_SPAWN_SETSCHEDPARAM);
}
#endif

/* POSIX_SPAWN_SETSIGDEF is not fenced at all.  "the signals ... shall
 * be set to their default actions in the child": src/signal/signal.c's
 * disposition table (`handlers[]`) is a static, an NT process created by
 * RtlCreateUserProcess runs its own crt1 before main(), and nothing
 * carries a parent's dispositions across -- so every signal in every
 * fresh child is already SIG_DFL, whatever subset the caller names.
 * src/process/posix_spawn.c accepts the flag on exactly that ground
 * (check_attr() lets it through unconditionally), and the postcondition
 * holds by construction rather than being ignored.  Nothing missing. */

#if NTLIBC_TEST(NA, posix_dl_spawn_setsigmask) /* N/A in this audit file: posix_spawn.html DESCRIPTION -- POSIX_SPAWN_SETSIGMASK
	with a *non-empty* mask: "the signal mask of the child process
	shall be set to the signal set specified in the spawn-sigmask
	attribute".  src/process/posix_spawn.c honours the empty mask,
	which is true by construction (`blocked` in src/signal/signal.c is
	a static, so a fresh child's mask is empty -- and it is the case
	GNU make actually takes, calling sigemptyset() before
	posix_spawnattr_setsigmask()), and refuses a non-empty one with
	EINVAL rather than accepting it and silently not installing it.

	This fence previously read N/A on the grounds that there is "no
	channel to hand a chosen initial mask/disposition to a child that
	has not yet run its own startup code".  That reason is false, and
	was already false when it was written: RTL_USER_PROCESS_PARAMETERS'
	RuntimeData is such a channel.  It is packed into the parameters
	block by RtlCreateProcessParametersEx (src/process/spawn.c),
	carries the inherited descriptor table today, is read back by
	__fd_init (src/internal/fd.c) before main(), and
	test/spawn-runtimedata-stress.c exercises it hard enough to have
	caught a dangling-pointer bug in it.  The mechanism exists.

	What is missing is a format and a reader: RuntimeData's layout is
	msvcrt's inherited-descriptor table (count, then osfile[], then
	osfhnd[]) precisely so an ntlibc child and an msvcrt child can each
	read the other's, so a mask would have to be an ntlibc-specific
	trailer past the count msvcrt stops at, picked up by __fd_init or a
	sibling initialiser.  And even then it would not be POSIX's
	promise: on POSIX the kernel carries the mask across exec so it
	applies to any image, whereas a RuntimeData trailer reaches an
	ntlibc-built child only and would silently do nothing for cmd.exe.
	Choosing not to build that is UNIMPL, never N/A.  test/posix-
	spawn.c carries the same finding against the shipped <spawn.h>, and
	posix_spawn_setsigmask_nonempty_is_delivered is the executable BUG
	case. This block only checks a local flag constant. */
static void test_spawn_setsigmask(void)
{
	struct spawn_attr_local attr;
	attr.flags = POSIX_SPAWN_SETSIGMASK;
	CHECK(attr.flags & POSIX_SPAWN_SETSIGMASK);
}
#endif

#if NTLIBC_TEST(NA, posix_dl_spawn_setpgroup) /* N/A: posix_spawn.html DESCRIPTION -- POSIX_SPAWN_SETPGROUP:
	"set the process group ID of the new process ... as if by
	setpgid()." NT has no process-group concept in the POSIX sense
	at all (no getpgid()/setpgid() anywhere in this tree, and no NT
	kernel object plays that role -- a job object groups processes
	for resource limits, not for job-control signal delivery, which
	is what a POSIX process group is actually for). Not implementable
	without first inventing process groups for this platform
	wholesale, which is out of this header's scope entirely. */
static void test_spawn_setpgroup(void)
{
	struct spawn_attr_local attr;
	attr.pgroup = 0;
	attr.flags = POSIX_SPAWN_SETPGROUP;
	CHECK(attr.flags == POSIX_SPAWN_SETPGROUP);
}
#endif

/* Not fenced: POSIX_SPAWN_USEVFORK (XSI/obsolescent -- "the
 * implementation may use vfork() ... instead of fork()") is purely a
 * performance hint about *how* the new process image comes into
 * being, not an observable behaviour. __spawn() never forks in the
 * first place -- every ntlibc process creation is already a direct
 * RtlCreateUserProcess(), the NT equivalent of an already-vfork-like
 * "no copy of the parent's address space" creation -- so this flag is
 * satisfied by construction, with nothing to implement or fence. */
static void test_spawn_usevfork_trivially_satisfied(void)
{
	CHECK(1); /* __spawn() never copies the parent's address space */
}


/* ==== clauses the successor-queue <dlfcn.h> audit added ================== */

/* dlopen.html DESCRIPTION: "If file is a null pointer, dlopen() shall
 * return a global symbol table handle for the currently running
 * process image." dlclose.html: a handle that was successfully opened
 * closes with 0. The handle half is checkable here; what dlsym() can
 * see through it is a separate clause, fenced below. */
static void test_dlopen_null_returns_a_handle(void)
{
	void *g = dlopen(NULL, RTLD_NOW | RTLD_GLOBAL);
	CHECK(g != NULL);
	if (g) CHECK(dlclose(g) == 0);
}

/* dlsym.html DESCRIPTION: "if the symbol named by name cannot be found
 * ... dlsym() shall return a null pointer. More detailed diagnostic
 * information shall be available through dlerror()", together with
 * dlerror.html's disambiguation idiom -- clear the error, call, then
 * check dlerror() -- which exists precisely because a NULL return is
 * ambiguous with a symbol whose value is NULL.
 *
 * test_dl_underlying_mechanism() exercises this at the
 * ntlibc_rpath_sym()/ntlibc_rpath_error() layer; nothing exercised it
 * through the <dlfcn.h> surface, which is what an application sees. */
static void test_dlsym_failure_through_dlfcn(void)
{
	void *h = dlopen("C:\\Windows\\System32\\ntdll.dll", RTLD_NOW);
	char *e;

	CHECK(h != NULL);
	if (!h) return;

	/* Success must leave nothing pending, or the idiom cannot work. */
	(void)dlerror();
	CHECK(dlsym(h, "RtlAllocateHeap") != NULL);
	CHECK(dlerror() == NULL);

	(void)dlerror();
	CHECK(dlsym(h, "no_such_export_in_ntdll_at_all") == NULL);
	e = dlerror();
	CHECK(e != NULL);
	if (e) CHECK(strstr(e, "no_such_export_in_ntdll_at_all") != NULL);
	CHECK(dlerror() == NULL);		/* one-shot */

	/* dlsym.html: "If handle does not refer to a valid symbol table
	 * handle ... dlsym() shall return a null pointer." Only the NULL
	 * handle is asserted: it is answered entirely inside ntlibc, so
	 * it is deterministic. A synthetic non-NULL garbage base is
	 * deliberately not tested -- what LdrGetProcedureAddress() does
	 * with an unmapped address is not contractually specified on
	 * either Wine or real NT. */
	(void)dlerror();
	CHECK(dlsym(NULL, "RtlAllocateHeap") == NULL);
	CHECK(dlerror() != NULL);

	CHECK(dlclose(h) == 0);
}

/* dlclose.html RETURN VALUE: "If handle does not refer to an open
 * symbol table handle or if the symbol table handle could not be
 * closed, dlclose() shall return a non-zero value. More detailed
 * diagnostic information shall be available through dlerror()."
 * test_dlopen_now_lazy() calls dlclose() but never checks a failure
 * path. The NULL handle is used for the same reason as above: it is
 * answered inside ntlibc, before any NT call. */
static void test_dlclose_invalid_handle(void)
{
	(void)dlerror();
	CHECK(dlclose(NULL) != 0);
	CHECK(dlerror() != NULL);
	CHECK(dlerror() == NULL);
}

/* dlerror.html DESCRIPTION: "shall return a null-terminated character
 * string (with no trailing <newline>) that describes the last error
 * that occurred". The no-trailing-newline requirement and the
 * describes-the-last-error requirement were both unasserted -- the
 * existing test_dlerror_consumed_once() only checks non-NULL then
 * NULL. */
static void test_dlerror_message_shape(void)
{
	char *e;
	size_t n;

	(void)dlerror();
	CHECK(dlopen("C:\\no-such-dll-anywhere-xyz.dll", RTLD_NOW) == NULL);
	e = dlerror();
	CHECK(e != NULL);
	if (e) {
		n = strlen(e);
		CHECK(n > 0);
		CHECK(e[n - 1] != '\n');
		CHECK(strstr(e, "no-such-dll-anywhere-xyz.dll") != NULL);
	}
	CHECK(dlerror() == NULL);
}

/* dlopen.html DESCRIPTION: "Only a single copy of an executable object
 * file shall be brought into the address space, even if dlopen() is
 * invoked multiple times in reference to the executable object file,
 * and even if different pathnames are used to reference the executable
 * object file."
 *
 * test_dlclose_refcounts() checks the identical-string case. This is
 * the different-pathname case, using forward slashes -- which
 * src/internal/rpath.c normalises itself, so the assertion is decided
 * by ntlibc's own code rather than by the loader's module-identity
 * rules. The case-insensitivity variant is deliberately not asserted:
 * Wine's and real NT's module identity diverge there, and this file
 * runs under both. */
static void test_dlopen_single_copy_different_pathnames(void)
{
	void *a = dlopen("C:\\Windows\\System32\\ntdll.dll", RTLD_NOW);
	void *b = dlopen("C:/Windows/System32/ntdll.dll", RTLD_NOW);

	CHECK(a != NULL && b != NULL);
	if (a && b) CHECK(a == b);
	if (a) CHECK(dlclose(a) == 0);
	if (b) CHECK(dlclose(b) == 0);
}

/* dlfcn.h.html: "The <dlfcn.h> header shall define the following
 * symbolic constants for use in the construction of a dlopen() mode
 * argument: RTLD_LAZY, RTLD_NOW, RTLD_GLOBAL, RTLD_LOCAL." They are
 * combined with bitwise OR at every call site in the spec's own
 * examples, so each has to be independently representable.
 *
 * Recorded from the fetched text, because it is easy to assume the
 * opposite: dlopen.html defines *no* errors ("No errors are defined"),
 * says of RTLD_GLOBAL/RTLD_LOCAL only that "the default behavior is
 * unspecified" when neither is given, and gives no [EINVAL] for any
 * mode at all. glibc's rejection of an invalid mode is an extension.
 * So ntlibc accepting any mode is conforming, and no test here asserts
 * a rejection. */
static void test_dlfcn_header_constants(void)
{
	CHECK((RTLD_LAZY & RTLD_NOW) == 0);
	CHECK((RTLD_GLOBAL & RTLD_LOCAL) == 0);
	CHECK(RTLD_LAZY != 0 && RTLD_NOW != 0 && RTLD_GLOBAL != 0 && RTLD_LOCAL != 0);
	CHECK((RTLD_LAZY & RTLD_GLOBAL) == 0 && (RTLD_LAZY & RTLD_LOCAL) == 0);
	CHECK((RTLD_NOW & RTLD_GLOBAL) == 0 && (RTLD_NOW & RTLD_LOCAL) == 0);
}

#if NTLIBC_TEST(BUG, posix_dl_dlerror_null_after_successful_dlopen) /* BUG: dlerror.html DESCRIPTION -- "If no dynamic linking errors
	have occurred since the last invocation of dlerror(), dlerror()
	shall return NULL."

	A *successful* dlopen() of a bare name can leave a pending error
	behind. src/internal/rpath.c walks __rpath entry by entry and
	calls its set_err() on each miss, and set_err() bumps the
	sequence counter src/dlfcn/dlfcn.c's dlerror() uses to decide
	whether an error is outstanding. When an early entry misses and a
	later one hits, dlopen() returns a valid handle with the counter
	already bumped, and the next dlerror() reports a failure the
	caller's dlopen() did not experience.

	src/dlfcn/dlfcn.c's own comment states the opposite as its
	correctness argument -- "A successful dlopen()/dlsym()/dlclose()
	call never bumps that counter" -- which is what made this worth
	checking.

	Measured under Wine with this file's two-entry __rpath: a
	bare-name dlopen("ntdll.dll", RTLD_NOW) returns a handle, and
	dlerror() then returns
	"...\no-such-rpath-dir\ntdll.dll: DLL not found (NTSTATUS
	0xc0000135)". Not Wine-specific -- the failing entry is a
	directory that exists on neither Wine nor real Windows.

	This breaks the spec's own recommended disambiguation idiom for
	every subsequent dlsym() too, since the stale error is still
	outstanding when the caller clears-and-calls.

	Fix shape: snapshot the error sequence on entry to
	ntlibc_rpath_load() and restore it before returning a successful
	handle, or accumulate misses without recording an error and
	record one only once the whole search has failed. */
static void test_dlerror_null_after_successful_dlopen(void)
{
	void *h;

	(void)dlerror();
	h = dlopen("ntdll.dll", RTLD_NOW);	/* __rpath[0] misses, __rpath[1] hits */
	CHECK(h != NULL);
	CHECK(dlerror() == NULL);		/* reports __rpath[0]'s miss today */
	if (h) CHECK(dlclose(h) == 0);
}
#endif

#if NTLIBC_TEST(BUG, posix_dl_dlopen_null_global_symbol_set) /* BUG: dlopen.html DESCRIPTION -- "If file is a null pointer,
	dlopen() shall return a global symbol table handle for the
	currently running process image. This symbol table handle shall
	provide access to the symbols from an ordered set of executable
	object files consisting of the original program image file, any
	executable object files loaded at program start-up as specified
	by that process file (for example, shared libraries), and the set
	of executable object files loaded using dlopen() operations with
	the RTLD_GLOBAL flag." And, under RTLD_GLOBAL: "Load ordering is
	used in dlsym() operations upon the global symbol table handle."

	src/dlfcn/dlfcn.c returns the PEB's ImageBaseAddress for a NULL
	file, and dlsym() hands that straight to
	LdrGetProcedureAddress(), which answers only "does *this one
	module* export this name". The start-up-loaded modules and the
	RTLD_GLOBAL set are never searched.

	Measured under Wine: dlopen(NULL, RTLD_NOW|RTLD_GLOBAL) returns a
	handle, dlclose() on it succeeds, but
	dlsym(g, "RtlAllocateHeap") is NULL -- and ntdll.dll is
	unambiguously "loaded at program start-up" on both Wine and real
	NT.

	Classified BUG rather than N/A because the NT mechanism is not
	missing: PEB_LDR_DATA, InLoadOrderModuleList and
	LDR_DATA_TABLE_ENTRY are already declared in src/internal/nt.h,
	and walking InLoadOrderModuleList trying LdrGetProcedureAddress
	on each DllBase is precisely POSIX's load-order search over the
	global handle. src/dlfcn/dlfcn.c's comments discuss only the
	narrower point that a -nostdlib tcc EXE has an empty export
	directory, and never address the "ordered set" requirement at
	all.

	Any such walk should stay to InLoadOrderLinks and DllBase: the
	layout of LDR_DATA_TABLE_ENTRY past DllBase has historically
	diverged between Wine and real NT. */
static void test_dlopen_null_global_symbol_set(void)
{
	void *g = dlopen(NULL, RTLD_NOW);

	CHECK(g != NULL);
	/* ntdll.dll is loaded at program start-up in every NT process,
	 * so its exports are part of the ordered set this handle must
	 * provide access to. */
	CHECK(dlsym(g, "RtlAllocateHeap") != NULL);
	if (g) CHECK(dlclose(g) == 0);
}
#endif

#if NTLIBC_TEST(BUG, posix_dl_dlopen_relative_pathname_uses_cwd) /* BUG (knowing deviation, recorded rather than changed):
	dlopen.html DESCRIPTION -- "If file contains a <slash>
	character, the file argument is used as the pathname for the
	file. Otherwise, file is used in an implementation-defined manner
	to yield a pathname."

	The implementation-defined latitude covers only the *no-slash*
	case. A relative pathname that does contain a slash must be used
	as the pathname, i.e. resolved against the current working
	directory like any other. src/internal/rpath.c instead joins it
	onto the image directory, and include/ntlibc/rpath.h says so
	explicitly -- "never against the current working directory".

	The security rationale for that (a CWD-relative load is
	attacker-controllable in a way an $ORIGIN-relative one is not) is
	sound and this fence is not an argument for changing the
	behaviour. What it records is that the clause the surrounding
	comments cite does not license the deviation: the bare-name half
	of ntlibc's policy *is* fully conforming, because that half is
	genuinely implementation-defined, and the slash-containing half
	is not. A knowing deviation, not a conformance claim.

	Left without a runnable assertion body on purpose: writing one
	would mean creating a DLL under the CWD and loading it by a
	relative path, which is exactly the operation the deviation
	exists to refuse. */
static void test_dlopen_relative_pathname_uses_cwd(void)
{
	/* would need: a DLL at "./subdir/x.dll" relative to the CWD,
	 * loaded as dlopen("subdir/x.dll", RTLD_NOW), which POSIX
	 * requires to resolve against the CWD and ntlibc resolves
	 * against the image directory instead. */
	CHECK(0);
}
#endif

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--spawn-fd-child")) {
		write(2, "child-fd2\n", 10);
		return 0;
	}

	test_dl_underlying_mechanism();
	test_dlopen_now_lazy();
	test_dlclose_refcounts();
	test_dlerror_consumed_once();
	test_dlfcn_header_constants();
	test_dlopen_null_returns_a_handle();
	test_dlsym_failure_through_dlfcn();
	test_dlclose_invalid_handle();
	test_dlerror_message_shape();
	test_dlopen_single_copy_different_pathnames();
	test_termios_isatty_prerequisite();
	test_spawn_fd_remap_via_existing_inheritance(argv[0]);
	test_spawn_usevfork_trivially_satisfied();

	if (fails) { printf("posix-dl: failures: %d\n", fails); return 1; }
	printf("posix-dl: all ok\n");
	return 0;
}
