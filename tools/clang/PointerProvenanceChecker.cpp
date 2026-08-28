// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include <cctype>
#include <memory>
#include <string>

using namespace clang;
using namespace ento;

namespace {

class PointerProvenanceChecker
    : public Checker<check::PreStmt<BinaryOperator>, check::PreStmt<CastExpr>> {
  mutable std::unique_ptr<BugType> BT;

  static const MemRegion *baseRegion(SVal Value) {
    const MemRegion *Region = Value.getAsRegion();
    return Region ? Region->getBaseRegion() : nullptr;
  }

  static std::string text(const Stmt *Statement, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    StringRef Raw =
        Lexer::getSourceText(CharSourceRange::getTokenRange(
                                 SM.getSpellingLoc(Statement->getBeginLoc()),
                                 SM.getSpellingLoc(Statement->getEndLoc())),
                             SM, C.getLangOpts());
    std::string Result;
    bool Space = false;
    for (char Character : Raw) {
      if (std::isspace(static_cast<unsigned char>(Character))) {
        Space = !Result.empty();
      } else {
        if (Space)
          Result += ' ';
        Result += Character;
        Space = false;
      }
    }
    return Result.empty() ? Statement->getStmtClassName() : Result;
  }

  static std::string context(CheckerContext &C) {
    const Decl *Current = C.getLocationContext()->getDecl();
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      return Named->getQualifiedNameAsString();
    return Current ? Current->getDeclKindName() : "unknown";
  }

  void report(StringRef Reason, const Stmt *Statement,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode();
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven pointer provenance",
                                     categories::MemoryError);
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        (Reason + "; origin '" +
         SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
         "'; context '" + context(C) + "'; expression '" + text(Statement, C) +
         "'")
            .str();
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

public:
  void checkPreStmt(const BinaryOperator *Operation, CheckerContext &C) const {
    BinaryOperatorKind Opcode = Operation->getOpcode();
    bool Subtraction = Opcode == BO_Sub &&
                       Operation->getLHS()->getType()->isPointerType() &&
                       Operation->getRHS()->getType()->isPointerType();
    bool Ordering = (Opcode == BO_LT || Opcode == BO_LE || Opcode == BO_GT ||
                     Opcode == BO_GE) &&
                    Operation->getLHS()->getType()->isPointerType() &&
                    Operation->getRHS()->getType()->isPointerType();
    if (!Subtraction && !Ordering)
      return;

    ProgramStateRef State = C.getState();
    const LocationContext *LC = C.getLocationContext();
    const MemRegion *Left = baseRegion(State->getSVal(Operation->getLHS(), LC));
    const MemRegion *Right =
        baseRegion(State->getSVal(Operation->getRHS(), LC));
    if (Left && Right && Left == Right)
      return;
    report(
        Subtraction
            ? "pointer subtraction operands are not proven to share provenance"
            : "ordered pointer operands are not proven to share provenance",
        Operation, C);
  }

  void checkPreStmt(const CastExpr *Cast, CheckerContext &C) const {
    if (Cast->getCastKind() == CK_IntegralToPointer)
      report(
          "integer-to-pointer conversion is not proven provenance-preserving",
          Cast, C);
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<PointerProvenanceChecker>(
      "ntlibc.PointerProvenance",
      "Proves pointer ordering, subtraction, and integer conversion provenance",
      "");
}
