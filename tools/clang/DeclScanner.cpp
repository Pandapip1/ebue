// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeclScanner -- a real AST walk, replacing tools/linkcheck.sh's inline
// hand-rolled awk scanner for "what functions does this header declare".
//
// The awk scanner did character-at-a-time comment/string stripping and
// treated "the first identifier immediately followed by '('" as a
// declarator name.  That heuristic breaks on two patterns that now appear
// in the headers:
//
//   - `// NOLINTBEGIN(...)` / `// NOLINTEND(...)` clang-tidy suppression
//     comments: the awk only strips /* */ comments, never // ones, so it
//     parses "NOLINTBEGIN" as if it were a 3-argument function declarator.
//   - `withtok(token_name)` (include/ownership.h) used as a *prefix*
//     attribute before a declaration's return type, e.g.
//         withtok(heap_allocated)
//         void *malloc (size_t);
//     The awk's "first identifier followed by '('" match lands on
//     `withtok(` itself, before it ever reaches `malloc(`.
//
// Neither is a text-scanning bug that can be patched away in general: any
// heuristic that does not actually know what a declaration is will find a
// new way to misparse the next macro-attribute idiom.  This walks the real
// clang AST instead, so it only ever reports what the compiler itself
// parsed as a top-level function declaration.
//
// Usage: run once per header, in C mode, with this project's own
// -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -I... flags (see tools/linkcheck.sh),
// e.g.:
//
//   clang-18 -std=c99 -fsyntax-only $CFLAGS \
//     -Xclang -load -Xclang ntlibc-declscan.so \
//     -Xclang -add-plugin -Xclang ntlibc-declscan \
//     -Xclang -plugin-arg-ntlibc-declscan -Xclang "$header" \
//     "$header"
//
// Output: one line per declared function, tab-separated, to stdout:
//
//   name  header  fixed_argc  undefined_ok(0/1)
//
// "header" is printed exactly as given via the plugin argument (the same
// string the caller passed as $header on argv), not re-derived from the
// AST -- so the declfile format tools/linkcheck.sh already consumes
// (reason_for(), the worklist builder, etc.) needs no changes at all.
//
// A function is skipped when doesThisDeclarationHaveABody() is true: a
// static-inline function defined right in the header (endian.h's inline
// bswaps) is already defined everywhere it's included, not something to
// link-check. FunctionDecl is the only Decl kind matched, so typedefs
// (including function-pointer typedefs like signal.h's sig_t) and any
// top-level variable declaration are naturally excluded.
//
// fixed_argc is FD->getNumParams(): clang already treats an explicit
// `(void)` parameter list as zero parameters (a real prototype, not
// FunctionNoProtoType), matching the old scanner's `void` special case
// with no extra work; an unprototyped bare `()` also yields zero
// parameters, matching the old scanner's fallback for that shape too.
//
// undefined_ok is computed by searching the raw header source text, from
// this declaration's own start offset up to the next top-level
// declaration's start offset (or end of file, for the last one), for the
// substring "undefined-ok:". This deliberately over-includes any trailing
// same-line comment and any blank/comment lines before the next real
// declaration -- exactly what the old awk scanner's line-level "hasmark"
// flag effectively did too, and every marker actually used in the headers
// today sits on the declaration's own line.
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

class DeclScanConsumer : public ASTConsumer {
  std::string HeaderPath;

  // Every direct top-level declaration written in the file under scan
  // (not just the FunctionDecls this tool reports), in source order.
  // Any Decl kind at all serves as a boundary for the undefined-ok text
  // search below -- a typedef or struct between two functions ends the
  // first function's search range just as a function does.
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
  explicit DeclScanConsumer(std::string HeaderPath)
      : HeaderPath(std::move(HeaderPath)) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    const SourceManager &SM = Context.getSourceManager();
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(SM.getMainFileID(), &Invalid);
    if (Invalid) {
      llvm::errs() << "ntlibc-declscan: could not read the main file's source "
                      "buffer for '"
                   << HeaderPath << "'\n";
      return;
    }

    TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
    std::vector<std::pair<unsigned, const Decl *>> TopLevel;
    collectMainFile(TU, SM, TopLevel);
    // extern "C" { ... } is not entered when these headers are parsed in C
    // mode (the only mode tools/linkcheck.sh's CFLAGS ever asks for -- see
    // this file's own header comment), so this is unreachable today.  It
    // costs nothing to unwrap anyway, one level deep, matching what a C++
    // consumer of these same headers would see.
    for (const Decl *D : TU->decls())
      if (const auto *LS = dyn_cast<LinkageSpecDecl>(D))
        collectMainFile(const_cast<LinkageSpecDecl *>(LS), SM, TopLevel);

    llvm::stable_sort(TopLevel, [](const auto &A, const auto &B) {
      return A.first < B.first;
    });

    for (size_t I = 0; I < TopLevel.size(); ++I) {
      const auto *FD = dyn_cast<FunctionDecl>(TopLevel[I].second);
      if (!FD || FD->doesThisDeclarationHaveABody())
        continue;

      unsigned Begin = TopLevel[I].first;
      unsigned End =
          (I + 1 < TopLevel.size()) ? TopLevel[I + 1].first : Buffer.size();
      if (End < Begin)
        End = Begin;
      bool UndefinedOk = Buffer.substr(Begin, End - Begin).contains("undefined-ok:");

      llvm::outs() << FD->getNameAsString() << '\t' << HeaderPath << '\t'
                   << FD->getNumParams() << '\t' << (UndefinedOk ? 1 : 0)
                   << '\n';
    }
  }
};

class DeclScanAction : public PluginASTAction {
  std::string HeaderPath;

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                  StringRef) override {
    return std::make_unique<DeclScanConsumer>(HeaderPath);
  }

  bool ParseArgs(const CompilerInstance &,
                 const std::vector<std::string> &Args) override {
    if (Args.size() != 1) {
      llvm::errs() << "ntlibc-declscan: expected exactly one plugin "
                      "argument (the header path to report), got "
                   << Args.size() << "\n";
      return false;
    }
    HeaderPath = Args[0];
    return true;
  }

  ActionType getActionType() override { return AddAfterMainAction; }
};

} // namespace

static FrontendPluginRegistry::Add<DeclScanAction>
    X("ntlibc-declscan", "list top-level function declarations for linkcheck.sh");
