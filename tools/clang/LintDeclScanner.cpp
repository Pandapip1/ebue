// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LintDeclScanner -- a real AST walk, replacing tools/lint-decls.awk's
// hand-rolled character-at-a-time scanner for "what functions does this
// file declare (MODE=decl) or define (MODE=def)".
//
// tools/clang/DeclScanner.cpp already replaced this exact class of scanner
// for tools/linkcheck.sh (see its own header comment for the full story:
// `// NOLINTBEGIN(...)` comments and ownership.h's `withtok()` prefix
// attribute both defeated the awk's "first identifier before '('"
// heuristic). tools/lint-decls.awk is a *different*, independently
// hand-written implementation of the identical idea -- shared between
// tools/lint-undefined.sh and tools/lint-unreferenced.sh -- with the
// identical two blind spots: it only strips /* */ comments, never //
// ones, and it uses the same naive name heuristic. The failure is not
// theoretical: `awk -v MODE=def -f tools/lint-decls.awk src/stdlib/abs.c`
// prints "NOLINTBEGIN<TAB>11" instead of "abs<TAB>11", because the `//
// NOLINTBEGIN(misc-include-cleaner)` banner most .c files carry near
// their top gets absorbed into the accumulating declarator buffer and
// mis-attributes the next real definition to a bogus "NOLINTBEGIN" entry
// -- which is why tools/lint-undefined.sh currently misreports abs(),
// bsearch(), div(), ecvt(), mblen() and others as "declared but never
// defined" even though they are genuinely defined and archived into
// lib/libc.a.
//
// This is a distinct tool from DeclScanner.cpp, not an extra mode bolted
// onto it, because the two have genuinely different output contracts:
// DeclScanner.cpp reports (name, header, fixed_argc, undefined_ok) --
// linkcheck.sh needs the argument count to synthesize a call, and prints
// the header path back verbatim so its declfile format needs no
// changes. Neither lint-undefined.sh nor lint-unreferenced.sh needs an
// argument count at all, and both already know which file they are
// scanning (they build the file:line themselves), so bolting those two
// extra, unused columns onto every line here would just be dead weight.
// What this tool's two callers *do* share with tools/lint-decls.awk is
// its MODE=decl/MODE=def split and its "name<TAB>line" per-declaration
// output shape, so this mirrors that contract as closely as a real AST
// walk allows, rather than inventing a third shape.
//
// Usage: run once per file, in C mode, with the same CFLAGS a real
// compile of that file would use (see tools/lint-undefined.sh and
// tools/lint-unreferenced.sh for what each picks), e.g.:
//
//   clang-18 -std=c99 -fsyntax-only $CFLAGS \
//     -Xclang -load -Xclang ntlibc-lintdecls.so \
//     -Xclang -add-plugin -Xclang ntlibc-lintdecls \
//     -Xclang -plugin-arg-ntlibc-lintdecls -Xclang decl \
//     -Xclang -plugin-arg-ntlibc-lintdecls -Xclang "$file" \
//     "$file"
//
// The two plugin arguments are MODE ("decl" or "def") and PATH, in that
// order. PATH is printed back exactly as given, the same way
// DeclScanner.cpp's single HeaderPath argument is (see its own header
// comment): it lets a caller run this once per file and concatenate
// every file's output into one combined stream (mirroring
// tools/linkcheck.sh's scan()) without losing track of which line came
// from which file, and without depending on this plugin re-deriving a
// path from the AST that might not match what the caller's own
// bookkeeping (e.g. tools/lint-undefined.sh's file:line report) expects.
//
// Output, to stdout, one line per top-level function declarator found:
//
//   MODE=decl (a header: report every FunctionDecl with no body --
//   exactly the old awk mode's "a declarator ends at a top-level ';'"):
//     name  path  line  undefined_ok(0/1)
//
//   MODE=def (a .c file: report every FunctionDecl WITH a body --
//   exactly the old awk mode's "a declarator ends at a top-level '{',
//   then its body is skipped"):
//     name  path  line
//
// "line" is the declaration's own starting line in the file that was
// scanned (1-based, from the same SourceManager the compile itself
// used) -- the AST equivalent of the old awk's `bufline` (the line the
// declarator's first non-blank character appeared on).
//
// undefined_ok (MODE=decl only) is computed exactly the way
// DeclScanner.cpp already computes it: search the raw source text, from
// this declaration's own start offset up to the next top-level
// declaration's start offset (or end of file, for the last one), for
// the substring "undefined-ok:". This subsumes tools/lint-undefined.sh's
// former second, independent regex-based `markednames` pass, which had
// its own copy of the same naive name-extraction heuristic; folding it
// in here removes that copy rather than leaving it to rot on its own.
//
// FunctionDecl is the only Decl kind matched, so typedefs (including
// function-pointer typedefs) and top-level variables are naturally
// excluded -- no separate "is this a typedef" check is needed the way
// the awk scanner needed one. A static-inline function defined right in
// a header (endian.h's inline bswaps) has a body, so it is invisible to
// MODE=decl, exactly matching the awk's behaviour: a top-level '{' in
// decl mode was already treated as an opaque nested block with no
// declarator emitted, not as a definition to report.
//
// This does not distinguish storage class in MODE=def: a `static`
// function's name is reported exactly like an external one, matching
// the old awk (which never looked at linkage either). Both callers only
// ask "is this name defined somewhere in this tree", so this parity
// choice is deliberate, not an oversight.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace clang;

namespace {

enum class ScanMode { Decl, Def };

class LintDeclScanConsumer : public ASTConsumer {
  ScanMode Mode;
  std::string Path;

  // Every direct top-level declaration written in the file under scan
  // (not just the FunctionDecls this tool reports), in source order --
  // see DeclScanner.cpp's identical helper for why any Decl kind serves
  // as a boundary for the undefined-ok text search below.
  static void collectMainFile(DeclContext *DC, const SourceManager &SM,
                               std::vector<std::pair<unsigned, const Decl *>> &Out) {
    for (const Decl *D : DC->decls()) {
      SourceLocation Begin = SM.getExpansionLoc(D->getBeginLoc());
      if (!SM.isWrittenInMainFile(Begin))
        continue;
      Out.emplace_back(SM.getFileOffset(Begin), D);
    }
  }

public:
  LintDeclScanConsumer(ScanMode Mode, std::string Path)
      : Mode(Mode), Path(std::move(Path)) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    const SourceManager &SM = Context.getSourceManager();
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(SM.getMainFileID(), &Invalid);
    if (Invalid) {
      llvm::errs() << "ntlibc-lintdecls: could not read the main file's "
                      "source buffer\n";
      return;
    }

    TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
    std::vector<std::pair<unsigned, const Decl *>> TopLevel;
    collectMainFile(TU, SM, TopLevel);
    // extern "C" { ... } is never entered when these files are parsed in
    // C mode (the only mode either caller ever asks for), so this is
    // unreachable today -- see DeclScanner.cpp's identical note. Costs
    // nothing to unwrap anyway, one level deep.
    for (const Decl *D : TU->decls())
      if (const auto *LS = dyn_cast<LinkageSpecDecl>(D))
        collectMainFile(const_cast<LinkageSpecDecl *>(LS), SM, TopLevel);

    llvm::stable_sort(TopLevel, [](const auto &A, const auto &B) {
      return A.first < B.first;
    });

    for (size_t I = 0; I < TopLevel.size(); ++I) {
      const auto *FD = dyn_cast<FunctionDecl>(TopLevel[I].second);
      if (!FD)
        continue;
      bool HasBody = FD->doesThisDeclarationHaveABody();
      if (Mode == ScanMode::Decl && HasBody)
        continue;
      if (Mode == ScanMode::Def && !HasBody)
        continue;

      unsigned Begin = TopLevel[I].first;
      SourceLocation BeginLoc = SM.getExpansionLoc(FD->getBeginLoc());
      unsigned Line = SM.getExpansionLineNumber(BeginLoc);

      if (Mode == ScanMode::Def) {
        llvm::outs() << FD->getNameAsString() << '\t' << Path << '\t' << Line
                     << '\n';
        continue;
      }

      unsigned End =
          (I + 1 < TopLevel.size()) ? TopLevel[I + 1].first : Buffer.size();
      if (End < Begin)
        End = Begin;
      bool UndefinedOk = Buffer.substr(Begin, End - Begin).contains("undefined-ok:");

      llvm::outs() << FD->getNameAsString() << '\t' << Path << '\t' << Line
                   << '\t' << (UndefinedOk ? 1 : 0) << '\n';
    }
  }
};

class LintDeclScanAction : public PluginASTAction {
  ScanMode Mode = ScanMode::Decl;
  std::string Path;

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                  StringRef) override {
    return std::make_unique<LintDeclScanConsumer>(Mode, Path);
  }

  bool ParseArgs(const CompilerInstance &,
                 const std::vector<std::string> &Args) override {
    if (Args.size() != 2 || (Args[0] != "decl" && Args[0] != "def")) {
      llvm::errs() << "ntlibc-lintdecls: expected exactly two plugin "
                      "arguments, ('decl'|'def') then a path to echo back, "
                      "got "
                   << Args.size() << " argument(s)\n";
      return false;
    }
    Mode = (Args[0] == "decl") ? ScanMode::Decl : ScanMode::Def;
    Path = Args[1];
    return true;
  }

  ActionType getActionType() override { return AddAfterMainAction; }
};

} // namespace

static FrontendPluginRegistry::Add<LintDeclScanAction>
    X("ntlibc-lintdecls",
      "list top-level function declarators/definitions for "
      "lint-undefined.sh and lint-unreferenced.sh");
