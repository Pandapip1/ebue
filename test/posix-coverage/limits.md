<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# limits.h / float.h / stdint.h / inttypes.h coverage fragment

None of these four headers (nor the inttypes.h functions strtoimax/
strtoumax/imaxabs/imaxdiv) had been audited before this session -- see
`test/POSIX-COVERAGE.md`'s "Also not yet reached at all" list. This
fragment is written standalone for the coordinator to merge; it does
not edit `test/POSIX-COVERAGE.md` itself.

New clause-cited audit: `test/posix-limits.c` (226 `CHECK()`
assertions, plus two `#if`-only checks on `UINT64_C`/`INTMAX_C` that
fail the build itself if wrong). No pre-existing broad sanity file
covered this ground (unlike string.h/stdlib.h/math.h, which had a
`test/string.c`-style file already); this is genuinely new coverage.

Each assertion cites the clause of
`https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/<hdr>.html`
(or `.../functions/<name>.html`) it checks. Two rules held throughout,
per the task brief:

1. Where POSIX gives a floor/ceiling ("Minimum/Maximum Acceptable
   Value"), the assertion checks the *direction* the spec states, not
   ntlibc's exact number -- a change-detector is not a conformance
   check.
2. Where a value is genuinely arch-dependent under this target's LLP64
   model (`long` 32-bit, pointer-width types 32/64-bit by arch,
   `wchar_t` 16-bit UTF-16 on both), the expected value is derived from
   `sizeof()`/the type's own arithmetic rather than hardcoded, so one
   assertion covers both arches -- see the file banner for the one
   necessary exception (`long`'s actual compiler-chosen width, guarded
   by `__SIZEOF_LONG__` so it simply skips under a native `make asan`
   build rather than needing a `tools/asan-build.sh` `not_native()`
   entry).

## Header coverage

| header | what's checked | status | test |
|---|---|---|---|
| limits.h | CHAR_BIT/SCHAR/UCHAR/CHAR exact values; SHRT/INT/LONG/LLONG *_MAX/*_MIN and U*_MAX floors (direction only); MB_LEN_MAX, WORD_BIT, LONG_BIT floors | covered | test/posix-limits.c (`test_limits_numerical`) |
| limits.h | internal consistency: UINT_MAX==(unsigned)-1, *_MIN<=-*_MAX, sizeof(type)\*CHAR_BIT matching *_MAX's actual bit width (int, long long unconditionally; long guarded on __SIZEOF_LONG__==4) | covered | test/posix-limits.c (`test_limits_consistency`) |
| limits.h | SSIZE_MAX == the true max of `ssize_t` (not just >= the POSIX floor) | covered; **BUG found and fixed** (see below) | test/posix-limits.c (`test_limits_consistency`) |
| limits.h | NAME_MAX/PATH_MAX/PIPE_BUF/SYMLOOP_MAX/NGROUPS_MAX/OPEN_MAX/ARG_MAX/TZNAME_MAX/TTY_NAME_MAX/HOST_NAME_MAX/FILESIZEBITS/IOV_MAX vs their `_POSIX_*`/spec floors | covered | test/posix-limits.c (`test_limits_pathname`) |
| limits.h | `_POSIX_*`/`_POSIX2_*` "Minimum Values" table: representative cross-section (19 macros) asserted equal to the spec's literal floor; the remaining ~15 (`_POSIX_AIO_LISTIO_MAX`, `_POSIX_AIO_MAX`, `_POSIX_DELAYTIMER_MAX`, `_POSIX_LOGIN_NAME_MAX`, `_POSIX_MAX_CANON`, `_POSIX_MAX_INPUT`, `_POSIX_RE_DUP_MAX`, `_POSIX_RTSIG_MAX`, `_POSIX_SEM_NSEMS_MAX`, `_POSIX_SEM_VALUE_MAX`, `_POSIX_SIGQUEUE_MAX`, `_POSIX_SYMLINK_MAX`, `_POSIX_TIMER_MAX`, `_POSIX2_BC_DIM_MAX`, `_POSIX2_BC_SCALE_MAX`, `_POSIX2_BC_STRING_MAX`, `_POSIX2_CHARCLASS_NAME_MAX`, `_POSIX2_COLL_WEIGHTS_MAX`, `_POSIX2_EXPR_NEST_MAX`) were diffed by hand against the fetched spec table and all match exactly; not each re-asserted to avoid a ~40-line wall of identical-shaped CHECKs | covered | test/posix-limits.c (`test_limits_posix_floors`) |
| limits.h | `ATEXIT_MAX`, `CHILD_MAX` | N/A (spec-conformant omission) | Runtime Invariant Values "may be omitted if [the value] is indeterminate"; ntlibc reports `CHILD_MAX` only via `sysconf(_SC_CHILD_MAX)` (`src/unistd/sysconf.c`, out of this audit's `src/` scope) |
| float.h | FLT_RADIX, FLT_ROUNDS, FLT_EVAL_METHOD domain | covered | test/posix-limits.c (`test_float_radix_and_rounds`) |
| float.h | FLT_EVAL_METHOD's *correct* value for this compiler's actual codegen | not testable portably | needs disassembly, not a documented compiler macro; asserted only to be one of the four defined values, not asserted to be i386=2/x86_64=0 (which is what ntlibc's headers already say and looks right by convention, but wasn't independently re-verified) |
| float.h | FLT_DIG/MANT_DIG/MIN_10_EXP/MAX_10_EXP/MAX/MIN/EPSILON floors; the EPSILON defining property (`1+eps != 1`, `1+eps/2 == 1`) | covered, FLT and DBL | test/posix-limits.c (`test_float_flt`, `test_float_dbl`) |
| float.h | DECIMAL_DIG floor and >= DBL_DIG | covered | test/posix-limits.c (`test_float_dbl`) |
| float.h | LDBL_* floors and EPSILON property, for whichever of the two `long double` layouts this compiler actually built (80-bit x87 extended vs. an 8-byte alias for `double`) | covered; **BUG found and fixed** (see below) | test/posix-limits.c (`test_float_ldbl`) |
| stdint.h | int8/16/32/64_t, uint8/16/32/64_t: exact sizeof, no padding, two's complement (*_MIN/*_MAX exact, (u)-1==*_MAX) | covered | test/posix-limits.c (`test_stdint_exact_width`) |
| stdint.h | int_least\*/int_fast\* families: magnitude floor vs the exact-width type, and the *_MAX macro matching its own type's real range | covered | test/posix-limits.c (`test_stdint_least_fast`) |
| stdint.h | intmax_t/uintmax_t: floor, two's-complement identity, >= long long | covered | test/posix-limits.c (`test_stdint_max`) |
| stdint.h | intptr_t/uintptr_t/ptrdiff_t/size_t: pointer-width split (LLP64), each *_MAX/*_MIN derived from `sizeof(intptr_t)`/`sizeof(ptrdiff_t)` rather than hardcoded, plus the POSIX stdint.h floors (65535) | covered | test/posix-limits.c (`test_stdint_pointer_width`) |
| stdint.h | wchar_t: 16-bit UTF-16 (not the 32-bit-on-Linux convention), WCHAR_MIN/MAX derived from `sizeof(wchar_t)`/`(wchar_t)-1`, not hardcoded to either signedness convention | covered | test/posix-limits.c (`test_stdint_wchar`) |
| stdint.h | wint_t (WINT_MIN/MAX), sig_atomic_t (SIG_ATOMIC_MIN/MAX) floors + exact-width identity | covered | test/posix-limits.c (`test_stdint_wchar`) |
| stdint.h | INTN_C/UINTN_C/INTMAX_C/UINTMAX_C: `#if`-usability (literal `#error` guards) and runtime value/width | covered | test/posix-limits.c (`test_stdint_c_macros`) |
| inttypes.h | PRId/i/o/u/x/X8/16/32/64, SCNd/i/o/u/x8/16/32/64: round-tripped through real `sprintf`/`sscanf`, not just compiled | covered | test/posix-limits.c (`test_inttypes_pri_fixed`, `test_inttypes_scn_fixed`) |
| inttypes.h | PRI/SCN LEAST64/FAST64/FAST32 families | covered (representative; not all 36 LEAST/FAST combinations individually round-tripped -- the length-modifier machinery is shared by width class, see `__PRI64` in include/inttypes.h, so LEAST64/FAST64/FAST32 exercise every distinct modifier the header emits) | test/posix-limits.c (`test_inttypes_pri_scn_least_fast`) |
| inttypes.h | PRI/SCN dMAX/uMAX, PRI/SCN d/u/xPTR -- the LLP64 pointer-width family the task brief flags as highest-risk (`%ld` vs `%lld`), round-tripped at the arch's actual `INTPTR_MAX`/`INTPTR_MIN`/`UINTPTR_MAX` | covered | test/posix-limits.c (`test_inttypes_pri_scn_max_ptr`) |
| inttypes.h | imaxdiv_t struct, wcstoimax/wcstoumax declarations | not independently re-audited | `wcstoimax`/`wcstoumax` live in `src/stdlib/wcstoimax.c`, out of this audit's assigned `src/` file (only `strtoimax`/`strtoumax`/`imaxabs`/`imaxdiv`'s implementing file was in scope); `imaxdiv_t`'s layout is exercised structurally by every `imaxdiv()` call in `test_imaxdiv` |

## strtoimax / strtoumax / imaxabs / imaxdiv

Implementation: `src/stdlib/strtol.c` (strtoimax/strtoumax share the
`strtox()`/`parse()` machinery with strtol/strtoul/strtoll/strtoull),
`src/stdlib/abs.c` (imaxabs), `src/stdlib/div.c` (imaxdiv).

| function | clause checked | status | test |
|---|---|---|---|
| strtoimax/strtoumax | RETURN VALUE/ERRORS: ERANGE + saturation at INTMAX_MAX/INTMAX_MIN/UINTMAX_MAX on overflow; clean conversion at the exact boundary values; EINVAL (required "shall fail", not "may fail") for an unsupported base, with endptr==nptr; no-conversion endptr==nptr; base-0 prefix auto-detection; errno unchanged on a clean conversion | covered | test/posix-limits.c (`test_strtoimax_errors`) |
| imaxabs | RETURN VALUE: absolute value, for 0/positive/negative/INTMAX_MAX | covered | test/posix-limits.c (`test_imaxabs`) |
| imaxabs | `imaxabs(INTMAX_MIN)` | **deliberately not tested** | DESCRIPTION: "If the result cannot be represented, the behavior is undefined" -- `-INTMAX_MIN` overflows `intmax_t`, so any expected value the test could name would just be asserting the UB itself, not a spec requirement; see the comment above `test_imaxabs` in test/posix-limits.c |
| imaxdiv | RETURN VALUE: `quot*denom + rem == numer` and truncation toward zero, for all four sign combinations plus a small table including near-INTMAX_MAX/MIN operands | covered | test/posix-limits.c (`test_imaxdiv`) |

## Bugs found this session

Both were found by deriving expected values from `sizeof()`/the actual
type's arithmetic rather than trusting the header's own number --
exactly the class of bug the task brief flagged as the primary hunting
ground (the LLP64 split). Both are header-value fixes (no ABI/public
API change) and were made directly rather than fenced with `#if 0`,
per the task brief's "a macro whose value is simply wrong is a header
fix you may make directly" rule.

1. **`SSIZE_MAX` used the 32-bit `LONG_MAX` on x86_64, where `ssize_t`
   is actually a 64-bit `long long`.** `include/limits.h` had
   `#define SSIZE_MAX LONG_MAX`. `ssize_t` is typedef'd from `_Addr`
   (`arch/*/bits/alltypes.h.in`): `int` (32-bit) on i386, `long long`
   (64-bit) on x86_64 -- it tracks pointer width, not `long`, despite
   the name. `long` stays 32-bit on both arches under this target's
   LLP64 model (confirmed: `LONG_MAX` is `0x7fffffffL` in both
   `arch/i386/bits/limits.h` and `arch/x86_64/bits/limits.h`), so on
   i386 `SSIZE_MAX == LONG_MAX` happened to be numerically correct
   (both 2^31-1), masking the bug there. On x86_64, `SSIZE_MAX` was
   silently capped at 2^31-1 despite `ssize_t` actually holding 64
   bits -- a real conformance defect against
   `limits.h.html`'s "SSIZE_MAX: Maximum value for an object of type
   ssize_t." **Fixed**: moved `SSIZE_MAX` out of the generic
   `include/limits.h` into `arch/i386/bits/limits.h` (`0x7fffffff`,
   unchanged value) and `arch/x86_64/bits/limits.h` (new:
   `0x7fffffffffffffffLL`). Regression test:
   `test_limits_consistency`'s `SSIZE_MAX` check in
   test/posix-limits.c, which derives the expected value from
   `sizeof(ssize_t)` so it covers both arches from one assertion.

2. **`LDBL_MANT_DIG`/`LDBL_MAX`/`LDBL_EPSILON`/etc. unconditionally
   described the 80-bit x87 extended format, even for this tcc's PE
   targets where `long double` is really just an 8-byte `double`.**
   `arch/i386/bits/float.h` and `arch/x86_64/bits/float.h` both had a
   single, unconditional block: `LDBL_MANT_DIG 64`,
   `LDBL_MAX 1.1897...e+4932L`, etc. `src/math/x87.h` (and its
   `NTLIBC_LDBL_EXTENDED` macro, used throughout `src/math/`'s
   `fpclassify.c`/`fabs.c`/`copysign.c`/`frexp.c`) already documents,
   in detail, that this exact tcc build gives `long double` no 80-bit
   range or precision at all (`sizeof(long double) == 8`,
   `__SIZEOF_LONG_DOUBLE__` undefined -- confirmed empirically both by
   that comment and independently in this session, see the file banner
   of test/posix-limits.c) -- while the mingw-w64/gcc fallback compiler,
   and the native gcc/clang used for `make asan`, give it the genuine
   80-bit format. Before the fix, a tcc-built PE binary's `float.h`
   claimed `LDBL_MAX ~= 1.19e+4932L`, which cannot be represented in an
   8-byte `double` (the literal itself would need to become +Inf at
   initialization) and `LDBL_EPSILON ~= 1.08e-19L`, implying 64 bits of
   mantissa precision an 8-byte object does not have. This is the
   `math.h`/`long double` divergence the task brief calls out
   explicitly. **Fixed**: gated both arch headers' LDBL_* block on
   `defined(__SIZEOF_LONG_DOUBLE__) && __SIZEOF_LONG_DOUBLE__ > 8` --
   the identical test `src/math/x87.h`'s `NTLIBC_LDBL_EXTENDED` already
   uses -- keeping the 80-bit values for that branch and adding a new
   branch that mirrors `DBL_*` exactly (since `long double` really is
   `double` there) for the tcc/NT-target branch. Regression test:
   `test_float_ldbl` in test/posix-limits.c, written entirely in terms
   of `sizeof(long double)`/`DBL_MANT_DIG` so it holds under either
   branch rather than asserting one specific width.

No other divergence was found between i386 and x86_64 in these four
headers: `intptr_t`/`uintptr_t`/`ptrdiff_t`/`size_t`/`WCHAR_MIN`/
`WCHAR_MAX` are already correctly split per-arch (via `_Addr` in
`arch/*/bits/alltypes.h.in` and `arch/*/bits/stdint.h`), and
`inttypes.h`'s `__PRI64`/`__PRIPTR` selection (`include/inttypes.h`)
was independently checked against those same typedefs and found
correct as-is (int64_t/intmax_t are always `long long`, hence always
`"ll"`, on both arches; intptr_t/uintptr_t is `int` on i386 -- plain
`%d`, no modifier -- and `long long` on x86_64 -- `"ll"` -- which is
exactly what `__PRIPTR`'s `UINTPTR_MAX == UINT64_MAX` branch already
selects).

## Verification

`make check` (WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d): 28/28 on
both i386 and x86_64 (was 27/27 before this file; `posix-limits.exe`
is the only new entry). `make generated`: no drift. `make lint`
(clang-tidy/cppcheck/shellcheck via nix-shell): see the session's own
report for the result at the time this fragment was written.
