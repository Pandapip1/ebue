#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tsan-probe.sh -- opt-in ThreadSanitizer probe for ntlibc's shared state.
#
# This is NOT part of `make all`, `make check`, or `make asan`, and it must
# never become a build dependency: it needs clang, glibc's pthreads, and a
# *dynamic* TSan runtime, none of which the tcc+Wine build has.
#
# Why it exists.  ntlibc has no threads of its own -- there is no pthreads
# implementation in src/, and flockfile() is a documented no-op.  But a libc
# is used by programs, and NT programs create threads, so the question that
# matters is whether ntlibc is safe to *call* from two threads.  Today it is
# not: errno is a plain global (src/internal/errno.c), and several tables are
# read-modify-written with no interlock.
#
# The probe builds src/*.c natively the same mechanical way tools/asan-build.sh
# does -- compile everything, keep what the compiler accepts, stub NT with
# fuzz/ntstubs.c -- and then runs a driver that calls ntlibc from two host
# pthreads.  It is a regression check for thread-safety work: make errno
# per-thread (a TEB slot -- see __teb() in src/internal/$ARCH/teb.c) and the
# first report below must disappear.
#
# -shared-libsan is load-bearing, not cosmetic, and for a sharper reason than
# in asan-build.sh.  With the static runtime, TSan's own start-up calls
# confstr() from __sanitizer::GetLibcVersion(); that binds to ntlibc's
# confstr() in src/unistd/sysconf.c, which runs instrumented before
# __tsan::Initialize() has finished, and the process dies in TraceSwitchPart
# before main().  The dynamic runtime resolves those calls through libc.so.
#
# Symbolization is off by default: llvm-symbolizer takes minutes on this
# binary.  The script resolves the raced *addresses* with nm instead, which is
# what identifies the variable anyway.  Set TSAN_SYMBOLIZE=1 to override.
#
# Usage: tools/tsan-probe.sh
# Env:   NTLIBC_CC (default clang), NTLIBC_TSAN_OBJ (default obj/tsan),
#        NTLIBC_ARCH (default x86_64), TSAN_SYMBOLIZE (default 0)

set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
CC=${NTLIBC_CC:-clang}
OBJ=${NTLIBC_TSAN_OBJ:-$srcdir/obj/tsan}
ARCH=${NTLIBC_ARCH:-x86_64}
SYMBOLIZE=${TSAN_SYMBOLIZE:-0}

if [ ! -f "$srcdir/obj/include/bits/alltypes.h" ]; then
	echo "tsan: obj/include/bits/alltypes.h missing -- run 'make' first" >&2
	exit 1
fi

RT=$($CC -print-file-name=libclang_rt.tsan-"$ARCH".so)
case $RT in
*/libclang_rt.tsan-*.so) ;;
*)	echo "tsan: $CC has no dynamic TSan runtime (libclang_rt.tsan-$ARCH.so);" >&2
	echo "      the static one cannot work here -- see the header comment." >&2
	exit 1 ;;
esac
SAN="-fsanitize=thread -shared-libsan -Wl,-rpath,$(dirname "$RT")"

INC="-I$srcdir/src/internal -I$srcdir/obj/include -I$srcdir/include \
     -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"
CFLAGS="$SAN -g -O1 -std=c99 -nostdinc -fno-builtin -fvisibility=hidden \
        -D_XOPEN_SOURCE=700 -D_NTLIBC_INTERNAL $INC"

rm -rf "$OBJ"
mkdir -p "$OBJ/obj"

# Same mechanical file selection as asan-build.sh: other-architecture
# directories are not ours, and teb.c reads gs:0x30 (ntstubs.c supplies it).
: > "$OBJ/compiled.txt"
: > "$OBJ/skipped.txt"
for f in $(cd "$srcdir" && find src -name '*.c' | sort); do
	sub=$(echo "$f" | awk -F/ 'NF==4{print $3}')
	if [ -n "$sub" ] && [ "$sub" != "$ARCH" ]; then
		echo "$f  (other architecture)" >> "$OBJ/skipped.txt"
		continue
	fi
	case $f in
	*/teb.c) echo "$f  (reads gs:0x30; __teb() comes from ntstubs.c)" >> "$OBJ/skipped.txt"
	         continue ;;
	esac
	o="$OBJ/obj/$(echo "$f" | tr / _).o"
	# $CC/$CFLAGS/$SAN are flag lists and must word-split.
	# shellcheck disable=SC2086
	if $CC -c $CFLAGS -w "$srcdir/$f" -o "$o" 2> "$o.err"; then
		echo "$f" >> "$OBJ/compiled.txt"
	else
		echo "$f  (see $o.err)" >> "$OBJ/skipped.txt"
		rm -f "$o"
	fi
done
# $CC/$CFLAGS/$SAN are flag lists and must word-split.
# shellcheck disable=SC2086
$CC -c $CFLAGS -w "$srcdir/fuzz/ntstubs.c" -o "$OBJ/ntstubs.o"
echo "tsan: $(wc -l < "$OBJ/compiled.txt") src/*.c compiled natively,\
 $(wc -l < "$OBJ/skipped.txt") skipped"

