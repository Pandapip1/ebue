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
- [Backlog rather than gate](#backlog-rather-than-gate)

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

### N5: pin the reference, and probe the target

**Catches: F3 (3 fences) and the class the seven ReactOS-versus-real-
Windows errors belong to. Catches nothing else.**

F3's three fences and the seven ReactOS errors are the same defect: the
code is faithful to a reference and the reference is wrong about the
target. `src/math/fenv.c`'s `0x037F` is musl's correct value *for
Linux*; NT starts a thread at `0x027F`. ReactOS's sub-page
`SectionAlignment`, its 24-byte `AFD_OPEN_PACKET`, its 12-byte AFD
create packet, its `AFD_POLL_INFO` and `AFD_CONNECT_INFO` layouts, its
`FileBasicInformation` access rule and its `Information` truncation were
each correct about ReactOS.

Measured on `06f3203`: `src/` cites a third-party reference **217
times** -- Wine 84, ReactOS 61, phnt 27, mingw 21, musl 20, the Intel
SDM 4. Exactly **one** of those citations sits next to anything
resembling a revision. Twelve places claim a measurement on real NT.

Two things follow, and they are separable.

**The cheap half, which is a lint.** A citation of a reference
implementation must name a pinned revision, because a citation of a
branch is a citation of whatever that branch says today. This is not
hypothetical bookkeeping: this project has three multi-writer reference
trees under `/tmp/claude/repos/`, one of which carries six unpushed
local commits, so "what ReactOS does" has already been quoted back from
a local patch at least once. The check is a grep over comment blocks
mentioning a known reference name for an accompanying `<= 40 hex`
revision, and the fix is `git show <sha>:path` rather than a branch
name. Cost: one grep; 217 sites to annotate once, which is real work but
mechanical and can be done incrementally against a baseline count.

**The expensive half, which is the one that actually catches the bug.**
A constant taken from a reference must have a **probe** that measures the
same quantity on the target, and the probe must run on the leg that has
a real target -- `.github/workflows/ci.yml`'s Server 2025 job. The
`fenv` fence shows both the shape and the timing: it says the NT value
was "verified with a bare `-nostdlib` PE", which means the probe was
written *after* the defect was found. As a standing CI check the same
probe is the guard.

**Cost.** The lint: 0.05 s and a 217-line annotation backlog. The
probes: one small program per quantity, on the real-Windows leg only,
seconds each. There is no local substitute -- under Wine every one of
these probes reports Wine's answer, which is the whole problem.

**Honest limit, stated plainly.** The probe half cannot be written
generically. Somebody has to decide, per constant, what measures it on
NT. It is a convention with a lint attached, not a check, and it will
only ever cover the constants somebody thought to probe. Its value is
that it moves the measurement from *after* the defect to *before* it.

### N6: compare what arrived, not that something arrived

**Catches: the shape shared by three defects. Mostly already covered,
and the remainder is a review rule.**

Three defects in this project's recent history are one shape: a
transport silently dropping data the producer genuinely wrote.
`IOCTL_AFD_SELECT`'s `METHOD_BUFFERED` reply overwrote the request;
ReactOS truncated `Information`; `objcopy` carried an ELF relocation
*number* into a COFF record so `R_X86_64_PC32` became `R_AMD64_DIR32`,
turning PC-relative into absolute with no diagnostic. The `objcopy` case
is the clearest, because the verification of it failed the same way the
tool did: five relocations in, five relocations out, reported as
"measured, works". Two of the five were right by coincidence.

The rule -- *compare types, not counts* -- is correct and it is not a
script. I looked for a mechanical form and the honest answer is that
most of it is already taken. The first document's M4 (bound every read
of an out-parameter by the returned count) and M9 (assert the PE
header's fields, not the file's size) are the two places where this
shape *has* a technical form, and both are proposed there.

What is left that is mechanical and is not already proposed: **54 NT
calls in `src/*.c` take an out-buffer** (`NtDeviceIoControlFile`,
`NtFsControlFile`, `NtQueryInformationFile`,
`NtQueryVolumeInformationFile`, `NtQueryDirectoryFile`, `NtReadFile`),
across fifteen files, and twenty files read an `Information` field
somewhere. Reconciling those two sets per call site -- rather than per
file, which is all a grep gives -- is the check, and it is
`clang-query`-shaped rather than `grep`-shaped. At three AFD sites the
first document already proposes doing it by hand.

