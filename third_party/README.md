<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# `third_party/` — code this repository points at but does not contain

Everything under this directory is a **git submodule**. This repository
stores a 20-byte gitlink per entry — a repository URL and a commit SHA —
and no third-party source at all. That distinction is the whole point of
the arrangement and it is load-bearing for the licence question below.

## `libc-test` — musl's regression corpus

| | |
|---|---|
| submodule remote | `https://github.com/Pandapip1/libc-test.git` |
| pinned SHA | **`68edb8bd73dab8147ee54c8bec638f4d2b3cff37`** (`math: fmaf underflow tests`) |
| upstream | `git://repo.or.cz/libc-test.git` — **not** the `jart` GitHub fork, which is a different tree |
| licence | MIT overall (`COPYRIGHT`), **except `src/math/`** — see below |
| driver | `tools/libc-test.sh`, ledger `test/libc-test-expected.txt` |

### Provenance of the remote

`https://github.com/Pandapip1/libc-test.git` is **not a GitHub fork** —
it cannot be, because upstream lives on `repo.or.cz` and there is no fork
button to press. It is a **fresh public GitHub repository seeded by
pushing upstream's history into it**: all 414 commits of
`git://repo.or.cz/libc-test.git` `master`, unsquashed, so `68edb8b` is a
genuine ancestor with upstream's own tree and not a synthetic commit that
merely happens to have the same contents.

That matters for two things a squash would have made impossible:

* the fork can be diffed against upstream, so "have we drifted?" stays a
  one-liner (`git diff upstream/master`);
* if this project ever needs to carry a patch, that patch can be rebased
  onto a newer upstream.

The canonical working clone is `~/Projects/libc-test`, with `origin`
pointing at `repo.or.cz` and `ghfork` at the GitHub mirror — the same
`origin`/`ghfork` split this machine uses for `~/Projects/wine`.

**The fork exists so the pin cannot vanish.** `repo.or.cz` is a single
volunteer-run host; a gate stage pinned to a SHA that only exists there
is one outage away from being unfixable. Nothing in the fork is patched
today, and it should stay that way until there is a reason.

### Nothing here is modified

The four helpers that cannot work against this library — `t_vmfill` (no
`mmap`), `t_setrlim` and `t_fdfill` (rlimits are not enforced),
`t_setutf8` (no `langinfo.h`) — are **replaced at link time** by
`test/libc-test-shim-src/libc-test-shim.c`. They are never patched in
place, precisely so that "is our copy clean?" stays a one-liner:

```sh
git -C third_party/libc-test status --porcelain   # must be empty
git -C third_party/libc-test rev-parse HEAD       # must be 68edb8b...
```

`git submodule status` from the top of the tree says both at once: a
leading `+` means the checkout has moved off the pin, and a trailing
`-` means it was never initialised at all.

### Moving the pin

```sh
cd third_party/libc-test
git fetch origin && git checkout <new-sha>
cd ../.. && git add third_party/libc-test
```

Then re-run `make libc-test` and expect the ledger to need edits: every
test in the corpus must have exactly one row in
`test/libc-test-expected.txt`, and new upstream tests arrive with none.
That is deliberate — see `tools/libc-test.sh`'s header.

### Deleting a public symbol? Re-run `make libc-test`

The ledger encodes, per test, whether that test *links* against this
library — so removing any public symbol can turn a `pass` row into an
`unbuildable` one, and the stage goes red in CI rather than on the branch
that did the removing. `tools/lint-unreferenced.sh` does not catch this:
it scans `test/*.c`, and this corpus is not ours.

That happened once already. `clearenv()` was dropped in `49b8099` as an
unused non-POSIX extension — correctly, on a downstream survey that found
no consumer — but musl provides it, so `src/functional/env.c` calls it,
and `env`'s row had to become `unbuildable`. The removal was right; only
the ledger was stale.

**So: any commit that removes a public symbol must re-run `make
libc-test` before it is pushed.** No tooling enforces this, deliberately
— the check is one command, and a mechanism nobody maintains would be
worth less than this paragraph.

### Why a submodule and not a vendored copy

An earlier attempt vendored 173 files (792 KB) of this corpus directly
into the tree. It worked, but it nearly tripled a 5.6 MB repository and
put a foreign corpus under our own history, where `git log` and `git
blame` over `src/` stop being about this project's code.

The objection to a submodule was that it makes a verification stage
depend on the network. That objection is weaker than it looks here:
`nova-nix`, the downstream chain that actually builds ntlibc into shipped
Windows binaries, already consumes this repository through `fetchgit`,
and `fetchSubmodules = true` is an ordinary thing to write there. A
submodule is *consistent* with how this project is really packaged.

What the objection does establish, and what still stands, is that **an
absent submodule must fail loudly**. A developer who clones without
`--recurse-submodules` gets an empty directory, and `tools/libc-test.sh`
exits **2** with a message naming `git submodule update --init
--recursive` rather than skipping, passing, or degrading to zero tests.
There is deliberately **no offline cache and no fallback tarball**: a
fallback that lets the stage go green without the corpus is exactly the
vacuous stage this project spent a day removing nine of
(`test/verification-measures.md`).

### `src/math/`: present, unused, and differently licensed

This is the one thing about a submodule that is genuinely worse than
vendoring, so it is stated rather than omitted.

Upstream's own `COPYRIGHT` says:

> math tests use numbers under BSD and GPL licenses see `src/math/ucb/*`
> and `src/math/crlibm/*`

The vendored copy simply left `src/math/` out — 9.6 MB, 83% of the tree —
so the tree was uniformly MIT by construction. A submodule cannot do
that: `git submodule update --init` brings the **whole** repository, so
those BSD/GPL vectors are now on a developer's disk.

**A sparse checkout was considered and rejected.** `.gitmodules` has no
field that makes `git clone --recurse-submodules` or `git submodule
update --init` apply a sparse pattern, so enforcing one would require a
bespoke init step — and anybody typing the standard incantation would get
the full tree anyway. A rule that the documented command does not obey is
not a rule; it is a comment that lies.

So the answer is not to hide the files but to be exact about what this
repository does with them:

1. **This repository does not distribute them.** What is committed here
   is a gitlink: a URL and a SHA. Cloning ntlibc without
   `--recurse-submodules` fetches zero bytes of libc-test. That is a
   materially better position than the vendored copy, which put 792 KB of
   third-party source into *our* history permanently — and it is why the
   `src/math` exclusion was needed there and is not needed here.
2. **Nothing under `src/math/` is compiled, linked, or shipped.**
   `tools/libc-test.sh` builds `src/common`, `src/functional` and
   `src/regression` only. The `math` mode exists, is not wired into
   `tools/gate.sh`, and refuses to run without an explicit
   `LIBC_TEST_MATH=` opt-in — see that script's `math` case.
3. **They are not this repository's files for REUSE purposes.** Submodule
   contents belong to the submodule's own project, which ships its own
   `COPYRIGHT`. `REUSE.toml` therefore carries no stanza for them; adding
   one would be a false claim, and the vendored version's single
   `MIT` aggregate stanza would have been *actively wrong* over
   `src/math/ucb/` and `src/math/crlibm/`.

Point 3 is the part that would have been easy to get wrong by inertia:
carrying the old `third_party/libc-test/** = MIT` stanza forward would
have asserted MIT over BSD- and GPL-licensed vectors that the previous
arrangement had been careful to keep out of the tree entirely.
