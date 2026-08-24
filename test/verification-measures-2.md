<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Bug classes the first pass did not reach

A second evaluation of technical measures, written against the classes
[verification-measures.md](verification-measures.md) did not cover. That
document worked backwards from ten defects found by *people reading
output*. This one works backwards from a larger and differently-shaped
corpus: the tagged `#if 0` fences in `test/`, each one a confirmed,
reproduced, unfixed defect with a written description and a citation.

The nine measures ranked there are taken as given and not re-derived.
Everything below was measured on a fresh clone of `origin/main` at
`06f3203`, x86_64, clang 18.1.3 / gcc 13, tcc cross to `x86_64-win32`,
Wine for the PE legs. Numbers are quoted as measured.

## Contents

- [The fence corpus](#the-fence-corpus)
- [What the classification says](#what-the-classification-says)
- [Measures](#measures)
- [Rejected, with reasons](#rejected-with-reasons)
- [The oracle-classification design](#the-oracle-classification-design)
- [Ranking](#ranking)
- [Live findings](#live-findings)

## The fence corpus

`test/` fences work it cannot assert with `#if 0` and a tag. The tag
distinguishes three things, and the counts at `06f3203` are:

| Tag | Count | Means |
|-----|-------|-------|
| `BUG:` | **30** | a genuine spec violation, reproduced, unfixed |
| `UNIMPL:` | 43 | the function or flag does not exist yet |
| `N/A:` | 31 | the requirement is unobservable on NT, with the reason |
| | **104** | |

Only the 30 `BUG:` fences are defects. The other 74 are inventory: an
`UNIMPL:` fence is a written test waiting for an implementation, and an
`N/A:` fence is a written test waiting for a platform that will not
arrive. Neither is something a checker could have caught, because
neither is wrong.

(The brief for this document said 64. That is not the number in the
tree at `06f3203` under any grouping I can construct: 30 / 43 / 31, or
104 in total, or 61 for `BUG:`+`N/A:`. It may count fences on branches
not yet merged. The classification below is of the 30 that exist.)

### The classification

Classified by **what would have had to notice**, since that is the axis a
measure can be built on.

| Class | Count | Fences |
|-------|-------|--------|
| **F1** argument never validated; success reported for input the spec says *shall* fail | 6 | signal 1087, 1147, 1200; unistd 1041, 1242, 1455 |
| **F2** errno or out-parameter contract violated on a path that otherwise works | 4 | pwd 437; grp 421; dl 974; glob 1358 |
| **F3** a constant or a primitive's behaviour taken from the wrong reference | 3 | math 1807, 1852, 1898 |
| **F4** pattern-matching semantics: a plausible reading of a clause that is not the clause | 13 | glob 895, 968, 1004, 1026, 1056, 1226, 1271, 1311, 1870, 1901, 1942, 1964, 1989 |
| **F5** a documented capability absent or reduced to a stub | 2 | dl 1019, 1068 |
| **F6** unbounded resource consumption on adversarial input | 2 | glob 1821, 2328 |

Line numbers are in `test/posix-<name>.c` (`pwd` in `test/pwd.c`), all at
`06f3203`. `test/posix-glob.c` carries 16 of the 30 because it covers
four modules: `src/glob`, `src/fnmatch`, `src/regex` and `src/wordexp`.

**F1 — argument never validated (6).** `sighold`/`sigrelse` ignore
`sigaddset()`'s `-1` (`src/signal/signal.c:309-310`); `siginterrupt`
discards `sig` outright (`:305`); `tcgetpgrp`/`tcsetpgrp` discard `fd`
(`src/unistd/ttyname.c:23-24`); `unlinkat` masks undefined flag bits
instead of rejecting them (`src/unistd/unlink.c`); `confstr` falls an
unrecognised `name` through the empty-value path
(`src/unistd/sysconf.c`); `sigset` is a bare alias of `signal()` and
`<signal.h>` never defines `SIG_HOLD`. Every one is a *shall-fail*
clause, not a may-fail one.

**F2 — contract violated on a working path (4).** `getpwuid`/`getpwnam`
and `getgrgid`/`getgrnam` set `ERANGE`, which appears in their POSIX
ERRORS lists only for the `_r` forms. `dlerror()` returns non-NULL after
a *successful* `dlopen()`, because `src/internal/rpath.c` calls
`set_err()` on every rpath entry that misses and one of them missing
before a hit is the normal case. `wordexp()` leaves `we_wordc` unset on
every failure but `WRDE_NOSPACE`.

**F3 — the wrong reference (3).** `src/math/fenv.c` hardcodes an x87
control word of `0x037F`, which is musl's value for Linux x86-64; NT
starts a thread at `0x027F`. `fegetenv()` is a bare `FNSTENV`, and per
the Intel SDM that instruction masks every floating-point exception as a
side effect, so the getter changes what it read. `feholdexcept()`
inherits both and returns 0 unconditionally. This is the class the seven
ReactOS-versus-real-Windows errors belong to: the implementation is
faithful to its reference and the reference is wrong about the target.

**F4 — pattern-matching semantics (13).** `src/regex` 6, `src/wordexp`
4 (`src/glob`'s file holds them), `src/glob` 4, `src/fnmatch` 1. These
are the fences whose descriptions are longest, and the reason is
uniform: each is a clause that reads one way in isolation and another
way in context. `GLOB_MARK` is about what a pathname *is*, not about how
the pattern named it. Field splitting acts on the *result* of expansion,
not on the input text. `*` is ordinary as the first character of a BRE
"after an initial `^`, if any" — which means after the anchor, not after
the first atom. No tool finds these. A person reads the sentence twice.

**F5 — capability absent (2).** `dlopen(NULL)` does not return a global
symbol-table handle; a relative path *containing a slash* is joined to
the rpath rather than resolved against the working directory (recorded
as a knowing deviation).

**F6 — unbounded consumption (2).** `src/regex/regex.c`'s `run()`
recurses per `I_SPLIT`, a nullable repeat makes no input progress, and
the `MAX_STEPS` guard counts steps rather than depth — so `regexec()`
kills the process. `src/search/hsearch.c:45` computes `nel + nel/2 + 8`
in `size_t` with no overflow check.

## What the classification says

Three things, and they point in different directions.

**1. Only 8 of the 30 have a mechanical detector at all.** F1 (6) and
F6 (2). F2's four need a per-function table of the POSIX ERRORS list;
F3's three need a measurement on the target rather than a check; F4's
thirteen and F5's two need somebody to read the specification. **Twenty-
two of thirty are not reachable by any tool.** That is the single most
important number in this document, and it argues against generic
measures: the marginal value here is in the two classes that *are*
mechanical, and in making the reading cheaper for the rest.

**2. The corpus is not scattered — it has one dominant site.** Fifteen
of the thirty are in four modules: `src/regex` (6), `src/wordexp` (4),
`src/glob` (4), `src/fnmatch` (1). Add `src/search/hsearch.c` and it is
sixteen. These are the modules that take adversarial string input and
have the most internal state, and they are also the four for which
`fuzz/` has **no harness** — the eight that exist cover `strtod`,
`printf`, `scanf`, `utf`, `path`, `strptime`, `strtol` and `strftime`.

**3. The F1 shape has a cheap textual signature, and it is still live.**
`(void)<parameter>;` on a line that also returns success and never
touches `errno` has **21 hits** across `src/` at `06f3203`. Three of
F1's six defects are among them. The other eighteen are the same shape
and were never examined as such — seven of them are live shall-fail
violations, reproduced under Wine; see [Live findings](#live-findings).

Compare that trade with the one the first document rejected: its
`snprintf`-into-a-fixed-array grep had 88 hits and covered one
historical defect. This grep has 21 hits and covers three fenced
defects plus seven live ones.

## Measures

Named N1..N6 to avoid colliding with the first document's M1..M9. Every
"would have caught" claim below was demonstrated against the defect
named, not asserted: where a measure claims a fenced or live bug, the
check was run and the bug came out; where it claims a *latent*
fragility, the fragility was introduced by mutation and the suite was
confirmed green.

### N1: fuzz the four pattern-matching modules

**Catches: F6's `regex` crash (demonstrated), and it found a live
undefined-behaviour defect in `src/wordexp/arith.c` that no fence
records.**

Fifteen of the thirty fenced defects are in `src/regex`, `src/wordexp`,
`src/glob` and `src/fnmatch`. `fuzz/` has eight harnesses and none of
them is for any of those four. Everything needed already exists: the
`fuzz/Makefile` builds the instrumented library from `find src -name
'*.c'`, so a new harness is one file and one word in `HARNESSES`.

Prototyped and thrown away. Two harnesses, 36 and 27 lines.

*Result 1 — the fenced crash, found without an oracle.* A harness that
only calls `regcomp`/`regexec` under BRE, ERE and `REG_ICASE`, checking
nothing but that the process survives, reproduces
`test/posix-glob.c:1821` (unbounded `run()` recursion on a nullable
repeat) on **6 of 6 seeds**. Crashing inputs, decoded from libFuzzer's
Base64: `-**`, `(**`, `A**`, `\xb8*+`, `\xc6*+^`, `\xf3***]`. Each run
took 90 s wall, and that number needs splitting, because it is not
search time: a standalone driver that calls `regcomp("-**", 0)` and
`regexec()` directly takes **90.4 s**, essentially all of it inside
ASan's unwinder printing a quarter of a million identical stack frames.
The *search* found the bug between run #196 and run #2501 at roughly 19
execs/s, i.e. **10 to 130 seconds**. The nightly `tools/fuzz.sh` budget
is 300 s per harness.

*Result 2 — a live defect, previously unrecorded.* A harness calling
`fnmatch`, `glob` and `wordexp(..., WRDE_NOCMD)` found, in 120 s on the
second seed it was given:

```
src/wordexp/arith.c:217:23: runtime error: shift exponent -1 is negative
```

Minimised to one line: `wordexp("$((1>>-1))", &w, WRDE_NOCMD)`.
`src/wordexp/arith.c:216-217` are `case 'L': return cur << rhs;` and
`case 'R': return cur >> rhs;`, and the `/` and `%` cases three lines
above them *do* guard their operand (`if (rhs == 0) { fail(a,
WRDE_SYNTAX); ... }`). This is F1's shape -- an operand not validated --
in a module the fence corpus never reached, reachable from a public API
with caller-controlled input. See [Live findings](#live-findings).

**Cost.** Zero gate wall clock: fuzzing is not a gate stage, it is
`.github/workflows/fuzz.yml` on a nightly cron. Each harness adds 300 s
to a job that already runs eight of them, and about 30 lines. Building
the instrumented library is already paid for.

**Honest limit.** A fuzzer without an oracle sees crashes, hangs, leaks
and UBSan traps. It sees **none** of F4's thirteen semantic fences,
because a wrong answer is still an answer. The obvious fix -- a
differential oracle against glibc -- was prototyped and is rejected
below on measurement.

### N2: assert that a function can fail

**Catches: the shape behind F1 (6 fences), the `newlocale` /
`posix_fadvise` / `posix_fallocate` defects found by the clause audit,
N1's `wordexp` finding, and it names `symlinkat` -- the live
heap-buffer-overflow -- without being told about it.**

The first document's M2 asks which implemented functions no test
*references*. The finer and more productive question is which functions
no test ever makes **fail**. Both sets are already computable from files
in the tree:

- a function's own body assigns `errno` or calls `__set_errno_status`
  -- so it has a failure path;
- some `test/*.c` line names that function and asserts an `errno ==
  E...` on the same line.

Measured on `06f3203` by a 25-line script, **0.93 s** end to end:

```
public functions whose own body sets errno        124
  ... asserted to fail by at least one test         58
  ... never asserted to fail                        66
```

The 66:

> `accept`, `aligned_alloc`, `bind`, `calloc`, `clock_gettime`,
> `clock_nanosleep`, `connect`, `copy_file_range`, `execvpe`,
> `faccessat`, `fchmodat`, `fgets`, `fileno`, `fmemopen`, `fopen`,
> `fork`, `fstatat`, `fstatvfs`, `getdelim`, `getdents`, `getgrgid`,
> `getgrnam`, `getpwnam`, `getpwuid`, `getsockopt`, `glob`, `gmtime_r`,
> `inet_addr`, `inet_ntop`, `inet_pton`, `listen`, `mbrtowc`,
> `mbsrtowcs`, `mbtowc`, `mkfifo`, `mkfifoat`, `mktemp`, `newlocale`,
> `nftw`, `open_memstream`, `pause`, `pipe2`, `pselect`, `realloc`,
> `reallocarray`, `recv`, `remove`, `renameat`, `scandir`, `send`,
> `setsockopt`, `setvbuf`, `shutdown`, `sigwait`, `socket`, `statvfs`,
> `stime`, `symlinkat`, `system`, `times`, `tmpfile`, `uname`,
> `wcrtomb`, `wcsrtombs`, `wctomb`, `wordexp`

The list is worth reading closely, because three defects found by three
independent routes are all on it and none of them was used to build it.
`newlocale` is the clause audit's `(void)mask; (void)base;` finding.
`wordexp` is N1's shift. `symlinkat` is the live ASan
heap-buffer-overflow at `src/unistd/link.c:186` -- and the reason that
overflow survived is visible in the list: `test/posix-unistd.c:928`
calls `symlink()` inside `if (symlink(...) == 0)`, so the *failure* of
symlink is never asserted and its *success* path is skipped everywhere
except the one build where it runs.

**Cost.** 0.93 s, once, in a stage that runs concurrently. ~25 lines
plus a committed baseline count so the number can only go down. Ship it
report-only, exactly as the first document proposes for M2.

**Honest limit, and it is a real one.** "Asserts *an* errno" is not
"asserts the *required* errno". This measure cannot tell you that
`unlinkat` is missing `[EINVAL]` specifically -- only that
`unlinkat`-with-a-failure is somewhere in the suite. Closing that gap
needs the required-errno table, which is evaluated and rejected below.
Also, the function-name recovery is textual: 297 of the 416 errno
assertions in `test/*.c` (71%) name the function on the same line, so
the remaining 29% would show as false gaps until annotated.

### N3: index a table by its constant, not by its position

**Catches: a latent fragility class, demonstrated by mutation. Catches
none of the thirty fences -- there is no fence here because nothing is
currently transposed.**

`src/ctype/wctype.c` returns `i+1` as an index into a positional
twelve-name `classes[]`; `src/ctype/iswctype.c` consumes it with a
hand-written `case 1:` .. `case 12:` ladder. The two lists are held in
step by a comment.

Demonstrated rather than argued. Two mutations, applied together to a
clean `06f3203`:

- `src/ctype/iswctype.c`: swap `case 2: return iswalpha(wc);` and
  `case 3: return iswblank(wc);`
- `src/string/strsignal.c`: transpose `"User defined signal 1"` and
  `"User defined signal 2"` in the positional `__sigmsgs[]`

Result: the tree builds clean and `make check` reports **46 passed, 0
failed, 2 unverified, 0 skipped**. After the mutation,
`iswctype(L'a', wctype("alpha"))` answers `iswblank()` and
`strsignal(SIGUSR1)` says "User defined signal 2", and nothing in the
suite notices. `test/posix-wchar.c:1046` only ever asserts `digit`;
`test/string.c:134-135` pins `SIGINT`, `SIGSEGV`, `SIGSYS` and `0`.

The check is a grep, and its selectivity is the surprise. A scan for
"a `switch` whose case labels are five or more consecutive integers"
over every `.c` in `src/` and `crt/` has **exactly one hit in the whole
tree**, and it is `src/ctype/iswctype.c`. A scan for string-literal
tables of five or more entries has seven hits, of which
`src/string/strerror.c`'s `__errmsgs` is *not* fragile -- it is written
`[EPERM] = "Operation not permitted"`, designated by the very macro it
must agree with, so a transposition is not expressible. `strsignal.c`'s
`__sigmsgs[]` is positional and is.

So the rule is not "add a lint". It is **a table indexed by a constant
defined elsewhere must be written with designated initialisers keyed by
that constant**, and the grep is there to find the places that are not.
Both the safe exemplar (`strerror.c`) and the general form
(`src/regex/regex.c:146`, a single table of `{ name, fn }` pairs
covering the same twelve class names) are already in this tree. The best
version of this measure deletes the second list rather than checking it,
which is the same "make it unrepresentable" preference the first
document applies to `__afd_ioctl`.

**Cost.** 0.04 s for the scan; the conversions are per-site and small.
Two sites today.

**Honest limit.** The grep is fitted to the two instances it was built
from. It finds positional integer ladders and positional string tables;
it will not find two lists kept in step by, say, matching struct field
order, or a bitmask whose meaning is defined in a header and decoded by
arithmetic. It is cheap enough that being narrow is acceptable, but it
is narrow.

### N4: a committed expected-result set per leg

**Catches: the class of "an assertion that is true only in the
configuration its author ran". Two of the two failures on `main`'s
`asan` leg today are in this class or adjacent to it.**

The `nextafterl`/`nexttowardl` case is the pattern. `make asan` on
`06f3203` reports:

```
FAIL test/posix-math.c:1427: nextafterl(1.0L, 2.0L) > 1.0L && nextafterl(1.0L, 0.0L) < 1.0L
FAIL test/posix-math.c:1443: nexttowardl(1.0L, 2.0L) > 1.0L && nexttowardl(1.0L, 0.0L) < 1.0L
```

and `make check` under Wine reports 46/46. Both are right. The native
ASan build has an 80-bit `long double` (measured: `sizeof 16`,
`LDBL_MANT_DIG 64`); the shipped `--host=x86_64-win32` tcc build has a
64-bit one, and `arch/x86_64/bits/float.h:21` already switches on
`__SIZEOF_LONG_DOUBLE__` to say so. The *header* handles the axis
correctly. The *assertions* at `test/posix-math.c:1423-1443` carry no
guard at all, while `test/math.c:19` and `test/posix-limits.c:346`
define `TEST_LDBL_EXTENDED` from the identical test. Four `#if`s in
`test/` mention `__SIZEOF_LONG_DOUBLE__`; none of them is in the file
that fails.

Nothing here needs a new instrument. What is missing is a place to say
"this test is expected to fail on this leg, for this reason", and a
check that an expectation which stops being true is an error. Concretely:
one committed file per leg listing test-and-line expected failures with
a reason, consulted by `tools/asan-build.sh`, `tools/runtests.sh` and
the CI PowerShell leg; an unlisted failure fails the leg, and a **listed
failure that passes also fails the leg**. The second half is what stops
the list becoming the graveyard that such lists usually become -- it is
the same self-evaluating idea the first document proposes in M8 for the
findings record, applied to the per-leg result set.

**Cost.** Zero wall clock. One file, three consumers, roughly 30 lines
each. The three consumers are the same three that already had to be
taught `rc=77` independently, so this belongs with the first document's
M5 rather than beside it.

**Honest limit.** This does not *find* the configuration-dependent
assertion. It stops one from being explained away, which is what
happened here: a red `asan` leg with a plausible one-line reason is
indistinguishable from a red `asan` leg with a real bug in it -- and on
`06f3203` that leg is red for *both* reasons at once, one
configuration-dependent assertion and one genuine heap-buffer-overflow,
reported side by side.
