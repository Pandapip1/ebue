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
#   all       build lib/libc.a, lib/crt1.o, lib/ntdll.def (and the wrapper),
#             plus obj/sh/sh.exe, the sh(1p) binary over the shell engine
#   install   install headers, libraries and the wrapper under $(prefix)
#   check     build test/*.c against the result and run them under wine
#   linkcheck prove every publicly declared function actually links
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

# sh(1p): a PE program, not part of libc.a.  Its sources live in the
# top-level sh/ directory rather than under src/ -- test/sh-design.md's
# "Placement and gates" ("its own source directory and binary -- rather
# than blurring into src/") and CONTRIBUTING.md's "Why a shell lives in a
# libc repo" both say so, and SRC_DIRS above is the mechanical reason it
# has to be true: src/* is a wildcard, so a main() under src/ would be
# archived into libc.a and fight with the main() of every program that
# links it.  The command *language* stays in src/sh/ (an internal part
# of the library, which is how wordexp/system/popen will reach it); only
# the entry point is out here.
SH_SRCS = $(sort $(wildcard $(srcdir)/sh/*.c))
SH_EXE = obj/sh/sh.exe

WRAPCC_TCC = $(CC)

-include config.mak

ifeq ($(WRAPPER),yes)
ALL_TOOLS_BUILT = $(ALL_TOOLS)
endif

# A recipe that fails *after* it has already written part of its target
# leaves that partial file behind, and make then treats it as up to date
# for ever after -- the file is newer than its prerequisites, so nothing
# ever rebuilds it, and every later `make` says "up to date" about a
# corrupt artefact.  That is not hypothetical here: it is exactly how
# `make check` comes to report `FAIL rpath.exe (rc=6)` on run after run
# (rc=6 is 0xE0DE0006 truncated to a POSIX exit status -- abort() out of
# ntlibc_rpath_fail(), i.e. the delay-loaded rpath-plugin.dll would not
# map), while every other test still passes and a fresh tree passes too.
# GNU make already removes a target whose recipe was killed by a signal;
# .DELETE_ON_ERROR extends that to a recipe that merely exits non-zero,
# which is the case it does not handle by default.  See also the atomic
# rename on the two plugin DLLs below, for the one route this cannot
# cover: make itself being SIGKILLed (a cgroup OOM kill takes the whole
# process group, so make never runs its own cleanup).
.DELETE_ON_ERROR:

all: $(ALL_LIBS) $(ALL_TOOLS_BUILT) $(SH_EXE)

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

$(DESTDIR)$(bindir)/%: obj/sh/%
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

install-progs: $(DESTDIR)$(bindir)/$(notdir $(SH_EXE))

install: install-libs install-headers install-tools install-progs

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
# sh: link the shell binary against this arch's freshly built crt1.o +
# libc.a + ntdll.def, exactly the way a test PE (or any other program) is
# linked -- there is nothing special about it, which is the point: the
# engine it calls is already in libc.a for every caller, and this adds
# only a main().  Built as one link straight from source (one file today)
# rather than through obj/%.o: the generic object rule above compiles with
# CFLAGS_ALL, i.e. -D_ALL_SOURCE -D_NTLIBC_INTERNAL -Isrc/internal, which
# is deliberately the *library's* compile environment and not a program's
# (see CFLAGS_ALL's own comment).  sh/ is a program and must build with
# what any other program gets.
#
# `sh` is .PHONY for a reason that bites otherwise: a directory named sh/
# exists, so without it make considers the target already up to date and
# does nothing.
# obj/sh gets its own order-only directory target rather than joining
# OBJ_DIRS: OBJ_DIRS is walked by the kaem bootstrap generator (it
# dry-runs `make -B lib/libc.a lib/crt1.o`, whose mkdir lines come from
# there), and boot/kaem/ builds the *library*, not this program -- an
# obj/sh in those scripts would be a directory nothing there ever writes
# into.  Same shape as obj/test below, for the same reason.
obj/sh:
	mkdir -p $@

$(SH_EXE): $(SH_SRCS) $(srcdir)/src/sh/sh.h $(ALL_LIBS) | obj/sh
	$(CC) $(CFLAGS_C99FSE) $(CFLAGS_AUTO) -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic -Iobj/include -I$(srcdir)/include -nostdlib -o $@ lib/crt1.o $(SH_SRCS) -Llib -lc -lntdll

sh: $(SH_EXE)

.PHONY: sh install-progs

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

# Capability terms for test/test-profiles.tsv's profile selectors.
#
# WHY THIS HAS A DEFAULT AT ALL.  These are *declarations*, not
# measurements: tools/testlib.py's Rule.matches() only ever compares a
# selector against the terms passed in with --profile, and nothing in
# this tree probes for a symlink or a console.  So a rule written
# `capability.console=no` applies exactly when someone says so, and
# never otherwise.
#
# Until this default existed, the terms lived only in .github/workflows/
# ci.yml, which meant a plain local `make check-pedantic` passed none of
# them, the exempting rules could not match, and the base disposition got
# probed instead.  That is not an environment difference or a flake --
# it made two cases report STALE deterministically, for every local run,
# for everyone.  Measured back to back on one tree:
#
#     without:  52 BUG, 45 NA, 2 policy failure(s)
#     with:     50 BUG, 47 NA, 0 policy failure(s)
#
# exactly the two cases the rules name, moving as the rules specify.
#
# WHAT WOULD FALSIFY EACH, since nothing here checks:
#   capability.symlink=no  a runner where symlink()/symlinkat() can
#                          build a loop -- i.e. real Windows with
#                          SeCreateSymbolicLinkPrivilege, or Developer
#                          Mode.  Wine's is not that.
#   capability.console=no  a runner whose stdin is a real console input
#                          queue rather than a pipe or NUL, so that
#                          tcflush(TCIFLUSH) has a queue to discard.
#                          Any interactive run is that.
#
# So this default describes a headless CI-shaped runner, which is what
# both this project's Wine legs and its windows-test legs are.  Override
# it on the command line if yours is not:
#     make check-pedantic TEST_PROFILE="capability.console=yes"
# check-pedantic prints any capability term the rules use and the
# profile leaves unset, so a third term added later cannot go missing
# the way these two did.
TEST_PROFILE ?= capability.symlink=no capability.console=no

# TEST_DEPFLAGS is the same mechanism DEPFLAGS provides for library
# objects, applied to the test binaries -- which compile and link in one
# step, so the depfile is named from the .exe rather than from a .o.
#
# Without it a test binary depended on its own .c file and the libraries
# and on NOTHING ELSE: not <tar.h>, not <stdio.h>, not the generated
# obj/include/bits/alltypes.h.  Editing a header and re-running `make
# obj/test/foo.exe` rebuilt nothing and silently ran the previous
# binary.  That is the same class of defect the DEPFLAGS comment above
# describes for private headers -- "a stale .o ... would link without
# complaint" -- and it bit for real: two mutation runs against
# include/tar.h came back falsely clean because the mutated header was
# never compiled in.
#
# It matters most for exactly the work that touches headers, where the
# alternative is remembering to delete the target by hand every time.
TEST_DEPFLAGS = -MMD -MF $(@:.exe=.d)

obj/test/%.exe: $(srcdir)/test/%.c $(ALL_LIBS) | obj/test
	$(CC) $(CFLAGS_C99FSE) $(CFLAGS_AUTO) $(TEST_DEPFLAGS) -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic -Iobj/include -I$(srcdir)/include -nostdlib -o $@ lib/crt1.o $< -Llib -lc -lntdll

# test/rpath.c delay-loads this DLL from its own directory ($ORIGIN)
# to exercise the real resolution path -- it links against nothing of
# ntlibc's, so it is built directly with $(CC), no crt1.o/libc.a
# involved, same as any other freestanding DLL. Kept as a plain
# prerequisite of the one test that needs it, not folded into TEST_EXES:
# it is not itself a test (no main), so test/*.c's generic pattern rule
# above must never see it -- that's also why its source lives one level
# down, in test/rpath-plugin-src/, out of the test/*.c glob entirely.
#
# Written to a temporary and renamed into place, not straight to $@.
# rename(2) is atomic, so nothing can ever observe a half-written DLL:
# not a `make check` running rpath.exe out of the same obj/ while this
# link is in flight, and -- the case .DELETE_ON_ERROR above cannot cover
# -- not a later run either, after a cgroup OOM kill SIGKILLs the whole
# process group and make never gets to delete its own partial output.
# This file earns the extra care the other build outputs do not: it is
# the only one consumed at *run* time, by the NT loader inside another
# program, so a corrupt one is not caught by a link that follows.  It
# surfaces only as `FAIL rpath.exe (rc=6)` -- abort() out of
# ntlibc_rpath_fail() -- on every subsequent run, with make insisting
# the DLL is up to date the whole time.  Same for delayall-plugin.dll.
obj/test/rpath-plugin.dll: $(srcdir)/test/rpath-plugin-src/rpath-plugin.c | obj/test
	$(CC) -shared -o $@.tmp $< && mv -f $@.tmp $@

obj/test/rpath.exe: obj/test/rpath-plugin.dll

# test/sh-main.c is the black-box test of the sh binary: it spawns
# obj/sh/sh.exe as a real process (test/sh-engine.c, by contrast, links the
# engine directly and never involves a second image).  It finds the exe
# by walking up from its own argv[0] -- see that file's header -- so the
# only wiring needed here is making sure the exe exists before the test
# runs.
obj/test/sh-main.exe: $(SH_EXE)

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
	$(CC) -shared -o $@.tmp $< && mv -f $@.tmp $@

# delayload2.o must come *before* -lc/-lntdll on the command line, not
# after: tcc resolves each archive's undefined symbols in a single pass
# as it reaches that archive on the command line, so an object placed
# after -lc that itself needs symbols out of libc.a (delayload2.o needs
# ntlibc_rpath_load()/_fail() and ntlibc_pe_find_export()/_dll_range())
# would already be too late (confirmed empirically -- swapping the
# order is what turns "unresolved reference to ntlibc_rpath_load" etc.
# into a clean link).
obj/test/delayall.exe: $(srcdir)/test/delayall.c $(ALL_LIBS) obj/test/delayall-plugin.dll | obj/test
	$(CC) $(CFLAGS_C99FSE) $(CFLAGS_AUTO) $(TEST_DEPFLAGS) -Wl,--delay-all -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic -Iobj/include -I$(srcdir)/include -nostdlib -o $@ lib/crt1.o $< obj/test/delayall-plugin.dll lib/delayload2.o -Llib -lc -lntdll

TEST_EXES += obj/test/delayall.exe
# TEST_RUN is a recursively-expanded (`=`) variable, so it re-reads
# TEST_EXES -- now including delayall.exe -- every time it is expanded;
# no separate `+=` needed here, and one would double it up.
endif

obj/test:
	mkdir -p $@

# test-exes: build every test PE, run none of them.  Same prerequisite
# list `check` has, minus the running -- so it also drags in the plugin
# DLLs (obj/test/rpath-plugin.dll, and delayall-plugin.dll where
# DELAY_ALL is yes), which are prerequisites of the .exe that loads
# them rather than members of TEST_EXES.
#
# Exists for CI: .github/workflows/ci.yml's `build-test-exes` job
# produces the binaries that both the Wine leg and the real-Windows leg
# consume, and must not run anything itself (it has no Wine installed,
# and the point of the split is that the real-Windows leg stops waiting
# on the Wine run).  Nothing stops a developer using it to check that
# every test still *compiles* without sitting through the run.
test-exes: $(TEST_EXES)

.PHONY: test-exes

check: $(TEST_EXES)
ifneq ($(DELAY_ALL),yes)
	@echo "NA delayall.exe (this \$$(CC) has no -Wl,--delay-all support -- see configure's probe)"
endif
	@$(srcdir)/tools/test-policy.py check --profile runtime=wine $(foreach profile,$(TEST_PROFILE),--profile $(profile))
	@$(srcdir)/tools/run-tests.py --runner "$(WINE)" \
		--profile runtime=wine --profile target_arch=$(ARCH) \
		--profile kernel32=$(KERNEL32) $(TEST_RUN)

# Probe every source-level disposition independently.  Pedantic validates
# that BUG still compiles and fails and that UNIMPL still fails to link;
# strict performs those same probes and additionally rejects BUG and UNIMPL
# fences, while still allowing genuinely inapplicable N/A cases.
check-pedantic: check
	@WINE="$(WINE)" $(srcdir)/tools/test-policy.py pedantic $(foreach profile,$(TEST_PROFILE),--profile $(profile))

check-strict: check
	@WINE="$(WINE)" $(srcdir)/tools/test-policy.py strict $(foreach profile,$(TEST_PROFILE),--profile $(profile))

.PHONY: check check-pedantic check-strict

#
# libc-test: musl's own regression corpus (third_party/libc-test,
# a git submodule pinned at a SHA -- see third_party/README.md),
# adjudicated against test/libc-test-expected.txt.
#
# A separate target from `check`, not extra entries in TEST_SRCS: these
# are not this project's tests.  They are somebody else's, 39% of them
# do not compile here, 27 of the rest fail, and every one of those
# outcomes has to be adjudicated against a ledger rather than reduced to
# a single exit status.  tools/libc-test.sh does that; see its header for
# the rc contract and for why the expected-failure ledger cannot grow
# without saying so.
#
# Depends on $(ALL_LIBS) like linkcheck does: it links 146 PEs against
# lib/libc.a and needs it to exist, and, like `check`, it only ever sees
# the arch config.mak currently names.
#
libc-test: $(ALL_LIBS)
	@WINE="$(WINE)" NTLIBC_TEST_MODE=normal NTLIBC_TEST_PROFILE="$(TEST_PROFILE)" $(srcdir)/tools/libc-test.sh

libc-test-pedantic: $(ALL_LIBS)
	@WINE="$(WINE)" NTLIBC_TEST_MODE=pedantic NTLIBC_TEST_PROFILE="$(TEST_PROFILE)" $(srcdir)/tools/libc-test.sh

libc-test-strict: $(ALL_LIBS)
	@WINE="$(WINE)" NTLIBC_TEST_MODE=strict NTLIBC_TEST_PROFILE="$(TEST_PROFILE)" $(srcdir)/tools/libc-test.sh

.PHONY: libc-test libc-test-pedantic libc-test-strict

#
# posix-optsrun: the Open POSIX Test Suite (third_party/ltp's
# testcases/open_posix_testsuite/, a git submodule pinned at a SHA -- see
# third_party/README.md), adjudicated case by case.
#
# `posix-optsrun-pedantic` checks all 1610 profile dispositions: PASS and
# BUG cases compile and run, UNIMPL cases must fail compilation, NA cases
# stay out, and FLAKY cases remain observable without weakening strict.
#
# This absorbed the separate `posix-gapmap` compile-only census, which
# measured the same 1610 cases and gated four AGGREGATE invariants over
# them -- a census, a partition, floors in both directions, and two
# canaries.  Every one of those is a weaker restatement of what
# test/posix-opts-expected.txt now says per case:
#
#   census + partition   tools/posix-opts.py refuses to run unless the
#                        discovered sources number exactly CENSUS *and*
#                        the discovered set equals the annotated set, so
#                        an unannotated or stale case is a hard error
#                        rather than a count that still adds up.
#   floor (links)        358 PASS + 199 BUG cases must build and run.  A
#                        compiler that stopped finding lib/libc.a fails
#                        557 cases by name, not one threshold.
#   floor (blocked)      1016 UNIMPL cases must FAIL to build.  The
#                        dangerous direction -- an -I that starts
#                        pointing at a host libc, making everything link
#                        and the gap read as closed -- is now caught 1016
#                        times over instead of by a single floor.
#   canaries             two hand-picked cases were a proxy for "the
#                        classifier still discriminates".  The ledger
#                        checks all 1610 in both directions.
#
# The class A/B/C classification the gap map produced did not go away
# either: it is recorded per case in the ledger's reason column ("OPTS
# class A: compile/link blocked by aio.h"), where it names the header
# instead of contributing to a bucket total.
#
# Also deliberately NOT part of `check`, and for the same reason:
# `check` is this library's own suite and its failures are ours.  A
# foreign conformance suite that this library fails a third of is a
# different claim with a different meaning of failure, exactly as
# libc-test is kept separate.
#
# The exact case census and complete profile ledger prevent an empty or
# partial sweep from reporting success. There is no generated report: the
# driver prints every case and its observation to stdout, so the run log
# is the record.
#
# Depends on $(ALL_LIBS) for the same reason libc-test does: without
# lib/libc.a nothing links, nothing runs, and the run reports "no
# failures" about a sweep of zero tests.
#
posix-optsrun: $(ALL_LIBS)
	@WINE="$(WINE)" $(srcdir)/tools/posix-opts.py normal $(foreach profile,$(TEST_PROFILE),--profile $(profile))

posix-optsrun-pedantic: $(ALL_LIBS)
	@WINE="$(WINE)" $(srcdir)/tools/posix-opts.py pedantic $(foreach profile,$(TEST_PROFILE),--profile $(profile))

posix-optsrun-strict: $(ALL_LIBS)
	@WINE="$(WINE)" $(srcdir)/tools/posix-opts.py strict $(foreach profile,$(TEST_PROFILE),--profile $(profile))

.PHONY: posix-optsrun posix-optsrun-pedantic posix-optsrun-strict

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
# install-check: prove that `make install` produces something an outside
# program can actually build and run against, rather than something only
# in-tree gates (which never leave -I./include -I./arch/$(ARCH)
# -Iobj/include -Llib) can see. Configures and builds a second, throwaway
# copy of the tree out-of-tree, installs it into a temporary prefix, and
# builds/runs test programs against *only* that prefix through the
# installed tools/ntlibc-tcc wrapper -- no source-tree path anywhere on
# the compile line. See tools/install-check.sh for the full rationale and
# what it checks. Requires config.mak (same as `check`): run it once per
# configured arch, the same way `make check` is.
#
install-check: config.mak
	./tools/install-check.sh

.PHONY: install-check

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

# linkcheck: prove every function a public header declares can actually be
# linked against by a real program, using the real $(CC) and this arch's
# freshly built lib/crt1.o + lib/libc.a + lib/ntdll.def -- the same inputs
# `make check`'s test binaries link against.  This is deliberately NOT a
# stage of `make lint`: lint.sh's tools (gcc/clang/cppcheck/shellcheck)
# never touch $(CC) or produce a real PE link, so they cannot see a macro
# that silently retargets a call to a name tcc cannot resolve -- exactly
# how include/alloca.h's `#define alloca __builtin_alloca` shipped (see
# tools/linkcheck.sh's header for the full story). Catching that needs an
# actual per-arch compile-and-link with the real toolchain, which makes
# this the same shape of gate as `check`/`asan`, not `lint`: a build-and-
# run step keyed to $(CC)/$(ARCH), not a report-only static pass. Like
# `check`, it only ever sees the arch config.mak currently names -- run it
# again after reconfiguring for the other arch (arch/ is not shared,
# include/ is).
linkcheck: $(ALL_LIBS)
	@CC="$(CC)" ARCH="$(ARCH)" CFLAGS_C99FSE="$(CFLAGS_C99FSE)" CFLAGS_AUTO="$(CFLAGS_AUTO)" \
		$(srcdir)/tools/linkcheck.sh

.PHONY: linkcheck
#
# hygiene: every header under include/ (plus the generated obj/include/
# bits/alltypes.h) compiled entirely on its own, twice in one TU (include
# guard), and as C++ where it promises extern "C" -- for both arches,
# under -std=c99 -nostdinc with no feature-test macro set, i.e. exactly
# what a program gets from #include <whatever.h> and nothing else. See
# tools/hdr-hygiene.sh's header comment for the bug class this exists to
# catch (ntlibc shipping no <pwd.h> at all, unnoticed because nothing
# in-tree ever included a header ntlibc does not have -- this script is
# the "header exists but isn't independently usable" half of that class).
#
# A separate gate rather than a tools/lint.sh stage: lint.sh is
# explicitly report-only static analysis over src/*.c (warnings, the
# analyzer, cppcheck) and is documented as never a build dependency,
# where this is a pass/fail correctness check over include/*.h with its
# own exit status -- closer in kind to `make asan`/`make check` than to
# a linter, and folding it into lint.sh's stage list would let
# LINT_STRICT=0/report-only framing quietly cover a class of bug that
# should instead just fail the gate.
#
# Deliberately depends on $(GENH), like asan below, so a bare `make
# hygiene` in a fresh tree also has obj/include/bits/alltypes.h.
#
hygiene: $(GENH)
	./tools/hdr-hygiene.sh

.PHONY: hygiene

# minver: the library's minimum supported Windows version, checked rather
# than asserted -- tools/ntdll.def's per-export NTDLL-version annotations
# against the floor README.md declares. Like `ledger`, a grep over two
# checked-in artefacts with no build and no $(CC), so it has no
# prerequisite and runs instantly. Deliberately not a `lint` stage:
# lint.sh is report-only static analysis, and this is pass/fail. See
# tools/lint-minver.sh's header for the bug class (a static import of a
# name the running ntdll does not export makes the loader refuse the
# whole image) and for why neither Wine nor CI's Server 2025 can see it.
minver:
	./tools/lint-minver.sh

.PHONY: minver

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

# Wall-clock seconds per harness, matching tools/fuzz.sh's own `${1:-60}`
# default so the two cannot drift.  It was referenced here and defined
# nowhere, which read as a configured knob and was not one: a `make fuzz`
# passed an empty argument and the real default lived, undocumented, in
# the script.  `?=` so a command-line or environment FUZZ_TIME still
# wins, which is how it was always meant to be used:
#
#   make fuzz FUZZ_TIME=300
#
# Deliberately unquoted below: this is a plain number, and quoting it
# would turn an explicitly empty FUZZ_TIME= into an empty first argument
# (-max_total_time= with nothing after it) instead of no argument at all.
FUZZ_TIME ?= 60

fuzz: $(GENH)
	@$(srcdir)/tools/fuzz.sh $(FUZZ_TIME)

.PHONY: asan fuzz

clean:
	rm -rf obj lib

distclean: clean
	rm -f config.mak

.PHONY: all clean install install-libs install-headers install-tools check distclean

-include $(ALL_OBJS:.o=.d)
# The same, for the test binaries (see TEST_DEPFLAGS).  Absent on a
# clean tree, which is harmless: the first build creates them.
-include $(TEST_EXES:.exe=.d)
