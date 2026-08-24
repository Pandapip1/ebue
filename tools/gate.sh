#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/gate.sh -- run the full pre-push verification gate with every
# independent stage running concurrently, instead of the sum-of-all-stages
# wall clock a plain sequence of `make check`/`make asan`/... gives you.
#
# Why this is safe: every stage below that touches obj/, lib/ or
# config.mak gets its own private copy of the working tree (rsync'd once,
# up front, excluding .git/obj/lib/config.mak) and runs entirely inside
# it. Nothing here ever writes into the tree gate.sh was invoked from,
# except the "generated" stage, which by its nature has to (it checks
# generated-file drift with `git diff`) -- so it is deliberately run
# alone, before any of the copies are even taken, never concurrently with
# anything else.
#
# Each stage's stdout+stderr is buffered to its own log and only ever
# printed as one unit, labelled with the stage name, so a red run still
# tells you exactly which stage failed -- interleaving that would defeat
# the point of running these concurrently.
#
# Usage:
#   tools/gate.sh                run every stage
#   tools/gate.sh check-x86_64 lint-plain      run just the named stages
#   tools/gate.sh --list         print stage names and exit
#
# Requires the same things running each `make`/`tools/*.sh` stage by hand
# would: CC (x86_64-win32-tcc / i386-win32-tcc) on PATH, wine, and for the
# two pinned lint stages, nix-shell able to fetch/build the pinned tool.
# cppcheck is a stage of `tools/lint.sh` itself (invoked as part of
# lint-plain here), not a stage of this script; if cppcheck is not
# installed and LINT_ALLOW_MISSING is not set, lint-plain fails and says
# so -- see tools/lint.sh's own comment for why a silent skip is wrong.
#
# Env:
#   GATE_JOBS_DIR   where per-stage tree copies + logs go (default: a
#                   fresh mktemp -d, removed on a clean exit; kept on
#                   failure so logs stay inspectable).
#   GATE_WINE       wine binary for the check-i386/check-x86_64 stages
#                   (default: whatever ./configure auto-detects from
#                   PATH). Set this explicitly if that wine cannot run
#                   the suite -- e.g. it predates RtlCloneUserProcess, in
#                   which case any test that forks hangs into
#                   `winedbg --auto` forever instead of exiting nonzero.

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

: "${GATE_JOBS_DIR:=}"
own_jobs_dir=0
if [ -z "$GATE_JOBS_DIR" ]; then
	GATE_JOBS_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ntlibc-gate.XXXXXX") || exit 1
	own_jobs_dir=1
fi
mkdir -p "$GATE_JOBS_DIR/logs" "$GATE_JOBS_DIR/trees" || exit 1

# Every concurrent stage name, in the fixed order the summary reports them
# -- independent of start/finish order, so two runs are diffable.
ALL_STAGES="generated reuse check-i386 check-x86_64 asan linkcheck-i386 linkcheck-x86_64 hygiene lint-plain lint-analyze-pinned lint-shell-pinned"

if [ "${1:-}" = "--list" ]; then
	for s in $ALL_STAGES; do echo "$s"; done
	exit 0
fi

STAGES="${*:-$ALL_STAGES}"

note() { printf '%s\n' "$*" >&2; }

# rsync a clean copy of the working tree (as it stands right now,
# uncommitted changes included -- this gates what you're about to push,
# not just HEAD) into $GATE_JOBS_DIR/trees/$1, excluding build output and
# per-arch config so each stage configures its own.
make_tree() {
	dest="$GATE_JOBS_DIR/trees/$1"
	mkdir -p "$dest"
	rsync -a --delete \
		--exclude=.git --exclude=/obj --exclude=/lib --exclude=/config.mak \
		--exclude='*.tmp' \
		"$srcdir/" "$dest/"
}

# run_stage NAME CMD...  -- run CMD (a single sh -c string) in the
# background, buffering combined output to its log; record the exit code
# next to it. Never touches stdout/stderr directly so concurrent stages
# cannot interleave.
run_stage() {
	name=$1; shift
	(
		start=$(date +%s)
		# shellcheck disable=SC2068
		sh -c "$*" > "$GATE_JOBS_DIR/logs/$name.log" 2>&1
		rc=$?
		end=$(date +%s)
		echo "$rc" > "$GATE_JOBS_DIR/logs/$name.rc"
		echo $((end - start)) > "$GATE_JOBS_DIR/logs/$name.time"
	) &
}

