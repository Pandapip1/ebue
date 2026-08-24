/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <locale.h>'s locale-object
 * API -- `newlocale`, `duplocale`, `freelocale`, `uselocale` -- the
 * four functions test/POSIX-GAP-ACCOUNTING.md lists under "Implemented,
 * not clause-audited".  (`setlocale`/`localeconv`, the other two names
 * in <locale.h>, were audited in priority 4 and are not touched here.)
 *
 * Spec pages consulted (https://pubs.opengroup.org/onlinepubs/9699919799/):
 *   functions/newlocale.html   functions/duplocale.html
 *   functions/freelocale.html  functions/uselocale.html
 *   basedefs/locale.h.html
 *
 * ==================== the implementation, in full =====================
 *
 * src/misc/locale.c is 80 lines and its whole locale-object half is:
 *
 *     struct __locale_struct { int dummy; };
 *     static struct __locale_struct __c_locale;
 *
 *     locale_t newlocale(int mask, const char *name, locale_t base)
 *     { (void)mask; (void)base;
 *       if (name && *name && strcmp(name,"C") && strcmp(name,"POSIX"))
 *               { errno = ENOENT; return 0; }
 *       return &__c_locale; }
 *     void     freelocale(locale_t l) { (void)l; }
 *     locale_t duplocale(locale_t l)  { (void)l; return &__c_locale; }
 *     locale_t uselocale(locale_t l)  { (void)l; return &__c_locale; }
 *
 * One immutable, stateless, file-scope object handed out for
 * everything.  ntlibc is C/POSIX-locale-only -- setlocale() in the same
 * file accepts no other name -- so that is not obviously wrong, and the
 * point of this audit is to separate the parts of it that are genuinely
 * *correct for such a libc* from the parts that would mislead a caller.
 * Those two verdicts come out differently for different functions, so
 * each is argued on its own below rather than the file being labelled
 * "stub" wholesale:
 *
 *   freelocale  N/A -- nothing is ever allocated, so there is no
 *               resource to release.  A no-op is the correct
 *               implementation, not a placeholder for one.
 *   duplocale   N/A -- the object is immutable and carries no
 *               per-object state, so aliasing it is unobservable by
 *               any means POSIX defines, and freelocale() on the
 *               "copy" cannot damage the original because freelocale()
 *               does nothing.
 *   newlocale   BUG -- a *shall fail* [EINVAL] argument check is
 *               simply absent.  Nothing about being C-locale-only
 *               excuses it; validating a bitmask needs no locale data.
 *   uselocale   BUG -- and the interesting one.  See the fence.
 *
 * Two BUGs fenced.  Both are `#if 0 / * BUG: ... * /` per this
 * project's convention: the assertions are real and runnable and
 * should pass the day each is fixed.
 */
#include <locale.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* --------------------------------------------------------------------
 * newlocale -- newlocale.html
 *
 * DESCRIPTION: "shall create a new locale object or modify an existing
 * one.  If the base argument is (locale_t)0, a new locale object shall
 * be created."  The three preset locale names "are defined for all
 * settings of category_mask": "POSIX", "C" (equivalent to "POSIX"),
 * and "" (an implementation-defined native environment).
 *
 * RETURN VALUE: "shall return a handle which the caller may use on
 * subsequent calls to duplocale(), freelocale(), and other functions
 * taking a locale_t argument.  Upon failure ... shall return
 * (locale_t)0 and set errno."
 * ------------------------------------------------------------------ */
static void test_newlocale_accepts_the_preset_names(void)
{
	locale_t a, b, c;

	/* "C" / "POSIX" / "" are defined for all settings of
	 * category_mask -- and ntlibc has no other locale, so all three
	 * name the same one. */
	a = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	b = newlocale(LC_ALL_MASK, "POSIX", (locale_t)0);
	c = newlocale(LC_ALL_MASK, "", (locale_t)0);
	CHECK(a != (locale_t)0);
	CHECK(b != (locale_t)0);
	CHECK(c != (locale_t)0);

	/* a single category mask, not just LC_ALL_MASK */
	CHECK(newlocale(LC_CTYPE_MASK, "C", (locale_t)0) != (locale_t)0);
	CHECK(newlocale(LC_NUMERIC_MASK | LC_TIME_MASK, "C", (locale_t)0) != (locale_t)0);
	CHECK(newlocale(0, "C", (locale_t)0) != (locale_t)0);

	/* "the caller may use [the handle] on subsequent calls to
	 * duplocale(), freelocale()" -- so the handle newlocale() returns
	 * must survive both */
	CHECK(duplocale(a) != (locale_t)0);
	freelocale(b);
}

static void test_newlocale_enoent(void)
{
	/* ERRORS, shall fail: "[ENOENT] For any of the categories in
	 * category_mask, the locale data is not available." */
	errno = 0;
	CHECK(newlocale(LC_ALL_MASK, "de_DE.UTF-8", (locale_t)0) == (locale_t)0);
	CHECK(errno == ENOENT);
	errno = 0;
	CHECK(newlocale(LC_CTYPE_MASK, "en_US", (locale_t)0) == (locale_t)0);
	CHECK(errno == ENOENT);
	/* the name is compared exactly: "c" is not "C" */
	errno = 0;
	CHECK(newlocale(LC_ALL_MASK, "c", (locale_t)0) == (locale_t)0);
	CHECK(errno == ENOENT);
}

static void test_newlocale_base_unchanged_on_failure(void)
{
	/* DESCRIPTION: "If the function call fails and the base argument is
	 * not (locale_t)0, the contents of base shall remain valid and
	 * unchanged."  Vacuously satisfiable here (base is never touched at
	 * all), but the observable half -- base is still a usable handle
	 * after a failed call -- is real and worth asserting. */
	locale_t base = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	CHECK(base != (locale_t)0);
	errno = 0;
	CHECK(newlocale(LC_ALL_MASK, "no_SUCH.locale", base) == (locale_t)0);
	CHECK(errno == ENOENT);
	/* base still valid: still duplicable, still freeable */
	CHECK(duplocale(base) != (locale_t)0);
	freelocale(base);
}

#if 0 /* BUG: newlocale() never validates category_mask.
       * newlocale.html ERRORS, *shall fail* (not "may fail"):
       *   "[EINVAL] The category_mask contains a bit that does not
       *    correspond to a valid category."
       * DESCRIPTION defines the valid bits as "a bitwise-inclusive OR
       * of the symbolic constants LC_CTYPE_MASK, LC_NUMERIC_MASK,
       * LC_TIME_MASK, LC_COLLATE_MASK, LC_MONETARY_MASK, and
       * LC_MESSAGES_MASK, or any of the implementation-defined mask
       * values defined in <locale.h>".  include/locale.h defines those
       * six plus LC_ALL_MASK (0x7fffffff), so bit 31 -- the sign bit,
       * outside LC_ALL_MASK -- corresponds to no category under any
       * reading.
       *
       * src/misc/locale.c's newlocale() opens with `(void)mask;` and
       * never looks at it again: every mask, including one that is
       * nothing but invalid bits, returns a valid handle and success.
       * Measured under Wine: newlocale(0x40000000|(1<<31), "C", 0)
       * returns a non-null handle with errno untouched.
       *
       * This is not excused by ntlibc being C-locale-only.  Validating
       * a bitmask requires no locale data of any kind, and the clause
       * is *shall fail*, which makes it part of the contract a caller
       * is entitled to rely on to detect its own bad argument.  It is
       * the same defect class as the six unimplemented shall-fail
       * argument checks the never-asserted-name sweep found (see
       * test/POSIX-GAP-ACCOUNTING.md, "Successor session"). */
static void test_newlocale_einval_on_invalid_mask(void)
{
	int bad = (int)(~(unsigned)LC_ALL_MASK);   /* bit 31 only */
	errno = 0;
	CHECK(newlocale(bad, "C", (locale_t)0) == (locale_t)0);
	CHECK(errno == EINVAL);
	/* and a mask that mixes valid and invalid bits still fails */
	errno = 0;
	CHECK(newlocale(LC_CTYPE_MASK | bad, "C", (locale_t)0) == (locale_t)0);
	CHECK(errno == EINVAL);
	/* EINVAL is checked before the locale name is looked up: with both
	 * an invalid mask and an unavailable name, EINVAL is the required
	 * answer only if the standard ordered them, which it does not --
	 * so this asserts only that *some* documented failure happens, not
	 * which. */
	errno = 0;
	CHECK(newlocale(bad, "no_SUCH.locale", (locale_t)0) == (locale_t)0);
	CHECK(errno == EINVAL || errno == ENOENT);
}
#endif

/* --------------------------------------------------------------------
 * duplocale -- duplocale.html
 *
 * DESCRIPTION: "shall create a duplicate copy of the locale object
 * referenced by the locobj argument.  If the locobj argument is
 * LC_GLOBAL_LOCALE, duplocale() shall create a new locale object
 * containing a copy of the global locale determined by the setlocale()
 * function."  RETURN VALUE: "shall return a handle for a new locale
 * object ... Otherwise ... (locale_t)0 and set errno."  ERRORS, shall
 * fail: "[ENOMEM]".
 *
 * VERDICT: N/A, and the mechanism is the reason, not the convenience.
 * ntlibc has exactly one locale object; it is a file-scope static with
 * no mutable state (`struct __locale_struct { int dummy; };`), and
 * every category of it is fixed at "C" because setlocale() accepts no
 * other name.  A "duplicate copy" of an immutable, stateless object is
 * indistinguishable from the object itself by every means POSIX
 * defines: there is no field a caller can read, no field a caller can
 * change on one copy and observe on the other, and freeing the copy
 * cannot damage the original because freelocale() releases nothing
 * (see below).  POSIX never promises that two handles compare unequal,
 * so returning the same pointer is not a violation -- it is the same
 * object, correctly.
 *
 * What *is* testable, and asserted below: the returned handle is
 * non-null and is usable everywhere a newlocale() handle is, and the
 * LC_GLOBAL_LOCALE case is handled rather than crashing -- correct
 * here for a second reason, that the global locale is unconditionally
 * "C" so "a copy of the global locale" and "the C locale" are the same
 * object.
 * ------------------------------------------------------------------ */
static void test_duplocale(void)
{
	locale_t base = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	locale_t dup;

	CHECK(base != (locale_t)0);
	dup = duplocale(base);
	/* RETURN VALUE: "shall return a handle for a new locale object" */
	CHECK(dup != (locale_t)0);
	/* "the caller may use [it] on subsequent calls to duplocale(),
	 * freelocale(), and other functions taking a locale_t argument" */
	CHECK(duplocale(dup) != (locale_t)0);
	freelocale(dup);

	/* "If the locobj argument is LC_GLOBAL_LOCALE, duplocale() shall
	 * create a new locale object containing a copy of the global
	 * locale determined by the setlocale() function." */
	CHECK(duplocale(LC_GLOBAL_LOCALE) != (locale_t)0);
	/* and the global locale really is "C", so the copy is the C locale */
	CHECK(!strcmp(setlocale(LC_ALL, (char *)0), "C"));
}

/* --------------------------------------------------------------------
 * freelocale -- freelocale.html
 *
 * DESCRIPTION: "shall cause the resources allocated for a locale object
 * returned by a call to the newlocale() or duplocale() functions to be
 * released."  RETURN VALUE: "None."  ERRORS: "None."  "Any use of a
 * locale object that has been freed results in undefined behavior."
 *
 * VERDICT: N/A, by mechanism.  No resource is ever allocated for a
 * locale object here -- newlocale() and duplocale() both return the
 * address of one file-scope static -- so there is nothing for
 * freelocale() to release and a no-op is the complete, correct
 * implementation of the clause rather than a placeholder for one.  It
 * has no return value and no errors to get wrong, which leaves nothing
 * else on the page to assert.
 *
 * The "any use after freeing is undefined" sentence is deliberately not
 * exercised: it makes the behaviour undefined, so there is nothing to
 * require.  (It is also the sentence that makes duplocale()'s aliasing
 * harmless here rather than dangerous: freeing the "copy" is a no-op,
 * so the original cannot be damaged by it.)
 * ------------------------------------------------------------------ */
static void test_freelocale(void)
{
	locale_t a = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	CHECK(a != (locale_t)0);
	freelocale(a);              /* "RETURN VALUE: None." */
	CHECK(errno == errno);      /* "ERRORS: None." -- nothing to check */
	/* a fresh object after the free is still obtainable and usable */
	CHECK(newlocale(LC_ALL_MASK, "C", (locale_t)0) != (locale_t)0);
	freelocale(duplocale(LC_GLOBAL_LOCALE));
}

/* --------------------------------------------------------------------
 * uselocale -- uselocale.html
 *
 * DESCRIPTION: "shall set or query the current locale for the calling
 * thread ... If the newloc argument is (locale_t)0, the current locale
 * shall not be changed; this value can be used to query the current
 * locale setting.  If the newloc argument is LC_GLOBAL_LOCALE, any
 * thread-local locale for the calling thread shall be uninstalled; the
 * thread shall again use the global locale."
 * ------------------------------------------------------------------ */
static void test_uselocale_query_does_not_change(void)
{
	/* "If the newloc argument is (locale_t)0, the current locale shall
	 * not be changed" -- observable through setlocale(), which reports
	 * the global locale and must still say "C" afterwards. */
	locale_t q1 = uselocale((locale_t)0);
	locale_t q2 = uselocale((locale_t)0);
	CHECK(q1 == q2);
	CHECK(!strcmp(setlocale(LC_ALL, (char *)0), "C"));
}

static void test_uselocale_install_and_uninstall(void)
{
	locale_t loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	locale_t prev;

	CHECK(loc != (locale_t)0);
	/* "Otherwise, the locale represented by newloc shall be installed
	 * as a thread-local locale to be used as the current locale for
	 * the calling thread." */
	prev = uselocale(loc);
	CHECK(prev != (locale_t)0);   /* a handle, or LC_GLOBAL_LOCALE */
	/* the installed locale is what a query now reports */
	CHECK(uselocale((locale_t)0) == loc);

	/* "If the newloc argument is LC_GLOBAL_LOCALE, any thread-local
	 * locale for the calling thread shall be uninstalled; the thread
	 * shall again use the global locale, and changes to the global
	 * locale shall affect the thread." */
	CHECK(uselocale(LC_GLOBAL_LOCALE) != (locale_t)0);
	CHECK(!strcmp(setlocale(LC_ALL, (char *)0), "C"));
}

#if 0 /* BUG: uselocale() cannot report "no thread-local locale is in
       * use", so the one question the interface exists to answer
       * cannot be answered.
       *
       * uselocale.html RETURN VALUE, verbatim: "Upon successful
       * completion, the uselocale() function shall return a handle for
       * the thread-local locale that was in use as the current locale
       * for the calling thread on entry to the function, or
       * LC_GLOBAL_LOCALE if no thread-local locale was in use."
       *
       * ntlibc never installs a thread-local locale -- uselocale() is
       * `{ (void)l; return &__c_locale; }` and stores nothing -- so
       * "no thread-local locale was in use" is true on entry to every
       * call ever made, and LC_GLOBAL_LOCALE is therefore the required
       * return value of every call.  It returns &__c_locale instead.
       * Measured under Wine: uselocale((locale_t)0) returns 0x41d7c8
       * while LC_GLOBAL_LOCALE is (locale_t)-1.
       *
       * Why this is a BUG and freelocale()'s no-op is not: the failure
       * here is not "an unused constant is wrong".  `uselocale(0) ==
       * LC_GLOBAL_LOCALE` is *the documented way for a program to ask
       * whether it is on the global locale*, and on this
       * implementation that question always answers "no" when the
       * truth is always "yes".  The standard save/restore idiom
       *
       *     locale_t old = uselocale(my_locale);
       *     ... ;
       *     uselocale(old);
       *
       * therefore cannot distinguish "I was on the global locale, put
       * me back on it" from "I was on some locale object, put me back
       * on that" -- it silently does the wrong one of the two rather
       * than failing.  That is a caller being misled, not a resource
       * that did not need freeing, which is exactly the line this
       * audit draws between BUG and N/A for a C-locale-only libc.
       *
       * A correct fix is small but is not "return LC_GLOBAL_LOCALE
       * unconditionally": once uselocale(loc) has been called, a
       * thread-local locale *is* in use and a subsequent query must
       * report it (which the live test_uselocale_install_and_uninstall
       * above already asserts).  What is needed is one word of state --
       *
       *     static locale_t current = LC_GLOBAL_LOCALE;
       *     locale_t uselocale(locale_t l) {
       *             locale_t prev = current;
       *             if (l) current = l;
       *             return prev;
       *     }
       *
       * -- which satisfies both this fenced test and the live one.
       * Verified by applying exactly that to src/misc/locale.c and
       * un-fencing this block: it passes, and nothing else in the file
       * regresses. */
static void test_uselocale_reports_lc_global_locale(void)
{
	locale_t loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);

	/* nothing has ever been installed, so the query must say so */
	CHECK(uselocale((locale_t)0) == LC_GLOBAL_LOCALE);
	/* and so must the value handed back by the first install */
	CHECK(uselocale(loc) == LC_GLOBAL_LOCALE);
	/* after uninstalling, the query says global again */
	uselocale(LC_GLOBAL_LOCALE);
	CHECK(uselocale((locale_t)0) == LC_GLOBAL_LOCALE);
}
#endif