**Verdict: fold into M4 rather than adopting separately.** Recorded here
because the *rejection* is the useful part: "check what arrives" is a
review rule wearing a script's clothes everywhere except the two places
the first document already names.

## Rejected, with reasons

### A differential regex oracle against glibc -- rejected on measurement

This is the obvious answer to F4's thirteen semantic fences, and it is
the one candidate in this document I most wanted to work. `fuzz/
host_oracle.c` already reaches glibc through `dlopen("libc.so.6")`
precisely so that ntlibc's own hidden symbols cannot win the link, so
adding `regcomp`/`regexec`/`regfree` to it is fifteen lines.

Prototyped and measured. The harness compiles the same pattern with
ntlibc and glibc under BRE, ERE and `REG_ICASE`, and compares
accept/reject, match/no-match and the extent of match 0.

*Accept/reject, 120 s, one seed:* **59 distinct mismatching patterns**,
and every one of them falls into two buckets:

- `ntlibc 0 / glibc 13` -- ntlibc accepts a repeat applied to a
  zero-width assertion (`$+`, `$*`), glibc answers `REG_BADRPT`;
- `ntlibc 8 / glibc 0` -- ntlibc rejects an unmatched `)` where glibc
  accepts it as an ordinary character.

The first bucket is arguably real and is a sibling of the fenced `^*a`
defect. The second is POSIX-undefined behaviour where glibc has picked a
lenient reading. Neither is a *finding* in the sense a gate can act on.

*Match/extent, with accept/reject suppressed, 120 s:* **956 + 401
match/no-match mismatches** and roughly 150 extent mismatches, dominated
by BRE `\|` -- a GNU alternation extension glibc implements and POSIX
does not have. The single largest extent-mismatch class,
`[^Ao].*\xc3?` reporting 11 or 12 where glibc reports 3, is
leftmost-first versus leftmost-longest: fence
`test/posix-glob.c:1989`, correctly identified, and buried under a
thousand results that are not defects.

**Rejected.** A differential oracle is only usable when the two
implementations agree on what they are implementing, and here they do
not: glibc's default regex is a POSIX superset with GNU extensions, and
POSIX leaves several of the disputed cases undefined. Making it usable
means writing down the equivalence relation -- which patterns the two
are required to agree on -- and that document is the specification
reading that F4 needs anyway. The oracle would then only confirm what
writing it taught you. **A per-clause table, not an oracle**, is the
answer to F4, and that is what `test/POSIX-COVERAGE.md` already is.

Note the asymmetry with N1: the *same* harness, with the oracle removed
and only ASan and UBSan watching, produced two real defects and zero
false positives. Fuzz the four modules; do not point them at glibc.

### A required-errno table scraped from POSIX -- rejected on cost, for now

This is the strongest candidate against the dominant defect shape, and
it is the only one I am rejecting reluctantly.

The consuming half is cheap and already measured: `test/*.c` contains
**416** `errno == E...` assertions over **31** distinct codes, of which
**297 (71%)** name the function on the same line, so "which errnos does
the suite assert for this function" is a two-second grep.

The producing half is the problem. There is no machine-readable source
for "the errnos POSIX requires this function to produce, and which of
them are *shall*-fail". I checked the obvious in-tree substitute:
`test/POSIX-COVERAGE.md` has 585 table rows, of which only **45** name
an `[Exxx]` code in brackets and **20** distinct codes appear at all. It
is a clause ledger, not an ERRORS database, and building the latter
means scraping the ERRORS section of several hundred Open Group pages
and hand-classifying shall-versus-may -- with the classification
re-checked whenever a page is revised, since a wrong entry produces a
confident false finding, which is worse than no finding.

