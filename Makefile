# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later

#
# Makefile for ntlibc (requires GNU make)
#
# This is the same shape as musl's Makefile: the library is a single
# static archive built from every source file under src/, plus a crt1.o
# and a handful of per-arch files under arch/$(ARCH)/.  Sources in
# arch/$(ARCH)/src/ override sources of the same name under src/.
#
# Targets:
#   all       build lib/libc.a, lib/crt1.o, lib/ntdll.def (and the wrapper)
#   install   install headers, libraries and the wrapper under $(prefix)
#   check     build test/*.c against the result and run them under wine
#   asan      build src/*.c natively under ASan+UBSan and run what applies
#   fuzz      build and run the libFuzzer harnesses in fuzz/
#   clean     remove build output
#
# Configuration is read from config.mak, written by ./configure.
#

srcdir = .
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
libdir = $(prefix)/lib
includedir = $(prefix)/include

SRC_DIRS = $(wildcard $(addprefix $(srcdir)/,src/* crt))
BASE_GLOBS = $(addsuffix /*.c,$(SRC_DIRS))
ARCH_GLOBS = $(addsuffix /$(ARCH)/*.[csS],$(SRC_DIRS)) $(srcdir)/arch/$(ARCH)/src/*.[csS]
BASE_SRCS = $(sort $(wildcard $(BASE_GLOBS)))
ARCH_SRCS = $(sort $(wildcard $(ARCH_GLOBS)))
BASE_OBJS = $(patsubst $(srcdir)/%,%.o,$(basename $(BASE_SRCS)))
ARCH_OBJS = $(patsubst $(srcdir)/%,%.o,$(basename $(ARCH_SRCS)))
REPLACED_OBJS = $(sort $(subst /$(ARCH)/,/,$(filter-out arch/%,$(ARCH_OBJS))))
ALL_OBJS = $(addprefix obj/, $(filter-out $(REPLACED_OBJS), $(sort $(BASE_OBJS) $(ARCH_OBJS))))

LIBC_OBJS = $(filter obj/src/%,$(ALL_OBJS)) $(filter obj/arch/%,$(ALL_OBJS))
CRT_OBJS = $(filter obj/crt/%,$(ALL_OBJS))

AOBJS = $(LIBC_OBJS)
GENH = obj/include/bits/alltypes.h
IMPH = $(addprefix $(srcdir)/, src/internal/libc.h src/internal/nt.h src/internal/rtlib.h)

CFLAGS_ALL = $(CFLAGS_C99FSE)
# _ALL_SOURCE (features.h turns it into _GNU_SOURCE) is for the library
# itself, not for programs: without it a TU implementing a GNU/BSD
# extension never sees its own public declaration, so a mismatch between
# the header and the definition is silent instead of a compile error.
CFLAGS_ALL += -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL
CFLAGS_ALL += -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic -Iobj/include -I$(srcdir)/include -I$(srcdir)/src/internal
CFLAGS_ALL += $(CPPFLAGS) $(CFLAGS_AUTO) $(CFLAGS)
# KERNEL32 comes from config.mak, included below; deferred (not `ifeq`,
# which is resolved at parse time, before that include runs) so it still
# sees the real value.
CFLAGS_ALL += $(if $(filter yes,$(KERNEL32)),-DNTLIBC_USE_KERNEL32)

AR      = $(CC) -ar
RANLIB  = true
INSTALL = $(srcdir)/tools/install.sh

ARCH_INCLUDES = $(wildcard $(srcdir)/arch/$(ARCH)/bits/*.h)
GENERIC_INCLUDES = $(wildcard $(srcdir)/arch/generic/bits/*.h)
INCLUDES = $(wildcard $(srcdir)/include/*.h $(srcdir)/include/*/*.h)
ALL_INCLUDES = $(sort $(INCLUDES:$(srcdir)/%=%) $(GENH:obj/%=%) $(ARCH_INCLUDES:$(srcdir)/arch/$(ARCH)/%=include/%) $(GENERIC_INCLUDES:$(srcdir)/arch/generic/%=include/%))

EMPTY_LIB_NAMES = m rt pthread crypt util xnet resolv dl
EMPTY_LIBS = $(EMPTY_LIB_NAMES:%=lib/lib%.a)
STATIC_LIBS = lib/libc.a
DEF_FILES = lib/ntdll.def
CRT_LIBS = $(addprefix lib/,$(notdir $(CRT_OBJS)))
ALL_LIBS = $(CRT_LIBS) $(STATIC_LIBS) $(EMPTY_LIBS) $(DEF_FILES)
ALL_TOOLS = obj/ntlibc-tcc

WRAPCC_TCC = $(CC)

-include config.mak

ifeq ($(WRAPPER),yes)
ALL_TOOLS_BUILT = $(ALL_TOOLS)
endif

all: $(ALL_LIBS) $(ALL_TOOLS_BUILT)

OBJ_DIRS = $(sort $(patsubst %/,%,$(dir $(ALL_LIBS) $(ALL_TOOLS) $(ALL_OBJS) $(GENH))) obj/include)

$(ALL_LIBS) $(ALL_TOOLS) $(ALL_OBJS) $(ALL_OBJS:%.o=%.lo) $(GENH): | $(OBJ_DIRS)

$(OBJ_DIRS):
	mkdir -p $@

# bits/alltypes.h is the per-arch half followed by the generic half.  Both
# halves are written in the compact TYPEDEF/STRUCT/UNION DSL in their .h.in
# form and are expanded through tools/mkalltypes.sed by
# tools/gen-alltypes.sh, at development time, into committed .h.gen files
# (`make alltypes`).  This build therefore only has to concatenate them.
#
# That split exists for the kaem bootstrap path: boot/kaem/ has to build
# this same header with nothing but mescc-tools-extra's tools, which
# include no sed and nothing else that can do mkalltypes.sed's capture-group
# rewrite -- but do include `catm`, which concatenates.  Keeping the normal
# build on the same pre-expanded files means one expansion and one source of
# truth, not sed-for-make and catm-for-kaem.  See tools/gen-alltypes.sh.
obj/include/bits/%.h: $(srcdir)/arch/$(ARCH)/bits/%.h.gen $(srcdir)/include/%.h.gen
	cat $(srcdir)/arch/$(ARCH)/bits/$(*F).h.gen $(srcdir)/include/$(*F).h.gen > $@

$(ALL_OBJS): $(GENH) $(IMPH)

# Beyond libc.h/nt.h (listed above, since every object depends on them),
# per-module private headers (dirent_internal.h, stdio_impl.h, and the
# like) are picked up automatically here rather than hand-listed: tcc's
# -MMD emits one obj/%.d per object recording the headers it actually
# included, and the -include below feeds them back in.  Without this, an
# edit to a private header used only by, say, dirent's .c files would not
# force those .o's to rebuild, and a stale .o built against the old
# struct layout would link without complaint -- silently wrong at
# runtime rather than a build error.
DEPFLAGS = -MMD -MF $(@:.o=.d)

obj/%.o: $(srcdir)/%.c
	$(CC) $(CFLAGS_ALL) $(DEPFLAGS) -c -o $@ $<

obj/%.o: $(srcdir)/%.S
	$(CC) $(CFLAGS_ALL) $(DEPFLAGS) -c -o $@ $<

obj/%.o: $(srcdir)/%.s
	$(CC) $(CFLAGS_ALL) -c -o $@ $<

lib/libc.a: $(AOBJS)
	rm -f $@
	$(AR) rcs $@ $(AOBJS)

$(EMPTY_LIBS):
	rm -f $@
	$(AR) rcs $@

lib/%.o: obj/crt/%.o
	cp $< $@

lib/ntdll.def: $(srcdir)/tools/ntdll.def
	cp $< $@

obj/ntlibc-tcc: $(srcdir)/tools/ntlibc-tcc.in config.mak
	sed -e 's!@CC@!$(WRAPCC_TCC)!g' -e 's!@PREFIX@!$(prefix)!g' -e 's!@INCDIR@!$(includedir)!g' -e 's!@LIBDIR@!$(libdir)!g' $< > $@
	chmod +x $@

$(DESTDIR)$(bindir)/%: obj/%
	$(INSTALL) -D $< $@

$(DESTDIR)$(libdir)/%: lib/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/bits/%: $(srcdir)/arch/$(ARCH)/bits/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/bits/%: $(srcdir)/arch/generic/bits/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/bits/%: obj/include/bits/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/%: $(srcdir)/include/%
	$(INSTALL) -D -m 644 $< $@

install-libs: $(ALL_LIBS:lib/%=$(DESTDIR)$(libdir)/%)

install-headers: $(ALL_INCLUDES:include/%=$(DESTDIR)$(includedir)/%)

install-tools: $(ALL_TOOLS_BUILT:obj/%=$(DESTDIR)$(bindir)/%)

install: install-libs install-headers install-tools

#
# kaem: regenerate the kaem-only bootstrap build script from this
# Makefile's own build recipe (tools/gen-kaem.sh runs `make -n -B
# lib/libc.a lib/crt1.o` and rewrites the dry-run output into kaem syntax),
# so boot/kaem/build-*.kaem can never silently drift out of sync with this
# Makefile as source files are added, removed, or renamed. See
# CONTRIBUTING.md for what boot/kaem/ is for.
#
# gen-kaem.sh regenerates *every* arch's script, not just $(ARCH)'s, so a
# new source file cannot land in one and miss the others (CI only runs this
# on one matrix leg).
#
# Deliberately unconditional rather than a file target with prerequisites:
# an mtime rule is defeated by a script that is newer than the sources but
# wrong -- e.g. one git has just written with conflict markers in it -- and
# both this target and the pre-commit hook that calls it would then quietly
# do nothing and report success.
kaem:
	./tools/gen-kaem.sh

#
# alltypes: re-expand every bits/*.h.in through tools/mkalltypes.sed into
# the committed *.h.gen files that both this Makefile and the kaem
# bootstrap consume.  Same deal as `kaem` above: generated, committed, and
# regenerated unconditionally rather than on mtimes.
#
alltypes:
	./tools/gen-alltypes.sh

#
# generated: everything that is generated *and* committed, which is what
# .githooks/pre-commit and CI regenerate before checking for drift.  One
# recipe rather than two prerequisites so the order is fixed even under
# `make -j`: gen-kaem.sh dry-runs this Makefile, whose alltypes.h rule
# names the very files gen-alltypes.sh writes.
#
generated:
	./tools/gen-alltypes.sh
	./tools/gen-kaem.sh

.PHONY: kaem alltypes generated

#
# Tests: every test/*.c is built into a PE and run under wine.  A test
# passes if it exits 0.  tests named *-win.c are built but not run: they
# need something wine does not implement (RtlCloneUserProcess, say).
#
# test/delayall.c needs a different recipe (-Wl,--delay-all, plus
# lib/delayload2.o -- see crt/delayload2.c's header comment for why
# that object is not just part of -lc) and only builds at all when
# $(CC) has the flag (DELAY_ALL, from configure's probe): excluded from
# the generic glob/pattern-rule pair below for the same reason
# rpath-plugin.c is kept out of it (see the comment by
# obj/test/rpath-plugin.dll), even though delayall.c, unlike that file,
# does have a main and is otherwise an ordinary test.
TEST_SRCS = $(filter-out $(srcdir)/test/delayall.c,$(sort $(wildcard $(srcdir)/test/*.c)))
TEST_EXES = $(patsubst $(srcdir)/test/%.c,obj/test/%.exe,$(TEST_SRCS))
TEST_RUN = $(filter-out %-win.exe,$(TEST_EXES))

obj/test/%.exe: $(srcdir)/test/%.c $(ALL_LIBS) | obj/test
	$(CC) $(CFLAGS_C99FSE) $(CFLAGS_AUTO) -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic -Iobj/include -I$(srcdir)/include -nostdlib -o $@ lib/crt1.o $< -Llib -lc -lntdll

# test/rpath.c delay-loads this DLL from its own directory ($ORIGIN)
# to exercise the real resolution path -- it links against nothing of
# ntlibc's, so it is built directly with $(CC), no crt1.o/libc.a
# involved, same as any other freestanding DLL. Kept as a plain
# prerequisite of the one test that needs it, not folded into TEST_EXES:
# it is not itself a test (no main), so test/*.c's generic pattern rule
# above must never see it -- that's also why its source lives one level
# down, in test/rpath-plugin-src/, out of the test/*.c glob entirely.
obj/test/rpath-plugin.dll: $(srcdir)/test/rpath-plugin-src/rpath-plugin.c | obj/test
	$(CC) -shared -o $@ $<

obj/test/rpath.exe: obj/test/rpath-plugin.dll

# test/delayall.c and its plugin DLL: proof that an *unmodified* program
# (plain extern, ordinary call, no ntlibc-specific macro at the call
# site) gets $ORIGIN delay loading through -Wl,--delay-all and
# __delayLoadHelper2 (crt/delayload2.c) rather than through
# include/ntlibc/delayload.h's hand-authored stubs. Only defined/built
# when DELAY_ALL is yes -- see the "delayall (skipped)" branch of
# `check` below for what happens otherwise, and configure's "checking
# whether linker accepts -Wl,--delay-all" for the probe that sets it.
ifeq ($(DELAY_ALL),yes)
obj/test/delayall-plugin.dll: $(srcdir)/test/delayall-plugin-src/delayall-plugin.c | obj/test
	$(CC) -shared -o $@ $<

# delayload2.o must come *before* -lc/-lntdll on the command line, not
# after: tcc resolves each archive's undefined symbols in a single pass
# as it reaches that archive on the command line, so an object placed
# after -lc that itself needs symbols out of libc.a (delayload2.o needs
# ntlibc_rpath_load()/_fail() and ntlibc_pe_find_export()/_dll_range())
# would already be too late (confirmed empirically -- swapping the
# order is what turns "unresolved reference to ntlibc_rpath_load" etc.
# into a clean link).
obj/test/delayall.exe: $(srcdir)/test/delayall.c $(ALL_LIBS) obj/test/delayall-plugin.dll | obj/test
	$(CC) $(CFLAGS_C99FSE) $(CFLAGS_AUTO) -Wl,--delay-all -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic -Iobj/include -I$(srcdir)/include -nostdlib -o $@ lib/crt1.o $< obj/test/delayall-plugin.dll lib/delayload2.o -Llib -lc -lntdll

TEST_EXES += obj/test/delayall.exe
# TEST_RUN is a recursively-expanded (`=`) variable, so it re-reads
# TEST_EXES -- now including delayall.exe -- every time it is expanded;
# no separate `+=` needed here, and one would double it up.
endif

obj/test:
	mkdir -p $@

check: $(TEST_EXES)
ifneq ($(DELAY_ALL),yes)
	@echo "SKIP delayall.exe (this \$$(CC) has no -Wl,--delay-all support -- see configure's probe)"
endif
	@$(srcdir)/tools/runtests.sh "$(WINE)" $(TEST_RUN)

#
# check-kernel32: convenience wrapper for a developer who already has a
# normal tree configured (TARGET/CC come from the config.mak that is
# already there) and wants to know, in one command, whether the
# --enable-kernel32 build still passes -- without having to remember the
# `./configure --enable-kernel32 && make clean && make && make check`
# sequence by hand every time. `make clean` in the middle is required,
# not cosmetic: KERNEL32 feeds CFLAGS_ALL above, and object files have no
# dependency on config.mak's *contents* (only a `-include`, which make
# does not treat as a rebuild trigger the way a prerequisite would), so a
# same-tree `./configure --enable-kernel32` followed straight by `make`
# would relink a still-kernel32-disabled lib/libc.a instead of rebuilding
# it -- confirmed locally: nm kept reporting install_ctrl_handler/
# ctrl_handler as present in the archive after a --disable-kernel32
# reconfigure with no intervening `make clean`.
#
# Restores the tree to its original KERNEL32 setting (captured from
# config.mak before this target's own ./configure runs, since $(KERNEL32)
# is resolved at parse time) and rebuilds once more, so the tree is left
# the way a plain `make` would have found it, not switched over to
# --enable-kernel32 permanently.
#
check-kernel32: config.mak
	./configure --host=$(TARGET) CC=$(CC) --enable-kernel32
	$(MAKE) clean
	$(MAKE)
	$(MAKE) check
	./configure --host=$(TARGET) CC=$(CC) --$(if $(filter yes,$(KERNEL32)),enable,disable)-kernel32
	$(MAKE) clean
	$(MAKE)

.PHONY: check-kernel32

#
# lint: opt-in static checking (gcc/clang strict warnings, the clang static
# analyzer, cppcheck, shellcheck).  Never a prerequisite of anything: the
# library is built with tcc, and tools/lint.sh only reports.  It skips any
# tool that is not installed.  See tools/lint.sh for the flag set and
# .clang-tidy for the check list.
#
lint:
	./tools/lint.sh

.PHONY: lint

# asan/fuzz: a second, native (Linux/ELF) build of the same src/*.c under
# AddressSanitizer, UBSan and libFuzzer.  This is not a substitute for
# `make check` -- it cannot be, since a native build has no ntdll -- but a
# sanitizer sees things Wine cannot, and it needs no cross toolchain.  See
# CONTRIBUTING.md and the comments in tools/asan-build.sh.
#
# Deliberately not part of `all` or `check`: they build the real thing with
# $(CC), and this builds something else with clang.
#
asan: $(GENH)
	@$(srcdir)/tools/asan-build.sh

fuzz: $(GENH)
	@$(srcdir)/tools/fuzz.sh $(FUZZ_TIME)

.PHONY: asan fuzz

clean:
	rm -rf obj lib

distclean: clean
	rm -f config.mak

.PHONY: all clean install install-libs install-headers install-tools check distclean

-include $(ALL_OBJS:.o=.d)
