// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "llvm/ADT/SmallVector.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

namespace {

class MemoryContractChecker : public Checker<check::PreCall> {
  mutable std::unique_ptr<BugType> SpanBT;
  mutable std::unique_ptr<BugType> OverlapBT;

  struct Contract {
    unsigned First;
    std::optional<unsigned> Second;
    unsigned Length;
    bool NoOverlap;
  };

  static std::optional<Contract> contractFor(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return std::nullopt;
    StringRef Name = Function->getName();
    if (Name == "memcpy")
      return Contract{0, 1, 2, true};
    if (Name == "memmove" || Name == "memcmp")
      return Contract{0, 1, 2, false};
    if (Name == "memset")
      return Contract{0, std::nullopt, 2, false};
    if (Name == "read" || Name == "write" || Name == "pread" ||
        Name == "pwrite" || Name == "recv" || Name == "send")
      return Contract{1, std::nullopt, 2, false};
    return std::nullopt;
  }

public:
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

  static std::string site(const Stmt *Statement, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Location = SM.getExpansionLoc(Statement->getBeginLoc());
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(SM.getFileID(Location), &Invalid);
    if (Invalid)
      return Statement->getStmtClassName();
    unsigned Offset = SM.getFileOffset(Location);
    size_t Begin = Buffer.rfind('\n', Offset);
    Begin = Begin == StringRef::npos ? 0 : Begin + 1;
    size_t End = Buffer.find('\n', Offset);
    return Buffer.slice(Begin, End == StringRef::npos ? Buffer.size() : End)
        .trim()
        .str();
  }

  static std::string context(CheckerContext &C) {
    const Decl *Current = C.getLocationContext()->getDecl();
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      return Named->getQualifiedNameAsString();
    return Current ? Current->getDeclKindName() : "unknown";
  }

  void report(StringRef Reason, BugType *&Type, const CallEvent &Call,
              ProgramStateRef State, CheckerContext &C) const {
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!Type) {
      std::unique_ptr<BugType> New = std::make_unique<BugType>(
          this,
          Reason == "memory operation span is not proven valid"
              ? "Unproven memory span"
              : "Unproven memory overlap",
          categories::MemoryError);
      Type = New.release();
    }
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        (Reason + "; origin '" +
         SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
         "'; context '" + context(C) + "'; expression '" + text(Statement, C) +
         "'; site '" + site(Statement, C) + "'")
            .str();
    auto Report =
        std::make_unique<PathSensitiveBugReport>(*Type, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  bool spanProven(SVal Pointer, SVal Length, ProgramStateRef State,
                  CheckerContext &C) const {
    if (State->isNull(Length).isConstrainedTrue())
      return true;
    SVal Extent = getDynamicExtentWithOffset(State, Pointer);
    if (Extent.isUnknownOrUndef() || Length.isUnknownOrUndef())
      return false;
    SVal Enough = C.getSValBuilder().evalBinOp(
        State, BO_GE, Extent, Length, C.getSValBuilder().getConditionType());
    std::optional<DefinedOrUnknownSVal> Condition =
        Enough.getAs<DefinedOrUnknownSVal>();
    return Condition && !State->assume(*Condition, false);
  }

  bool overlapProven(SVal First, SVal Second, SVal Length,
                     ProgramStateRef State, CheckerContext &C) const {
    if (State->isNull(Length).isConstrainedTrue())
      return true;
    const MemRegion *A = First.getAsRegion();
    const MemRegion *B = Second.getAsRegion();
    if (!A || !B)
      return false;
    RegionOffset AO = A->getAsOffset();
    RegionOffset BO = B->getAsOffset();
    if (!AO.isValid() || !BO.isValid())
      return false;
    if (AO.getRegion() != BO.getRegion())
      return true;
    if (AO.hasSymbolicOffset() || BO.hasSymbolicOffset())
      return false;
    const llvm::APSInt *KnownLength =
        C.getSValBuilder().getKnownValue(State, Length);
    if (!KnownLength)
      return false;
    uint64_t Bytes = KnownLength->getLimitedValue();
    int64_t ABytes = AO.getOffset() / 8;
    int64_t BBytes = BO.getOffset() / 8;
    return ABytes + static_cast<int64_t>(Bytes) <= BBytes ||
           BBytes + static_cast<int64_t>(Bytes) <= ABytes;
  }

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    std::optional<Contract> Contract = contractFor(Call);
    if (!Contract || Contract->Length >= Call.getNumArgs() ||
        Contract->First >= Call.getNumArgs())
      return;
    SVal Length = Call.getArgSVal(Contract->Length);
    if (!spanProven(Call.getArgSVal(Contract->First), Length, C.getState(),
                    C)) {
      BugType *Type = SpanBT.get();
      report("memory operation span is not proven valid", Type, Call,
             C.getState(), C);
      if (!SpanBT && Type)
        SpanBT.reset(Type);
      return;
    }
    if (Contract->Second && !spanProven(Call.getArgSVal(*Contract->Second),
                                        Length, C.getState(), C)) {
      BugType *Type = SpanBT.get();
      report("memory operation span is not proven valid", Type, Call,
             C.getState(), C);
      if (!SpanBT && Type)
        SpanBT.reset(Type);
      return;
    }
    if (Contract->NoOverlap && Contract->Second &&
        !overlapProven(Call.getArgSVal(Contract->First),
                       Call.getArgSVal(*Contract->Second), Length, C.getState(),
                       C)) {
      BugType *Type = OverlapBT.get();
      report("memcpy ranges are not proven nonoverlapping", Type, Call,
             C.getState(), C);
      if (!OverlapBT && Type)
        OverlapBT.reset(Type);
    }
  }
};

class StringSentinelChecker : public Checker<check::PreCall> {
  mutable std::unique_ptr<BugType> BT;

