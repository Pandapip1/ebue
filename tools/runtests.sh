#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run each test executable under wine; report pass/fail; exit nonzero on
# any failure.
#
# Most tests run concurrently, each in its own private working directory
# (a fresh mktemp -d, removed afterwards) rather than the shared
# obj/test/ the exe files themselves live in: 13 of these tests write
# fixture files using fixed names (posix-io.c, posix-stdlib.c,
# posix-grp.c, misc.c, posix-strings.c, stdio.c, posix-glob.c,
# posix-wchar.c, posix-time.c, posix-stdio.c, stdlib.c, posix-unistd.c,
# unistd.c), and running two of them from the same directory at once is a
# collision -- intermittent, not deterministic, so it would show up as a
# flake rather than a clean failure. Giving *every* test its own private
# directory removes the hazard uniformly instead of hand-tracking which
# 13 need it today and which more might need it tomorrow.
#
# This is safe for $ORIGIN-relative loading (test/rpath.c,
# test/delayall.c): that resolves against the executable's own on-disk
# path (src/internal/rpath.c, via ImagePathName), not the process's
# current directory, so invoking wine with an absolute path to the .exe
# while cd'd elsewhere does not break it -- confirmed by the flake run
# below, which includes rpath.exe every time.
#
# A second, separate hazard: parallel wine processes share one
# wineserver, and a handful of tests exercise process-table/pid state
# that is inherently global to it (spawning children, waiting on them,
# signals) rather than merely "this test's own scratch files" -- see
# test/waitpid-overflow.c's own header comment on the bounded pid-
# retention ring this project's patched Wine adds. Tests named
# fork*/waitpid*/exec*/spawn*/posix-signal* run in a strictly serial
# group (never two of them at once), on the theory that it is
# specifically *concurrent* process-table churn from that family that
# could perturb another instance of itself, not merely coexisting with
# unrelated file-I/O tests. Other tests that happen to use __spawn()
# internally as a plumbing detail (e.g. to observe a child's behaviour
# from outside) stay in the parallel group; if the repeated flake-check
# this project's CONTRIBUTING.md/tooling calls for ever turns up a flake
# traceable to one of those, move it into the serial group and say why,
# rather than widening the pattern speculatively.
#
# Usage: tools/runtests.sh WINE EXE...
# Env:   RUNTESTS_JOBS   parallel workers for the non-serial group
#                        (default: nproc, or 1 if that's unavailable)

wine=$1; shift

: "${RUNTESTS_JOBS:=}"
if [ -z "$RUNTESTS_JOBS" ]; then
	RUNTESTS_JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi

rundir=$(mktemp -d "${TMPDIR:-/tmp}/ntlibc-runtests.XXXXXX") || exit 1
trap 'rm -rf "$rundir"' EXIT

# run_one EXE -- executed once per test, either directly (serial group)
# or as an xargs -P worker (parallel group). Buffers this test's own
# combined output to $rundir/<name>.log and its exit code to
# $rundir/<name>.rc; never touches the real stdout itself, so concurrent
# workers cannot interleave.
run_one() {
	t=$1
	name=${t##*/}
	log="$rundir/$name.log"
	if [ -z "$wine" ]; then
		echo skip > "$rundir/$name.rc"
		: > "$log"
		return 0
	fi
	# `CDPATH=` is an assignment prefixing the `cd`, not a botched assignment.
	# shellcheck disable=SC1007
	abs=$(CDPATH= cd -- "${t%/*}" && pwd)/$name
	work=$(mktemp -d "$rundir/work.XXXXXX") || { echo 1 > "$rundir/$name.rc"; return 1; }
	( cd "$work" && WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d "$wine" "$abs" ) \
		>"$log" 2>&1 </dev/null
	rc=$?
	rm -rf "$work"
	echo "$rc" > "$rundir/$name.rc"
	return "$rc"
}

# Only the group name has to be recognisable from the exe's basename --
# see the header comment above for why exactly these five and no more.
is_serial() {
	case ${1##*/} in
	fork*.exe|waitpid*.exe|exec*.exe|spawn*.exe|posix-signal*.exe) return 0 ;;
	*) return 1 ;;
	esac
}

serial_list=""
parallel_list=""
for t in "$@"; do
	if is_serial "$t"; then serial_list="$serial_list $t"; else parallel_list="$parallel_list $t"; fi
done

# The serial group runs one-at-a-time, in its own background job so it
# overlaps with the parallel group below rather than adding to the
# critical path -- the two groups have no file/PID hazard *between* them,
# only a process-global tests are able to threaten *each other*.
(
	# shellcheck disable=SC2086
	for t in $serial_list; do run_one "$t"; done
) &
serial_pid=$!

if [ -n "$parallel_list" ]; then
	# xargs spawns a fresh sh process per worker (not a subshell of this
	# script), so the run_one *function* above is not reachable there --
	# functions do not cross exec() the way exported variables do. Write
	# the same logic out as a standalone script instead, with $wine and
	# $rundir passed through the environment.
	helper="$rundir/runone.sh"
	cat > "$helper" <<'EOF'
#!/bin/sh
t=$1
name=${t##*/}
log="$rundir/$name.log"
if [ -z "$wine" ]; then
	echo skip > "$rundir/$name.rc"
	: > "$log"
	exit 0
fi
abs=$(CDPATH= cd -- "${t%/*}" && pwd)/$name
work=$(mktemp -d "$rundir/work.XXXXXX") || { echo 1 > "$rundir/$name.rc"; exit 1; }
( cd "$work" && WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d "$wine" "$abs" ) \
	>"$log" 2>&1 </dev/null
rc=$?
rm -rf "$work"
echo "$rc" > "$rundir/$name.rc"
exit "$rc"
EOF
	chmod +x "$helper"
	export wine rundir
	# shellcheck disable=SC2086
	printf '%s\n' $parallel_list | xargs -P "$RUNTESTS_JOBS" -I{} sh "$helper" {}
fi

wait "$serial_pid"

# --- consolidated report, in the exact order tests were passed in ---

pass=0 fail=0
for t in "$@"; do
	name=${t##*/}
	rc=$(cat "$rundir/$name.rc" 2>/dev/null || echo 1)
	if [ "$rc" = skip ]; then
		echo "SKIP $name (no wine)"
	elif [ "$rc" = 0 ]; then
		pass=$((pass + 1)); echo "PASS $name"
	else
		fail=$((fail + 1)); echo "FAIL $name (rc=$rc)"
		tail -n 40 "$rundir/$name.log" 2>/dev/null | sed 's/^/    /'
	fi
done
echo "$pass passed, $fail failed"
test "$fail" -eq 0
