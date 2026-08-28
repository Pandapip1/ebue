// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include <cctype>
#include <memory>
#include <string>

using namespace clang;
using namespace ento;

enum class OwnershipKind : unsigned char { Owned, Consumed };
REGISTER_MAP_WITH_PROGRAMSTATE(OwnershipMap, SymbolRef, OwnershipKind)

namespace {

class OwnershipChecker
    : public Checker<check::PreCall, check::PostCall, check::Location> {
  mutable std::unique_ptr<BugType> BT;

  static bool hasName(const CallEvent &Call, StringRef Wanted) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    return Function && Function->getIdentifier() &&
           Function->getName() == Wanted;
  }

  static bool isAllocator(const CallEvent &Call) {
    static constexpr llvm::StringLiteral Names[] = {
        "malloc", "__malloc", "calloc",        "realloc",  "reallocarray",
        "strdup", "strndup",  "aligned_alloc", "memalign", "valloc"};
    for (StringRef Name : Names)
      if (hasName(Call, Name))
        return true;
    return false;
  }

  static bool isDeallocator(const CallEvent &Call) {
    return hasName(Call, "free") || hasName(Call, "__free");
  }

  static bool isReallocator(const CallEvent &Call) {
    return hasName(Call, "realloc") || hasName(Call, "reallocarray");
  }

  static bool insideOwnershipConsumer(CheckerContext &C) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function)
      return false;
    StringRef Name = Function->getName();
    return Name == "free" || Name == "__free" || Name == "realloc" ||
           Name == "reallocarray";
  }

  static std::string sourceText(const Stmt *Statement, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Begin = SM.getSpellingLoc(Statement->getBeginLoc());
    SourceLocation End = SM.getSpellingLoc(Statement->getEndLoc());
    StringRef Raw = Lexer::getSourceText(
        CharSourceRange::getTokenRange(Begin, End), SM, C.getLangOpts());
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

  static std::string sourceOrigin(const Stmt *Statement, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    return SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())).str();
  }

  static std::string sourceSite(const Stmt *Statement, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Location = SM.getExpansionLoc(Statement->getBeginLoc());
    FileID File = SM.getFileID(Location);
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(File, &Invalid);
    if (Invalid)
      return Statement->getStmtClassName();
    unsigned Offset = SM.getFileOffset(Location);
    size_t Begin = Buffer.rfind('\n', Offset);
    Begin = Begin == StringRef::npos ? 0 : Begin + 1;
    size_t End = Buffer.find('\n', Offset);
    if (End == StringRef::npos)
      End = Buffer.size();
    StringRef Raw = Buffer.slice(Begin, End);
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
    return Result;
  }

  static std::string currentContext(CheckerContext &C) {
    const Decl *Current = C.getLocationContext()->getDecl();
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      return Named->getQualifiedNameAsString();
    return Current ? Current->getDeclKindName() : "unknown";
  }

  void report(StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven pointer ownership",
                                     categories::MemoryError);
    std::string Message =
        (Reason + "; origin '" + sourceOrigin(Statement, C) + "'; context '" +
         currentContext(C) + "'; expression '" + sourceText(Statement, C) +
         "'; site '" + sourceSite(Statement, C) + "'")
            .str();
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  static SymbolRef allocationSymbol(SVal Value) {
    return Value.getAsSymbol(/*IncludeBaseRegions=*/true);
  }

  static SymbolRef accessedOwner(SVal Location) {
    const MemRegion *Region = Location.getAsRegion();
    if (!Region)
      return nullptr;
    const auto *Symbolic = dyn_cast<SymbolicRegion>(Region->getBaseRegion());
    return Symbolic ? Symbolic->getSymbol() : nullptr;
  }

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    if (!isAllocator(Call))
      return;
    ProgramStateRef State = C.getState();
    SVal ReturnValue = Call.getReturnValue();
    if (isReallocator(Call) && Call.getNumArgs() >= 1 &&
        State->isNull(ReturnValue).isConstrainedFalse()) {
      SymbolRef Old = allocationSymbol(Call.getArgSVal(0));
      if (Old)
        State = State->set<OwnershipMap>(Old, OwnershipKind::Consumed);
    }
    SymbolRef Symbol = allocationSymbol(ReturnValue);
    if (!Symbol)
      return;
    State = State->set<OwnershipMap>(Symbol, OwnershipKind::Owned);
    C.addTransition(State);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    bool Deallocator = isDeallocator(Call);
    bool Reallocator = isReallocator(Call);
    if ((!Deallocator && !Reallocator) || Call.getNumArgs() < 1)
      return;
    if (insideOwnershipConsumer(C))
      return;
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;

    SVal Argument = Call.getArgSVal(0);
    ProgramStateRef State = C.getState();
    if (State->isNull(Argument).isConstrainedTrue())
      return;

    SymbolRef Symbol = allocationSymbol(Argument);
    const OwnershipKind *Kind =
        Symbol ? State->get<OwnershipMap>(Symbol) : nullptr;
    if (!Kind) {
      report(Deallocator ? "deallocator argument is not proven owned"
                         : "reallocator argument is not proven owned",
             Statement, State, C);
      return;
    }
    if (*Kind == OwnershipKind::Consumed) {
      report("ownership is already consumed", Statement, State, C);
      return;
    }
    if (Deallocator)
      C.addTransition(
          State->set<OwnershipMap>(Symbol, OwnershipKind::Consumed));
  }

  void checkLocation(SVal Location, bool, const Stmt *Statement,
                     CheckerContext &C) const {
    SymbolRef Symbol = accessedOwner(Location);
    if (!Symbol)
      return;
    const OwnershipKind *Kind = C.getState()->get<OwnershipMap>(Symbol);
    if (Kind && *Kind == OwnershipKind::Consumed)
      report("borrow accesses a consumed owner", Statement, C.getState(), C);
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<OwnershipChecker>(
      "ntlibc.Ownership",
      "Proves allocator provenance and borrow lifetime at deallocation", "");
}
