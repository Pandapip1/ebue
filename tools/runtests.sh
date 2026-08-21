#!/bin/sh
# Run each test executable under wine; report pass/fail; exit nonzero on any failure.
wine=$1; shift
fail=0; pass=0
for t in "$@"; do
	name=${t##*/}
	if test -z "$wine"; then echo "SKIP $name (no wine)"; continue; fi
	out=$(cd "${t%/*}" && WINEDEBUG=-all "$wine" "./$name" 2>&1 </dev/null); rc=$?
	if test $rc -eq 0; then
		pass=$((pass+1)); echo "PASS $name"
	else
		fail=$((fail+1)); echo "FAIL $name (rc=$rc)"; printf '%s\n' "$out" | sed 's/^/    /' | tail -n 40
	fi
done
echo "$pass passed, $fail failed"
test $fail -eq 0
