<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# math.h coverage fragment

`math.h` had never been audited before this session (see
`test/POSIX-COVERAGE.md`'s "Also not yet reached at all" list). This
fragment is written standalone for the coordinator to merge; it does
not edit `test/POSIX-COVERAGE.md` itself.

Pre-existing coverage: `test/math.c` (broad sanity pass, one assertion
per function, not clause-cited -- not this agent's file to edit). New
clause-cited audit: `test/posix-math.c` (98 `CHECK()` assertions),
concentrating on what `test/math.c` does not pin down: the special-value
tables (±0, ±Inf, NaN) every RETURN VALUE section enumerates, sign of
zero (checked via `signbit()`, since `==` cannot distinguish it), and
the `math_errhandling`/errno contract.

Functions covered by `src/math/` and audited this session: `fabs`,
`floor`/`ceil`/`trunc`/`round`, `sqrt`, `fmod`, `frexp`/`ldexp`/`scalbn`,
`modf`, `copysign`, `exp`, `log`/`log2`/`log10`, `sin`/`cos`/`tan`/
`atan`/`atan2`, `pow`, `fmax`/`fmin`, `hypot`, `nan`, `fpclassify`/
`isnan`/`isinf`/`isfinite`/`isnormal`/`signbit`. Not implemented by
`src/math/` at all (no coverage needed): `asin`/`acos`, `sinh`/`cosh`/
`tanh` and their inverses, `cbrt`, `expm1`/`log1p`, `erf`/`erfc`,
`lgamma`/`tgamma`, `j0`/`j1`/`jn`/`y0`/`y1`/`yn`, `remainder`/`remquo`,
`nextafter`/`nexttoward`, `fdim`, `fma`, `ilogb`/`logb`, `nearbyint`,
`scalbln`. `lround`/`llround`/`lrint`/`llrint`/`rint` are implemented
but not re-audited clause-by-clause this session (their special-value
behaviour is inherited from `round`/the current rounding mode, already
covered).

| function | clause checked | status | test |
|---|---|---|---|
| fabs / fabsf / fabsl | RETURN VALUE: NaN->NaN, ±0->+0, ±Inf->+Inf | covered | test/posix-math.c |
| copysign / copysignf / copysignl | RETURN VALUE: "magnitude of x and the sign of y", incl. a zero y/zero x and a NaN x | covered | test/posix-math.c |
| fpclassify / isnan / isinf / isfinite / isnormal / signbit | classification of each of the 5 categories, both signs where relevant | covered | test/math.c (sanity), test/posix-math.c (both-signs, DBL_MIN/subnormal boundary) |
| floor / ceil / trunc / round | NaN->NaN, ±0/±Inf passthrough, "result shall have the same sign as x" for a zero result from a nonzero (small-magnitude) input | covered | test/math.c (basic rounding), test/posix-math.c (NaN/Inf passthrough, negative-zero-result sign for ceil/trunc/round) |
| sqrt / sqrtf / sqrtl | RETURN VALUE/ERRORS: NaN->NaN, ±0->x (sign preserved), +Inf->x, finite x<-0 or x==-Inf -> domain error NaN | covered | test/math.c (basic), test/posix-math.c (sqrt(-0.0) sign, -Inf case, float/long double variants) |
| fmod / fmodf / fmodl | RETURN VALUE: sign of result follows x (not y); x or y NaN -> NaN; y==0 or x==±Inf -> domain error NaN; x==±0,y!=0 -> ±0; x finite,y==±Inf -> x exactly | covered | test/math.c (basic + NaN-for-mod-zero), test/posix-math.c (full sign table, ±0 dividend, y==±Inf passthrough, x==Inf still NaN) |
| frexp / frexpf / frexpl | RETURN VALUE: ±0->±0 with \*exp==0, NaN->NaN, ±Inf->x; magnitude in [0.5,1) round-tripping via scalbn, incl. a subnormal input | covered | test/math.c (normal + smallest-subnormal case), test/posix-math.c (±0, NaN, ±Inf, DBL_MIN and a subnormal round trip) |
| ldexp / scalbn (and float/long double variants) | RETURN VALUE: exp==0 -> x; NaN->NaN; ±0/±Inf->x; overflow -> ±HUGE_VAL "according to the sign of x"; underflow past the smallest subnormal -> signed 0 | covered | test/math.c (basic + underflow-to-0), test/posix-math.c (exp==0 for every special value, overflow sign, underflow sign) |
| modf / modff / modfl | RETURN VALUE: NaN->NaN with \*iptr NaN; ±Inf -> ±0 with \*iptr ±Inf; signed-zero integral part for a small negative fraction | covered | test/math.c (basic + Inf case), test/posix-math.c (NaN, both-parts-signed-Inf, modf(-0.5) integral-part sign) |
| exp / expf / expl | RETURN VALUE: NaN->NaN, ±0->1, -Inf->+0, +Inf->x; overflow->HUGE_VAL (informational: exact threshold not asserted, only that it occurs) | covered | test/math.c (basic + overflow/underflow), test/posix-math.c (NaN, -0 case, -Inf->+0) |
| log/log2/log10 (+f/l where present) | RETURN VALUE/ERRORS: NaN->NaN, x==1->+0, x==+Inf->x; x==±0 -> pole error -HUGE_VAL (both signs of zero alike); finite x<0 or x==-Inf -> domain error NaN | covered | test/math.c (basic), test/posix-math.c (full table for all three bases, incl. log(-0.0)) |
| sin/cos/tan/atan (+f/l where present) | RETURN VALUE: NaN->NaN, ±0->x (sin/tan) or ->1 (cos), ±Inf -> domain error NaN (sin/cos/tan); atan: ±0->x, ±Inf->±pi/2 | covered | test/math.c (basic accuracy, sin(HUGE_VAL) NaN), test/posix-math.c (full NaN/±0/±Inf table for all four) |
| atan2 (+f/l where present) | RETURN VALUE: full sign/zero/Inf table -- y==±0 vs x sign, y sign vs x==±0 (both signs of x alike), finite y vs x==±Inf, y==±Inf vs finite x, both ±Inf (the 4 quadrant results) | covered | test/math.c (basic quadrants), test/posix-math.c (worked the full table, ~13 clauses) |
| pow / powf / powl | RETURN VALUE/ERRORS: worked the full ~20-clause special-value table (y==±0, x==+1, x==-1&y==±Inf, domain error for finite x<0 non-int y, NaN propagation, x==±0 for both signs of y incl. odd/even integer y, \|x\|≷1 with y==±Inf, x==±Inf for both signs of y) rather than sampled | covered | test/math.c (~10 clauses sampled), test/posix-math.c (the remaining ~14: x==±0/y<0 pole-error odd-vs-even, x==-Inf odd/even for both signs of y, x==+Inf both signs of y, \|x\|≷1 with y==±Inf both directions) |
| fmax / fmin | RETURN VALUE: one-NaN-argument returns the other; both-NaN -> NaN | covered | test/math.c (basic), test/posix-math.c (both-NaN case; the +0-vs-\-0 tie-break is *not* POSIX-mandated -- marked informational, documents ntlibc's own permitted choice per src/math/fmax.c's comment) |
| hypot | RETURN VALUE: ±Inf wins even over a NaN co-argument (both orders, both signs of the Inf), NaN with a non-Inf co-argument -> NaN, overflow -> HUGE_VAL | **BUG (fenced)** | test/math.c (Inf-beats-one-NaN case only), test/posix-math.c (symmetric NaN cases pass; the both-arguments-±Inf-no-NaN case is fenced, see below) |
| nan / nanf / nanl | RETURN VALUE: "a quiet NaN, if available" | covered | test/math.c (basic), test/posix-math.c (quiet-NaN unordered-compare property, float/long double variants) |
| math_errhandling / MATH_ERRNO / MATH_ERREXCEPT | basedefs math.h.html: the macros' required values; the conditional `<fenv.h>` requirement | **BUG (documented, not fenceable)** | test/posix-math.c (`test_errhandling`), see below |

## Bugs found this session

1. **`hypot(x, y)` returns NaN instead of +Inf when both arguments are
   (non-NaN) infinities**, e.g. `hypot(HUGE_VAL, HUGE_VAL)`,
   `hypot(-HUGE_VAL, -HUGE_VAL)`, `hypot(HUGE_VAL, -HUGE_VAL)`.
   `hypot.html` RETURN VALUE: "If x or y is ±Inf, +Inf shall be
   returned (even if one of x or y is NaN)." `src/math/hypot.c` only
   special-cases the presence of a NaN argument
   (`if (x != x || y != y) { ... }`); when neither argument is NaN but
   both are infinite, it falls through to the general
   `r = ay/ax; return ax * sqrt(1 + r*r)` path, where `ay/ax` is
   `Inf/Inf = NaN`, so the final result is `Inf * NaN = NaN`.
   Confirmed by temporarily un-fencing the three `CHECK()`s at
   `test/posix-math.c`'s `test_hypot` (all three failed exactly as
   predicted, on both i386 and x86_64) before re-fencing them with
   `#if 0 /* BUG: ... */`. Fix would be a one-line special case for
   "both infinite" in `src/math/hypot.c`, left to the maintainer per
   the task's "never edit an assertion to match the implementation"
   rule -- and `src/math/hypot.c` is in this agent's `src/` area, so a
   future session picking this up can fix it directly.

2. **`math_errhandling`'s `<fenv.h>` promise is unfulfilled.**
   `basedefs/math.h.html`: "The following macros shall expand to the
   integer constants 1 and 2, respectively; MATH_ERRNO MATH_ERREXCEPT"
   and "If the expression (`math_errhandling & MATH_ERREXCEPT`) can be
   non-zero, the implementation shall define the macros FE_DIVBYZERO,
   FE_INVALID, and FE_OVERFLOW in `<fenv.h>`." `include/math.h` defines
   `math_errhandling` as the constant `2` (`MATH_ERREXCEPT`), which is
   unconditionally non-zero -- so this "shall" is unconditionally in
   force. But ntlibc has no `include/fenv.h` at all: no
   `FE_DIVBYZERO`/`FE_INVALID`/`FE_OVERFLOW`, no `feclearexcept()`/
   `fetestexcept()`. Every math.h RETURN VALUE section's "An
   application wishing to check for error situations should set errno
   to zero and call feclearexcept(FE_ALL_EXCEPT) ... fetestexcept(...)"
   guidance is therefore unfollowable: `math_errhandling & MATH_ERRNO`
   is correctly 0 (errno is never touched, confirmed by
   `test_errhandling`'s errno-untouched check across a representative
   set of domain/pole/range "errors" -- this part is *conformant*,
   since `MATH_ERRNO` is not claimed), but the `MATH_ERREXCEPT`
   mechanism the header claims to provide does not exist. This is not
   fenceable as a single failing `CHECK()` -- the violation is a
   missing header, not a wrong return value from a function call -- so
   it is documented in a comment in `test_errhandling()`
   (`test/posix-math.c`) and here rather than as a `#if 0` block.
   `include/fenv.h` is outside this agent's `src/math/` /
   `arch/*/src/fpconv.c` scope to add (and would widen `include/`,
   which the task brief forbids for this session regardless); the
   simplest in-scope-adjacent fix, if a maintainer wants one without
   implementing real exception flags, would be changing
   `math_errhandling`'s definition to `0` (neither bit set), which
   the base-definitions text permits and would make the claim honest
   without requiring new functionality.

Every other clause checked against `fabs.html`, `copysign.html`,
`fpclassify.html`, `signbit.html`, `floor.html`, `ceil.html`,
`trunc.html`, `round.html`, `sqrt.html`, `fmod.html`, `frexp.html`,
`ldexp.html`, `scalbn.html`, `modf.html`, `exp.html`, `log.html`,
`log2.html`, `log10.html`, `sin.html`, `cos.html`, `tan.html`,
`atan.html`, `atan2.html`, `pow.html`, `fmax.html`, `fmin.html`,
`nan.html`, and `basedefs/math.h.html` matched ntlibc's `src/math/`
implementation.

## Accuracy: informational, not asserted as failures

Per the task brief, POSIX does not fix a specific ulp bound for the
transcendental functions (`pow`, `exp`, `log`/`log2`/`log10`,
`sin`/`cos`/`tan`/`atan`/`atan2`), only the RETURN VALUE special-value
tables asserted above. `src/math/x87.h`'s own header comment already
documents the expected accuracy budget (fsqrt/frndint/fscale
correctly rounded, fprem exact, the transcendental instructions within
1 ulp of the 80-bit format) and `test/math.c`'s `NEAR()` macro already
spot-checks several transcendental results against known-good decimal
constants at a 1e-14 relative tolerance -- not duplicated here.
`test/posix-math.c` sticks to `==`-exact special values and the
`math_errhandling`-fixed monotonic identities (`exp(0)==1`,
`log(1)==+0`, etc.) rather than adding a second accuracy pass.

## i386 vs x86_64

Ran `make check` on both arches (`make clean` between). Both are
27/27 with `test/posix-math.c` included, and no divergence observed
between the two arches' results for any assertion in this file --
unlike `test/math.c`'s x87-80-bit-vs-NT-64-bit caveat
(`tools/asan-build.sh`'s `not_native()`), `test/posix-math.c` does not
need a `not_native()` entry: it also builds and passes cleanly under
`tools/asan-build.sh` (host clang/x86_64, genuinely 80-bit
`long double`), because its `long double` assertions (`fabsl`,
`copysignl`, `sqrtl`) check NaN-ness/sign/exact-small-integer results
rather than exact bit patterns or tight accuracy bounds that would be
sensitive to the 64-bit-vs-80-bit difference.

## `src/` changes

None needed. Every clause of interest was reachable through the
public API (no wrapper hid a decision that needed extracting to be
testable), so `src/math/` and `arch/*/src/fpconv.c` are unmodified --
the one genuine finding (`hypot`'s both-Inf case) is a real behavioural
bug, not a testability problem, and per the task's rule is fenced with
`#if 0 /* BUG: ... */` rather than fixed.

## What was not reached

- The `l`/`f` variants of `sin`/`cos`/`tan`/`atan`/`atan2` are declared
  and implemented (`src/math/trig.c`) but only the double-precision
  entry points were exercised clause-by-clause here; `test/math.c`
  already spot-checks `sinf`/`cosl` for basic accuracy.
- `lround`/`llround`/`lrint`/`llrint`/`rint` (declared, implemented in
  `src/math/round.c`) were not independently re-audited against
  `lround.html`/`lrint.html`/`rint.html`'s own RETURN VALUE/ERRORS
  text (e.g. the "if the correctly rounded value is outside the range
  of the return type, the numeric result is unspecified" clause) --
  `test/math.c` covers basic ties-to-even/away-from-zero behaviour.
- No fuzzing/property-based cross-check against glibc was added beyond
  what `test/math.c`'s existing `NEAR()` spot checks already do (see
  the accuracy note above); a successor with more time could extend
  that sampling, but it would be informational, not a new "shall".
