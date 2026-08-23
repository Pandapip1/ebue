# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# libFuzzer harnesses for ntlibc.  See ../CONTRIBUTING.md.
#
#   make -C fuzz            build every harness
#   make -C fuzz run        build and run each for $(FUZZ_TIME) seconds
#   make -C fuzz clean
#
# or, from the top: `make fuzz` / `tools/fuzz.sh`.
#
# Every harness links the *real* src/*.c.  Nothing here is a copy of
# library code, and nothing here may become one: a harness that tests a
# transcription of printf.c stops testing printf.c the moment somebody
# edits printf.c.
#
# The library objects are built by ../tools/asan-build.sh, the same script
# `make asan` uses, so both are built from one mechanically derived file
# list.  They are rebuilt here rather than reused because libFuzzer needs
# the library itself instrumented for coverage (-fsanitize=fuzzer-no-link);
# without that the fuzzer gets no feedback from the code under test and is
# little better than random input.

srcdir  = ..
CC      = clang
ARCH   ?= x86_64
LIBDIR  = $(srcdir)/obj/fuzzlib
OUT     = $(srcdir)/obj/fuzz
FUZZ_TIME ?= 60

SAN  = -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined
INC  = -I$(srcdir)/src/internal -I$(srcdir)/obj/include -I$(srcdir)/include \
       -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic
CFLAGS_HARNESS = $(SAN) -g -O1 -std=c99 -nostdinc -fno-builtin -w \
                 -D_XOPEN_SOURCE=700 -D_NTLIBC_INTERNAL $(INC)
# host_oracle.c is the one file built against the host headers.
CFLAGS_ORACLE  = -fsanitize=address,undefined -g -O1 -w
ASAN_SO = $(shell $(CC) -print-file-name=libclang_rt.asan-$(ARCH).so)
RPATH   = -Wl,-rpath,$(dir $(ASAN_SO))
# $(ASAN_SO) is named explicitly, and first: the shared ASan runtime has to
# be the first entry in the executable's library list or it refuses to
# start ("ASan runtime does not come first in initial library list").  The
# driver appends it after -ldl/-lm, which is too late.

HARNESSES = strtod printf scanf utf path strptime strtol strftime
EXES = $(HARNESSES:%=$(OUT)/fuzz_%)

# What the instrumented library is built from.  Derived with find, never
# written out by hand: a hand-listed prerequisite set stops mentioning the
# file somebody adds tomorrow, and the harness would then be fuzzing a
# library that no longer matches src/.  The header sweep is deliberately
# coarse -- every header the sanitized build can see, not the ones a given
# .c happens to include -- because asan-build.sh is all-or-nothing anyway
# (it starts with `rm -rf $OBJ`), so a finer set would buy nothing.
LIBSRC := $(shell find $(srcdir)/src -name '*.c' 2>/dev/null | sort)
LIBHDR := $(shell find $(srcdir)/src $(srcdir)/include $(srcdir)/arch \
                       $(srcdir)/obj/include -name '*.h' 2>/dev/null | sort)
LIBDEP  = $(LIBSRC) $(LIBHDR) $(srcdir)/tools/asan-build.sh ntstubs.c

all: $(EXES)

# The stamp, not compiled.txt, is the target.  compiled.txt is written
# line by line as the loop compiles, so a run that dies in the middle
# leaves one behind that is newer than every source and describes only
# half a library -- exactly the newer-but-wrong file that defeats an mtime
# rule (see the comment above `kaem:` in the top-level Makefile).  The
# stamp is touched only after the script exits 0, and asan-build.sh's own
# `rm -rf $OBJ` removes the previous one before anything else, so a failed
# or interrupted build leaves no stamp and the next make rebuilds.
#
# The rebuild is all 202 objects, because that is the granularity the
# script offers; it costs about 7s (and 2s more to relink the four
# harnesses), and happens only when a source, a header or the script
# itself has actually changed.  An unconditional rebuild would be
# defensible at that price, but it is not needed: unlike `make kaem`
# there is no committed artefact here that a newer-but-wrong file could
# corrupt -- the inputs are the same files the script itself reads, and
# the only way to fool the stamp is to `touch` it by hand.
$(LIBDIR)/.built: $(LIBDEP)
	NTLIBC_ASAN_OBJ=$(abspath $(LIBDIR)) \
	NTLIBC_ASAN_EXTRA=-fsanitize=fuzzer-no-link \
	$(srcdir)/tools/asan-build.sh --objects-only
	touch $@

$(OUT):
	mkdir -p $(OUT)

$(OUT)/host_oracle.o: host_oracle.c | $(OUT)
	$(CC) $(CFLAGS_ORACLE) -c $< -o $@

$(OUT)/ntstubs.o: ntstubs.c $(LIBHDR) $(LIBDIR)/.built | $(OUT)
	$(CC) $(CFLAGS_HARNESS) -c $< -o $@

# The library objects are globbed by the shell, at recipe time.  $(wildcard)
# would be expanded by make against a directory it may have cached before
# the stamp rule refilled it, and an empty expansion links a harness with
# no ntlibc in it at all.
$(OUT)/fuzz_%: fuzz_%.c $(LIBHDR) $(OUT)/host_oracle.o $(OUT)/ntstubs.o $(LIBDIR)/.built | $(OUT)
	$(CC) $(CFLAGS_HARNESS) -shared-libasan $(RPATH) \
	  $(ASAN_SO) $< $(OUT)/ntstubs.o $(OUT)/host_oracle.o \
	  $(LIBDIR)/obj/*.o -ldl -lm -o $@

# Leak detection on; see tools/asan-build.sh for why it can be trusted here.
LEAKS ?= 1

run: all
	@rc=0; \
	for h in $(HARNESSES); do \
	   echo "== fuzz_$$h ($(FUZZ_TIME)s)"; \
	   LD_PRELOAD=$(ASAN_SO) ASAN_OPTIONS=detect_leaks=$(LEAKS) UBSAN_OPTIONS=print_stacktrace=1 \
	   $(OUT)/fuzz_$$h -max_total_time=$(FUZZ_TIME) -max_len=256 \
	     -print_funcs=0 -print_final_stats=1 \
	   || { echo "   fuzz_$$h FOUND SOMETHING"; rc=1; }; \
	 done; exit $$rc

clean:
	rm -rf $(OUT) $(LIBDIR)

.PHONY: all run clean

# A half-written object or executable left behind by a failed compile is
# newer than its sources, so make would consider it up to date next time.
.DELETE_ON_ERROR:
