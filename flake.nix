# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "ntlibc build and lint tooling";

  # This flake exists to replace tribal knowledge, not to add to it: every
  # package here was pulled directly from the `nix shell nixpkgs#... --command
  # ...` combinations that tools/lint.sh, .github/workflows/ci.yml, and
  # tools/gate.sh already document/require, not guessed at. See the comment
  # above `versionedLlvm18` below for the one nontrivial wrinkle (Debian-style
  # `clang-18`-shaped binary names that nixpkgs does not itself provide).

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs = inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];

      perSystem = { pkgs, ... }:
        let
          llvm18 = pkgs.llvmPackages_18;

          # tools/lint.sh's Z3-backed stages (sizearith, totality, arithub,
          # ownership, initproof, fallible, provenance, locks, lockset,
          # abizeroinit, reentrancy, variadic, signals, errno, purity,
          # undefined, unreferenced) all `require_tool clang-18` /
          # `clang++-18` / `llvm-config-18`, and CI's own `analyze` leg
          # pins CLANG_TIDY=clang-tidy-18 (.github/workflows/ci.yml's
          # `lint` job matrix installs exactly those Debian package names
          # from ubuntu-24.04's apt, where llvm-18-dev and libclang-18-dev
          # share one /usr/lib/llvm-18 prefix). Nixpkgs does not name its
          # LLVM 18 binaries that way, and keeps LLVM's and Clang's
          # headers/shared libraries in separate per-package dev/lib
          # outputs rather than one shared prefix -- so besides the name
          # mismatch, `clang++-18 $(llvm-config-18 --cxxflags) ...` as
          # tools/lint.sh writes it can't otherwise find clang/AST/Expr.h,
          # and `find "$(llvm-config-18 --libdir)" -name
          # 'libclang-cpp.so.18*'` (also tools/lint.sh, verbatim) can't find
          # the shared library the built plugin loads into. This lets
          # lint.sh stay exactly as written: a small symlink farm supplies
          # the missing names, clang-unwrapped.dev goes on CPATH for the
          # missing headers below, and this wrapper answers --libdir with
          # clang-unwrapped.lib's directory (where libclang-cpp.so.18
          # actually lives) instead of llvm-config's own.
          #
          # llvmPackages_18.clang (aliased nixpkgs#clang_18), not
          # .clang-unwrapped, is the source for clang-18/clang++-18: it is
          # cc-wrapper-mediated, so -resource-dir and the C/C++ standard
          # library header search path it needs to compile
          # tools/clang/*.cpp (an ordinary hosted C++ program, unlike the
          # -nostdinc freestanding ntlibc sources cppflags_for() feeds it)
          # are already handled the same way they would be by a Debian
          # clang-18 package -- clang-unwrapped on its own knows neither.
          versionedLlvm18 = pkgs.runCommand "llvm18-versioned-names" { } ''
            mkdir -p "$out/bin"
            ln -s ${llvm18.clang}/bin/clang "$out/bin/clang-18"
            ln -s ${llvm18.clang}/bin/clang++ "$out/bin/clang++-18"
            # clang-tidy itself, not clang-tidy-unwrapped's caller wrapper:
            # that wrapper script re-derives its own target name from
            # "$(basename $0)-unwrapped", so invoking it under a symlink
            # named clang-tidy-18 makes it look for a nonexistent
            # clang-tidy-18-unwrapped. The wrapper's only job is injecting
            # Nix's libc/libc++ header search paths, which tools/lint.sh
            # never needs -- cppflags_for() in that script always passes
            # -nostdinc and its own explicit -I set, so going straight to
            # the unwrapped binary changes nothing this project's lint
            # stages rely on.
            ln -s ${llvm18.clang-tools}/bin/clang-tidy-unwrapped "$out/bin/clang-tidy-18"

            cat > "$out/bin/llvm-config-18" <<EOF
