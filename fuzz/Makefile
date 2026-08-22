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

HARNESSES = strtod printf scanf utf
EXES = $(HARNESSES:%=$(OUT)/fuzz_%)

all: $(EXES)

$(LIBDIR)/compiled.txt:
	NTLIBC_ASAN_OBJ=$(abspath $(LIBDIR)) \
	NTLIBC_ASAN_EXTRA=-fsanitize=fuzzer-no-link \
	$(srcdir)/tools/asan-build.sh --objects-only

$(OUT):
	mkdir -p $(OUT)

$(OUT)/host_oracle.o: host_oracle.c | $(OUT)
	$(CC) $(CFLAGS_ORACLE) -c $< -o $@

$(OUT)/ntstubs.o: ntstubs.c $(LIBDIR)/compiled.txt | $(OUT)
	$(CC) $(CFLAGS_HARNESS) -c $< -o $@

$(OUT)/fuzz_%: fuzz_%.c $(OUT)/host_oracle.o $(OUT)/ntstubs.o $(LIBDIR)/compiled.txt | $(OUT)
	$(CC) $(CFLAGS_HARNESS) -shared-libasan $(RPATH) \
	  $(ASAN_SO) $< $(OUT)/ntstubs.o $(OUT)/host_oracle.o \
	  $(wildcard $(LIBDIR)/obj/*.o) -ldl -lm -o $@

run: all
	@rc=0; \
	for h in $(HARNESSES); do \
	   echo "== fuzz_$$h ($(FUZZ_TIME)s)"; \
	   LD_PRELOAD=$(ASAN_SO) ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
	   $(OUT)/fuzz_$$h -max_total_time=$(FUZZ_TIME) -max_len=256 \
	     -print_funcs=0 -print_final_stats=1 \
	   || { echo "   fuzz_$$h FOUND SOMETHING"; rc=1; }; \
	 done; exit $$rc

clean:
	rm -rf $(OUT) $(LIBDIR)

.PHONY: all run clean