/* uselocale.html ERRORS: "The uselocale() function *may* fail if:
 * [EINVAL] newloc is not a valid locale object and is not (locale_t)0."
 * *May* fail, not shall -- so not implementing it is conforming, and
 * nothing here asserts it either way.  Recorded so a successor does not
 * mistake its absence for the shall-fail gap in newlocale() above.
 *
 * Likewise newlocale.html's *may fail* "[EINVAL] The locale argument is
 * not a valid string pointer" (ntlibc accepts a null `name` and treats
 * it as "C") and both pages' shall-fail [ENOMEM], which needs a real
 * allocation failure and has no injection hook here. */

int main(void)
{
	test_newlocale_accepts_the_preset_names();
	test_newlocale_enoent();
	test_newlocale_base_unchanged_on_failure();
#if 0 /* BUG: see the fence above test_newlocale_einval_on_invalid_mask */
	test_newlocale_einval_on_invalid_mask();
#endif
	test_duplocale();
	test_freelocale();
	test_uselocale_query_does_not_change();
	test_uselocale_install_and_uninstall();
#if 0 /* BUG: see the fence above test_uselocale_reports_lc_global_locale */
	test_uselocale_reports_lc_global_locale();
#endif

	if (fails) { printf("posix-locale: failures: %d\n", fails); return 1; }
	printf("posix-locale: all ok\n");
	return 0;
}
