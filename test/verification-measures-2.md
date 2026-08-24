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