#!/bin/sh
# See versionedLlvm18's comment in flake.nix for why --libdir is
# special-cased instead of just symlinking straight to llvm-config.
if [ "\$#" = 1 ] && [ "\$1" = --libdir ]; then
	echo "${llvm18.clang-unwrapped.lib}/lib"
	exit 0
fi
exec ${llvm18.llvm.dev}/bin/llvm-config "\$@"
EOF
            chmod +x "$out/bin/llvm-config-18"
          '';
        in
        {
          devShells.default = pkgs.mkShell {
            packages = [
              # Linux/aarch64 build (tools/lint.sh's own arch table) and the
              # NT/tcc build: both need GNU make. tcc itself is a local
              # install (e.g. /home/*/tinycc-install/bin on this project's
              # own dev machines, built from the pinned commit
              # .github/workflows/ci.yml's TINYCC_SHA names) -- it is not
              # in nixpkgs under any name this project uses, and this shell
              # deliberately does not assume that path exists. Point CC at
              # your own tcc build to do an NT build from this shell.
              pkgs.gnumake

              # A plain, unversioned clang: stage_warn's pick_cc() falls
              # back to bare `clang`/`gcc` per-arch, the native aarch64
              # Linux build uses it directly, and it is what
              # .github/workflows/ci.yml's `linux-builds` and `asan` jobs
              # install. lld and qemu-user round out cross-build
              # verification (tools/linux-build-*-cross.sh: clang
              # --target=..., -fuse-ld=lld, then run the cross binary under
              # qemu-x86_64/qemu-i386).
              pkgs.clang
              pkgs.lld
              pkgs.qemu-user

              # binutils' `nm` is what tools/lint-unreferenced.sh reads
              # (`nm --undefined-only`) to find which declared/defined
              # functions no test/*.c references; ar/objcopy/etc. ride
              # along in the same closure.
              pkgs.binutils

              # The Z3-backed analyzer-plugin lint stages: a real clang 18 +
              # LLVM 18 (headers/libs, for building tools/clang/*.cpp as
              # analyzer plugins), clang-tidy 18 (the `analyze` stage), and
              # Z3's library and development headers (pkg-config discovers
              # them via z3.pc, which z3.dev carries). clang-unwrapped.dev
              # and .lib supply clang's own AST/Analysis headers and
              # libclang-cpp.so.18, on top of the versioned names from
              # versionedLlvm18 above -- see its comment for why both are
              # needed.
              llvm18.clang-unwrapped.dev
              llvm18.clang-unwrapped.lib
              llvm18.llvm.dev
              llvm18.clang-tools
              versionedLlvm18
              pkgs.pkg-config
              pkgs.z3
              pkgs.z3.dev

              # tools/lint-*.py (every Z3-backed stage's Python-side proof
              # checker) and the pre-commit hook (.githooks/pre-commit,
              # which shells out to tools/test-policy.py and
              # tools/gen-alltypes.sh) both need python3 on PATH; make is
              # already covered above.
              pkgs.python3

              # tools/lint.sh's remaining stages: cppcheck and shellcheck.
              pkgs.cppcheck
              pkgs.shellcheck

              # Pushing changes up.
              pkgs.gh
            ];

            # clang-unwrapped.dev carries clang/AST/Expr.h and friends --
            # the plugin API tools/clang/*.cpp is written against -- which
            # is project-specific and no cc-wrapper adds on its own.
            # clang-unwrapped.lib carries libclang-cpp.so.18, which the
            # built plugins link against and clang-18 dlopen()s at analysis
            # time; it needs to be findable at runtime, not just link time.
            shellHook = ''
              export CPATH="${llvm18.clang-unwrapped.dev}/include''${CPATH:+:$CPATH}"
              export LD_LIBRARY_PATH="${llvm18.clang-unwrapped.lib}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
              echo "ntlibc dev shell: gnumake, clang/lld/qemu-user, the clang-18/llvm-18/z3 lint toolchain, python3, cppcheck, shellcheck, gh." >&2
              echo "tcc (the NT/win32 build's actual compiler) is not provided here -- point CC at your own tinycc-install build." >&2
            '';
          };
        };
    };
}