want() {
	case " $STAGES " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

overall_start=$(date +%s)

#
# "generated" runs alone, first, in the real tree: it's the one stage
# that legitimately mutates tracked files and diffs them against git, so
# running it next to a tree copy being rsync'd (or next to anything else
# reading the tree) is exactly the race the coordinator flagged. Every
# other stage below only ever touches its own private copy.
#
if want generated; then
	note "== generated (serial, must run alone) =="
	gen_start=$(date +%s)
	(
		make generated
		rc=$?
		if [ $rc -eq 0 ]; then
			git diff --exit-code -- 'arch/*/bits/*.h.gen' 'include/*.h.gen' || rc=$?
			git diff --exit-code -- boot/kaem/ || rc=$?
		fi
		exit $rc
	) > "$GATE_JOBS_DIR/logs/generated.log" 2>&1
	rc=$?
	gen_end=$(date +%s)
	echo "$rc" > "$GATE_JOBS_DIR/logs/generated.rc"
	echo $((gen_end - gen_start)) > "$GATE_JOBS_DIR/logs/generated.time"
fi

# Tree copies for every other wanted stage that needs its own obj/lib/
# config.mak. Taken now, after "generated" has already finished mutating
# the source tree, so every copy sees the same (post-generated) state.
for pair in \
	"check-i386:check-i386" "check-x86_64:check-x86_64" \
	"asan:asan" "linkcheck-i386:linkcheck-i386" "linkcheck-x86_64:linkcheck-x86_64" \
	"hygiene:hygiene" "lint-plain:lint-plain" \
	"lint-analyze-pinned:lint-analyze-pinned" "lint-shell-pinned:lint-shell-pinned" \
	"reuse:reuse"
do
	stage=${pair%%:*}
	want "$stage" || continue
	make_tree "$stage"
done

# --- launch every remaining stage concurrently ---

# GATE_WINE overrides which wine binary the check-* stages configure
# against; unset, ./configure auto-detects from PATH as it always has.
# Set this if PATH's wine can't actually run the suite (e.g. it predates
# RtlCloneUserProcess, so anything that forks hangs into winedbg forever
# instead of exiting) -- see CONTRIBUTING.md / project memory for which
# local wine build that is on this machine.
: "${GATE_WINE:=}"
wine_cfg=""
[ -n "$GATE_WINE" ] && wine_cfg="WINE=$GATE_WINE"

if want check-i386; then
	t="$GATE_JOBS_DIR/trees/check-i386"
	run_stage check-i386 "cd '$t' && ./configure --target=i386-win32 CC=i386-win32-tcc $wine_cfg >/dev/null && make -j\"\$(nproc)\" check"
fi

if want check-x86_64; then
	t="$GATE_JOBS_DIR/trees/check-x86_64"
	run_stage check-x86_64 "cd '$t' && ./configure --target=x86_64-win32 CC=x86_64-win32-tcc $wine_cfg >/dev/null && make -j\"\$(nproc)\" check"
fi

if want asan; then
	t="$GATE_JOBS_DIR/trees/asan"
	run_stage asan "cd '$t' && ./configure --target=x86_64-win32 CC=x86_64-win32-tcc >/dev/null && make asan"
fi

if want linkcheck-i386; then
	t="$GATE_JOBS_DIR/trees/linkcheck-i386"
	run_stage linkcheck-i386 "cd '$t' && ./configure --target=i386-win32 CC=i386-win32-tcc >/dev/null && make linkcheck"
fi

if want linkcheck-x86_64; then
	t="$GATE_JOBS_DIR/trees/linkcheck-x86_64"
	run_stage linkcheck-x86_64 "cd '$t' && ./configure --target=x86_64-win32 CC=x86_64-win32-tcc >/dev/null && make linkcheck"
fi

if want hygiene; then
	t="$GATE_JOBS_DIR/trees/hygiene"
	# hygiene.sh checks both arches itself; the Makefile's $(GENH)
	# prerequisite just needs *a* configured arch to exist.
	run_stage hygiene "cd '$t' && ./configure --target=x86_64-win32 CC=x86_64-win32-tcc >/dev/null && make hygiene"
fi

if want lint-plain; then
	t="$GATE_JOBS_DIR/trees/lint-plain"
	# On a dev machine with clang-tidy/cppcheck/shellcheck already
	# installed, use those (whatever version they are -- that's the
	# whole point of "plain"). Only reach for nix, unpinned, if every
	# one of them is missing outright -- never silently accepting
	# fewer checks: tools/lint.sh's own LINT_ALLOW_MISSING default (0)
	# is untouched either way, so a machine with some but not all of
	# the three still fails loudly, same as it always has.
	if command -v clang-tidy >/dev/null 2>&1 || command -v cppcheck >/dev/null 2>&1 || command -v shellcheck >/dev/null 2>&1; then
		run_stage lint-plain "cd '$t' && LINT_ALLOW_MISSING=\${LINT_ALLOW_MISSING:-0} tools/lint.sh"
	else
		run_stage lint-plain "cd '$t' && nix-shell -p llvmPackages.clang-tools cppcheck shellcheck --run 'LINT_ALLOW_MISSING=\${LINT_ALLOW_MISSING:-0} tools/lint.sh'"
	fi
fi

if want lint-analyze-pinned; then
	t="$GATE_JOBS_DIR/trees/lint-analyze-pinned"
	run_stage lint-analyze-pinned "cd '$t' && nix-shell -p llvmPackages_18.clang-tools --run 'CLANG_TIDY=clang-tidy tools/lint.sh analyze'"
fi

if want lint-shell-pinned; then
	t="$GATE_JOBS_DIR/trees/lint-shell-pinned"
	run_stage lint-shell-pinned "cd '$t' && nix-shell -I nixpkgs=channel:nixos-23.11 -p shellcheck --run 'tools/lint.sh shell'"
fi

if want reuse; then
	t="$GATE_JOBS_DIR/trees/reuse"
	if command -v reuse >/dev/null 2>&1; then
		run_stage reuse "cd '$t' && reuse lint"
	else
		run_stage reuse "cd '$t' && nix-shell -p reuse --run 'reuse lint'"
	fi
fi

wait

overall_end=$(date +%s)

# --- consolidated report, fixed order ---

fail=0
missing=0
echo
echo "=== gate summary (wall clock: $((overall_end - overall_start))s) ==="
for s in $ALL_STAGES; do
	want "$s" || continue
	rcfile="$GATE_JOBS_DIR/logs/$s.rc"
	[ -f "$rcfile" ] || continue
	rc=$(cat "$rcfile")
	t=$(cat "$GATE_JOBS_DIR/logs/$s.time" 2>/dev/null || echo '?')
	if [ "$rc" = skip ]; then
		echo "SKIP  $s"
		missing=1
	elif [ "$rc" = 0 ]; then
		echo "PASS  $s (${t}s)"
	else
		echo "FAIL  $s (rc=$rc, ${t}s)"
		fail=1
	fi
done

echo
for s in $ALL_STAGES; do
	want "$s" || continue
	rcfile="$GATE_JOBS_DIR/logs/$s.rc"
	[ -f "$rcfile" ] || continue
	rc=$(cat "$rcfile")
	if [ "$rc" != 0 ] && [ "$rc" != skip ]; then
		echo "--- $s output (last 60 lines) ---"
		tail -n 60 "$GATE_JOBS_DIR/logs/$s.log" | sed 's/^/    /'
		echo
	fi
done

if [ "$fail" -ne 0 ]; then
	echo "gate FAILED. Full logs kept at $GATE_JOBS_DIR/logs/"
	exit 1
fi
if [ "$missing" -ne 0 ]; then
	echo "gate ran with one or more stages SKIPPED (see above) -- this checks"
	echo "less than a full run. Logs kept at $GATE_JOBS_DIR/logs/"
	exit 0
fi

echo "gate PASSED (all stages)."
[ "$own_jobs_dir" = 1 ] && rm -rf "$GATE_JOBS_DIR"
exit 0