  static void requiredArguments(const CallEvent &Call,
                                SmallVectorImpl<unsigned> &Arguments) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return;
    StringRef Name = Function->getName();
    if (Name == "strlen" || Name == "strchr" || Name == "strrchr" ||
        Name == "strdup" || Name == "puts")
      Arguments.push_back(0);
    else if (Name == "strcmp" || Name == "strcasecmp" || Name == "strcoll" ||
             Name == "fopen") {
      Arguments.push_back(0);
      Arguments.push_back(1);
    } else if (Name == "strcpy")
      Arguments.push_back(1);
    else if (Name == "strcat") {
      Arguments.push_back(0);
      Arguments.push_back(1);
    }
  }

  static bool initializedByString(const VarRegion *Variable) {
    const Expr *Initializer = Variable->getDecl()->getInit();
    if (!Initializer)
      return false;
    Initializer = Initializer->IgnoreParenImpCasts();
    if (isa<StringLiteral>(Initializer))
      return true;
    if (const auto *List = dyn_cast<InitListExpr>(Initializer))
      return List->getNumInits() == 1 &&
             isa<StringLiteral>(List->getInit(0)->IgnoreParenImpCasts());
    return false;
  }

  static bool sentinelProven(SVal Pointer) {
    const MemRegion *Region = Pointer.getAsRegion();
    if (!Region)
      return false;
    const MemRegion *Base = Region->getBaseRegion();
    if (isa<StringRegion>(Base))
      return true;
    if (const auto *Variable = dyn_cast<VarRegion>(Base))
      return initializedByString(Variable);
    return false;
  }

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    SmallVector<unsigned, 2> Arguments;
    requiredArguments(Call, Arguments);
    for (unsigned Argument : Arguments) {
      if (Argument >= Call.getNumArgs() ||
          sentinelProven(Call.getArgSVal(Argument)))
        continue;
      const Stmt *Statement = Call.getOriginExpr();
      if (!Statement)
        return;
      ExplodedNode *Node = C.generateNonFatalErrorNode();
      if (!Node)
        return;
      if (!BT)
        BT = std::make_unique<BugType>(this, "Unproven string sentinel",
                                       categories::MemoryError);
      const SourceManager &SM = C.getSourceManager();
      std::string Message =
          (StringRef("string argument is not proven NUL-terminated; origin '") +
           SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
           "'; context '" + MemoryContractChecker::context(C) +
           "'; expression '" + MemoryContractChecker::text(Statement, C) +
           "'; site '" + MemoryContractChecker::site(Statement, C) + "'")
              .str();
      auto Report =
          std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
      Report->addRange(Statement->getSourceRange());
      C.emitReport(std::move(Report));
      return;
    }
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<MemoryContractChecker>(
      "ntlibc.MemoryContract",
      "Proves memory spans and memcpy non-overlap contracts", "");
  Registry.addChecker<StringSentinelChecker>(
      "ntlibc.StringSentinel",
      "Proves string API arguments have reachable NUL sentinels", "");
}
