<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Vendoring record: musl `libc-test`

| | |
|---|---|
| upstream | `git://repo.or.cz/libc-test.git` — **not** the `jart` GitHub fork, which is a different tree |
| pinned SHA | **`68edb8bd73dab8147ee54c8bec638f4d2b3cff37`** (`math: fmaf underflow tests`) |
| licence | MIT (`COPYRIGHT`), compatible with this tree's GPL-3.0-or-later |
| vendored | `COPYRIGHT`, `README`, `src/common/`, `src/functional/`, `src/regression/` — 173 files, 792 KB, byte-identical to upstream at that SHA |
| **not** vendored | `src/math/` (9.6 MB), `src/api/`, `src/musl/` |

Restore the vendored subset exactly with:

```sh
git clone git://repo.or.cz/libc-test.git /tmp/libc-test
cd /tmp/libc-test && git checkout 68edb8b
git archive 68edb8b COPYRIGHT README src/common src/functional src/regression \
  | tar -x -C <this directory>
```

`git diff` after that must be empty. Nothing under this directory is
modified: the four helpers that cannot work here are *replaced* at link
time by `test/libc-test-shim.c`, not patched in place, precisely so this
check stays a one-liner.

## Why vendored, and not fetched at build time

Fetching would make a verification stage depend on the network. A
network-dependent gate has exactly two behaviours when the network is
gone, and both are worse than the disk cost of 792 KB: it fails for a
reason that has nothing to do with the change under test, or — the one
that matters — somebody adds a fallback and it starts passing while
having run nothing. This project spent a day closing nine stages that
reported success having checked nothing (`test/verification-measures.md`);
introducing a tenth by way of a `curl ||  true` would be a poor trade for
792 KB.

The accepted cost of vendoring: 173 files that `reuse lint` must be told
about (`REUSE.toml`), and a pin that only moves when somebody moves it —
upstream fixes and new tests do not arrive on their own. That is the
right way round for a corpus whose whole job is to be a fixed yardstick.

## Why `src/math/` is not here

Two independent reasons, either sufficient:

1. **Licence.** Upstream's own `COPYRIGHT` says *"math tests use numbers
   under BSD and GPL licenses see `src/math/ucb/*` and
   `src/math/crlibm/*`"*. The subset vendored above is uniformly MIT;
   `src/math/` is 9.6 MB of mixed-licence third-party vectors.
2. **Size and readiness.** 9.6 MB would nearly triple this repository
   (5.6 MB today), for a corpus `test/external-suites.md` explicitly
   says should stay on demand until its 82 failures are triaged — among
   them spurious `FE_DIVBYZERO` from `logb(inf)`/`exp(inf)`/ordinary
   `pow`, the `isless` family raising `FE_INVALID` on NaN, and `fma()`
   failing at 0.5–0.8 ULP, which means it is not fused.

`tools/libc-test.sh math` therefore requires `LIBC_TEST_MATH=` pointing
at a checkout at the SHA above, and **exits non-zero when it is not
given** rather than skipping. See that script's `math` case.