# The driver lives here rather than in test/, because test/ is built for the
# NT target where none of this can run.  <pthread.h> is glibc's and we build
# -nostdinc against ntlibc's headers, so the two entry points are declared by
# hand; pthread_t is an unsigned long on every glibc port this can run on.
cat > "$OBJ/driver.c" <<'EOF'
/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

typedef unsigned long pth_t;
extern int pthread_create(pth_t *, void *, void *(*)(void *), void *);
extern int pthread_join(pth_t, void **);

/* reallocarray() overflows here, so it sets errno = ENOMEM and returns
 * without allocating: an errno store with nothing else shared around it. */
static void *errno_worker(void *a)
{
	int i;
	for (i = 0; i < 20000; i++) (void)reallocarray(0, (size_t)-1, 2);
	return a;
}

/* posix_memalign()/free() push and unlink aligned_list in malloc.c. */
static void *memalign_worker(void *a)
{
	int i;
	void *p;
	for (i = 0; i < 5000; i++)
		if (posix_memalign(&p, 4096, 64) == 0) free(p);
	return a;
}

/* strtok()'s saved pointer is static storage the standard *permits*
 * (C99 7.21.5.8p3), so this race is conforming.  It is here to show the
 * consequence: the reports name the other thread's stack buffer, because
 * one thread's strtok(NULL, ...) walks into the other thread's string. */
static void *strtok_worker(void *a)
{
	int i;
	char buf[32];
	for (i = 0; i < 5000; i++) {
		strcpy(buf, "a,b,c");
		strtok(buf, ",");
		while (strtok(0, ",")) ;
	}
	return a;
}

static void run(const char *name, void *(*fn)(void *))
{
	pth_t t1, t2;
	fprintf(stderr, "=== %s\n", name);
	pthread_create(&t1, 0, fn, 0);
	pthread_create(&t2, 0, fn, 0);
	pthread_join(t1, 0);
	pthread_join(t2, 0);
}

int main(void)
{
	/* The executable is PIE and TSan prints run-time addresses, so the
	 * script needs the load base to name the raced objects.  Publishing
	 * main's run-time address lets it subtract main's link-time address. */
	fprintf(stderr, "main-at %p\n", (void *)(unsigned long)&main);
	run("errno", errno_worker);
	run("posix_memalign/free", memalign_worker);
	run("strtok", strtok_worker);
	return 0;
}
EOF

TINC="-I$srcdir/obj/include -I$srcdir/include -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"
# Objects, not an archive, and hidden -- same reason as asan-build.sh: the
# sanitizer DSO exports weak str*/mem* interceptors that would otherwise
# satisfy those references and leave ntlibc's versions untested.
# shellcheck disable=SC2046  # deliberate word splitting of the object list
# $CC/$CFLAGS/$SAN are flag lists and must word-split.
# shellcheck disable=SC2086
$CC $SAN -g -O1 -std=c99 -nostdinc -fno-builtin -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -w \
# $CC/$CFLAGS/$SAN are flag lists and must word-split.
# shellcheck disable=SC2086
    $TINC "$OBJ/driver.c" "$OBJ/ntstubs.o" "$OBJ"/obj/*.o -lpthread -o "$OBJ/driver"

TSAN_OPTIONS="symbolize=$SYMBOLIZE halt_on_error=0 exitcode=0"
export TSAN_OPTIONS
timeout 300 "$OBJ/driver" > "$OBJ/report.txt" 2>&1 || true

nraces=$(grep -c 'WARNING: ThreadSanitizer: data race' "$OBJ/report.txt" || true)
echo "tsan: $nraces data races reported (full output in $OBJ/report.txt)"
awk '/^=== /{s=substr($0,5)} /WARNING: ThreadSanitizer: data race/{print s}' \
	"$OBJ/report.txt" | sort | uniq -c | sed 's/^/  /'

# Name the raced objects.  TSan prints run-time addresses and the executable
# is PIE, so recover the load base from the address the driver printed for
# main, then look each raced address up in the symbol table at base+offset.
# This is what identifies the variable, and it costs none of the minutes
# llvm-symbolizer spends on this binary.
echo "tsan: raced objects"
main_rt=$(sed -n 's/^main-at 0x//p' "$OBJ/report.txt" | head -1)
main_lt=$(nm "$OBJ/driver" | awk '$3=="main"{print $1; exit}')
if [ -n "$main_rt" ] && [ -n "$main_lt" ]; then
	base=$(printf '%d\n' $((0x$main_rt - 0x$main_lt)))
	nm "$OBJ/driver" | awk 'NF==3' | sort > "$OBJ/syms.txt"
	grep -oE 'at 0x[0-9a-f]+ by thread' "$OBJ/report.txt" |
	awk '{print $2}' | sort -u |
	while read -r a; do
		# $CC/$CFLAGS/$SAN are flag lists and must word-split.
		# shellcheck disable=SC2086
		off=$(printf '%016x\n' $((a - base)))
		sym=$(grep -i "^$off " "$OBJ/syms.txt" | head -1)
		echo "  $a  ${sym:-<not a named object>}"
	done
else
	echo "  (could not determine load base; see $OBJ/report.txt)"
fi
echo "tsan: this probe is expected to report races until ntlibc is made"
echo "      thread-safe; it is a description of the current state, not a gate."