**Rejected as a gate; recommended as a bounded piece of work.** The
right size is not "all of POSIX". It is the 66 functions
[N2](#n2-assert-that-a-function-can-fail) already names. Sixty-six
ERRORS sections is an afternoon, the output is a table small enough to
review, and every row of it is a test somebody should write. Do that
first; decide about the general case afterwards, with a hit rate to
argue from.

### Property-based testing of the pattern-matching modules -- rejected

The natural answer to F4 that is not a differential oracle: assert
algebraic properties instead of results -- `fnmatch(p, s)` agrees with
`glob` on a directory containing exactly `s`; `regexec` on `a|b` matches
iff `regexec` on `a` or on `b` matches; `wordexp` of a fully-quoted word
yields exactly one field. I could not construct a property that
discriminates any of the thirteen F4 fences without restating the fence.
Leftmost-longest is not a property of the answer, it is the definition
of the answer. `GLOB_MARK`'s trailing slash is not derivable from
anything else `glob` does. **No measurement supports this one, and that
is why it is rejected**: I could not find the property, not that I
measured it and it was slow.

### A "device-free tests must be byte-identical across legs" check -- rejected, see below

Evaluated at length in the next section, because the drafted design is
worth more than a one-line rejection.

## The oracle-classification design

The problem is real and the near-miss was expensive: a device-free
structural test and a device-touching test both print `PASS`, only one
of them is an oracle, and a correct Wine patch was nearly retracted
because a CI log could not tell them apart.

The drafted design has three parts: every test declares **device-free**
or **device-touching**; a lint checks the declaration against the
source; and -- the strong check -- device-free tests must produce
byte-identical output across the Wine legs and the Server 2025 leg. The
draft already identifies two limits: it cannot validate
`device-touching`, and raw stdout is not byte-identical across legs, so
normalisation is needed first.

I measured both halves. **The verdict is that the normalisation worry is
misplaced, that the byte-identity check should not be built for a
different and worse reason, and that the lint should be built but not as
a grep.**

### Is normalisation tractable? Yes -- and that is the problem

Measured directly. Every non-`*-win` test executable was run twice under
Wine, once from `/tmp/.../aaa` and once from a working directory with a
37-character name, and the outputs compared byte for byte:

```
identical 48   differ 0   empty output 0
```

Not one byte of difference. There are no pointer values, no handle
numbers and no temp paths in the output, because the tests do not print
any: **31 of the 48 print a single line**, and that line is
`<name>: all tests passed`. The entire vocabulary of the suite's output,
with digits collapsed, is about thirty distinct line shapes.

So normalisation is not where the design dies. Nothing needs
normalising. But the same measurement kills the check, for the reason
underneath it: **a passing device-free test and a passing
device-touching test emit the same bytes, because the bytes are
`all tests passed`.** Byte-identity across legs is therefore satisfied
by any test that passes on both legs, which is the normal case for the
whole suite. The check does not validate the `device-free` label; it
merely fails to contradict it. That is exactly the objection the draft
already accepts for the `device-touching` direction, and it applies with
equal force to the `device-free` one.

Making identity informative would mean making the tests *print what they
observed* rather than whether they agreed with it -- converting an
assertion suite into a golden-output suite. That is a large change in
the wrong direction for a project whose tests are written to clause
citations, and golden outputs are the artefact people regenerate when
they go red.

There is one genuine signal in the output, and it is the opposite of the
one the design looks for. Eight of the 48 tests print an environment-
conditional `note:` line -- `note: /dev/tty not openable here`,
`note: pid 4 is not a protected process here (Wine has no "System"
process)`, `note: symlink() not supported here (errno 38)`,
`note: flock(...) failed ... under concurrent Wine load`. A test that
can print such a line is *by construction* not environment-invariant.
That is a cheap, real check -- **a test declared device-free must not
contain an environment-conditional branch** -- and it needs one leg, not
two.

### Is the lint tractable? Not as a grep

I built the obvious version: classify a test as device-touching if its
source calls any of a 40-name list (`open`, `socket`, `stat`, `glob`,
`spawn`, `fork`, `NtCreateFile`, ...). Measured on `06f3203`: **19
device-free, 30 device-touching**, in 0.04 s.

Then I read the 19. Three are wrong, all in the dangerous direction:
`test/rpath.c` and `test/delayall.c` reach the NT loader through
`LdrLoadDll`/`LdrGetProcedureAddress`, and
`test/spawn-cmdline-manyargs.c` creates processes through `__spawn`.
A 40-name list misses every device reached by a name not on it, and
`src/` has plenty (`LdrLoadDll`, `RtlCreateUserProcess`,
`NtCreateUserProcess`, `__afd_ioctl`). A 16% false-`device-free` rate on
the first attempt is not a floor that will improve much, because the
failure mode is open-ended.

**The check that does work is a link, not a grep**, and the machinery is
already in the tree. `fuzz/ntstubs.c` exists precisely to stand in for
NT in a native build, and `tools/asan-build.sh` already links every test
against it. A test declared `device-free` can be linked against a
variant stub set in which `NtCreateFile`, `NtDeviceIoControlFile`,
`NtFsControlFile`, `NtOpenFile`, `Ldr*` and `Rtl*CreateUserProcess` all
`abort()`, and required to pass. That is decidable, complete, and
impossible to satisfy by accident: a test that touches a device dies,
and one that does not cannot. It costs one extra link and run per
declared-device-free test -- for the 16 that survive the read above,
seconds -- and it is a stage that runs concurrently.

### Verdict

| Part | Verdict |
|------|---------|
| the declaration | **build it.** It is the thing that was missing when the Wine patch was nearly retracted, and it costs one comment per test |
| the lint, as a grep | **do not build.** 16% false-`device-free` on 19 files, failing open |
| the lint, as an aborting-stub link | **build it.** Decidable, complete, reuses `fuzz/ntstubs.c`, seconds |
| byte-identity across legs | **do not build.** Normalisation is unnecessary -- 48/48 already identical across working directories -- and that is why the check carries no information: `all tests passed` is identical whether or not the test is an oracle |
| no environment-conditional branch in a device-free test | **build it**, as the cheap companion to the declaration. 8 of 48 tests print such a line today |

And the draft's own warning is the decisive argument against the
byte-identity half: *a check people disable is worse than no check*. A
check whose green result carries no information is the same thing one
step earlier -- nobody disables it, and nobody learns anything from it
either.

## Ranking

By expected defects caught per unit of cost, with what each misses.

| Rank | Measure | Cost | Would have caught | Would **not** catch |
|------|---------|------|-------------------|---------------------|
| 1 | **N2** functions never asserted to fail | 0.93 s, ~25 lines | the F1 shape (6 fences), `newlocale`, `posix_fadvise`, `posix_fallocate`; names `symlinkat` and `wordexp` unprompted | *which* errno is missing -- only that none is asserted |
| 2 | **N1** fuzz regex/glob/fnmatch/wordexp | 0 gate s; 300 s nightly per harness; ~30 lines each | F6's `regexec` crash (6/6 seeds); found a live `arith.c` shift UB in 120 s | all 13 F4 semantic fences -- a wrong answer is still an answer |
| 3 | **N3** designated initialisers for constant-indexed tables | 0.04 s scan; 2 sites | a demonstrated latent transposition in `iswctype`/`wctype` and `strsignal` | any pairing not shaped as an integer ladder or a positional string table |
| 4 | oracle declaration + aborting-stub link | seconds, concurrent | the device-free/device-touching confusion that nearly retracted a Wine patch | nothing about correctness; it labels oracles, it does not check them |
| 5 | **N4** committed expected-result set per leg | 0 s, ~30 lines × 3 | the `nextafterl` configuration-dependent assertion being explained away | it does not *find* such an assertion |
| 6 | **N5** pin the reference (lint half) | 0.05 s + 217 annotations | nothing directly; makes F3 and the ReactOS class diagnosable | the wrong value itself |
| 7 | **N5** probe the target (probe half) | per-constant, Server 2025 leg only | F3's `0x037F`, and the ReactOS layout errors | only the constants somebody thought to probe |
| -- | 66 bounded ERRORS sections | an afternoon | the specific missing errnos behind N2's 66 | it is work, not a check |
| -- | **N6** compare what arrived | -- | folds into the first document's M4 and M9 |  |
| -- | differential regex oracle, POSIX-wide ERRORS scrape, property-based F4 testing, byte-identity across legs, the grep-shaped device lint | -- | rejected above, each on a measurement |

**If only one is adopted, adopt N2.** It is 0.93 s and 25 lines, it
aims at the shape that both this classification and the clause audit
independently found to be dominant, and its output list contains three
defects found by three unrelated routes, none of which was used to
construct it. That is the closest thing to a validated detector in
either document.

**If two, add N1**, on the strength of the only measurement here that
produced a new defect rather than confirming an old one.

One cost note, inherited and worth repeating because it still holds.
`tools/gate.sh` runs stages concurrently, so the question for any
addition is "is it slower than `linkcheck`", not "how many seconds does
it add". Every measure ranked above is under a second. `linkcheck`
remains strictly serial, and parallelising it would still buy more
headroom than everything in both documents spends.

## Live findings

Verified against a fresh clone of `origin/main` at `06f3203`. None of
these are proposals, and none of them is fenced.

1. **`wordexp("$((1>>-1))")` is undefined behaviour.** UBSan, through
   the throwaway `fnmatch`/`glob`/`wordexp` harness, 120 s, second seed:
   `src/wordexp/arith.c:217:23: runtime error: shift exponent -1 is
   negative`. Minimised to a single call. `arith.c:216-217` are
   `case 'L': return cur << rhs;` and `case 'R': return cur >> rhs;`;
   the `/` and `%` cases three lines above do guard their operand. Both
   a negative and an over-wide count reach the shift. Reachable from a
   public API with caller-controlled input.

2. **Seven shall-fail clauses discarded in `src/unistd/ids.c`.** Built
   `--host=x86_64-win32` and run under Wine on `06f3203`:

   ```
   chown("/no/such/path/x", ...)      = 0   errno=0
   lchown("/no/such/path/x", ...)     = 0   errno=0
   fchown(-1, ...)                    = 0   errno=0
   fchownat(-1, "/no/such/path/x",...)= 0   errno=0
   getpgid(999999)                    = 1   errno=0
   getsid(999999)                     = 1   errno=0
   setpgid(999999, 999999)            = 0   errno=0
   access("/no/such/path/x", 0)       = -1  errno=2   <- control
   ```

   `chown.html` lists `[ENOENT]` as *shall* fail; `fchown.html` lists
   `[EBADF]`; `getpgid.html`, `getsid.html` and `setpgid.html` list
   `[ESRCH]`. `test/POSIX-COVERAGE.md:498` records this family as
   "covered for the returns; the *effects* N/A -- one user and one fixed
   session". The N/A argument is correct about the effect being
   unobservable and says nothing about the argument being unchecked.
   Every one of these sites is `(void)p; return 0;`, and every one is in
   [N2](#n2-assert-that-a-function-can-fail)'s list of functions no test
   makes fail. Not fenced -- fencing them is somebody else's commit, per
   the brief this document was written under.

3. **Two entries of `src/ctype/iswctype.c`'s case ladder and two of
   `src/string/strsignal.c`'s `__sigmsgs[]` can be transposed and the
   suite stays green** -- 46 passed, 0 failed. Nothing is transposed
   today; this is the negative control for
   [N3](#n3-index-a-table-by-its-constant-not-by-its-position), run and
   reverted.

4. **`test/posix-math.c:1423-1443` asserts `nextafterl`/`nexttowardl`
   with no `long double` width guard**, while `test/math.c:19` and
   `test/posix-limits.c:346` both define one from the identical
   `__SIZEOF_LONG_DOUBLE__` test. This is one of the two failures on
   `main`'s `asan` leg.

5. **`test/posix-unistd.c:928` calls `symlink()` inside
   `if (symlink(...) == 0)`**, so on every leg where `symlink()` is
   unsupported the whole readlink success path is skipped silently --
   `note: symlink() not supported here (errno 38)` in the Wine output --
   and the live heap-buffer-overflow at `src/unistd/link.c:186` is
   reachable only on the one leg where it does work. I did not
   root-cause the overflow; it is being worked elsewhere.

## Backlog rather than gate

Things found while measuring that are work, not checks.

- **`fuzz/` has no harness for the four modules holding half the fence
  corpus.** Two prototypes exist and were thrown away; the numbers are
  in [N1](#n1-fuzz-the-four-pattern-matching-modules).
- **66 ERRORS sections, read once, turned into 66 assertions.** The
  bounded version of the table this document rejects.
- **217 reference citations in `src/` with one revision between them.**
  Annotate incrementally against a baseline count.
- **`src/string/strsignal.c`'s `__sigmsgs[]` should be designated by
  `SIGxxx`,** the way `src/string/strerror.c`'s `__errmsgs[]` already is
  by `Exxx`, and `src/ctype/iswctype.c`'s ladder should be deleted in
  favour of `src/ctype/wctype.c`'s table -- `src/regex/regex.c:146`
  shows the shape.
- **ASan's stack-overflow report costs 90 s** on `src/regex`'s recursion
  because it unwinds every frame. Anything that bounds the recursion
  bounds the report too; until then a fuzz run that finds it spends most
  of its budget printing.
