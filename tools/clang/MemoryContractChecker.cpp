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
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "llvm/ADT/SmallVector.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

// strlen(s)'s own byte-count contract is exactly "the number of bytes
// before s's first NUL", so s is guaranteed to have at least that many
// bytes PLUS the terminator itself -- reading strlen(s) + up-to-1 bytes
// from s is always safe. strnlen(s, n)'s contract is looser: either it
// found a real terminator within the first n bytes (in which case the
// same "+1" reasoning applies) or it read all n bytes without finding
// one, in which case only the n (not n+1) bytes it actually walked are
// known-safe -- so a plain source-region-plus-slack-bound of 0, not 1,
// is all this checker can soundly attribute to an strnlen() result.
// Recording "this conjured return symbol came from strlen/strnlen(s)"
// at the call (see checkPostCall) is what lets spanProven recognize the
// extremely common "n = strlen(s) + 1; p = __malloc(n); memcpy(p, s,
// n);" idiom's SOURCE argument as in-bounds -- this tree's own xstrdup
// (src/glob/glob.c, src/sh/execute.c, src/sh/parse.c,
// src/wordexp/wordexp.c) and strdup.c/strndup.c themselves all share
// this exact shape.
REGISTER_MAP_WITH_PROGRAMSTATE(StrlenSource, SymbolRef, const MemRegion *)
REGISTER_MAP_WITH_PROGRAMSTATE(StrnlenSource, SymbolRef, const MemRegion *)

namespace {

class MemoryContractChecker
    : public Checker<check::PreCall, check::PostCall> {
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

  static bool hasName(const CallEvent &Call, StringRef Wanted) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    return Function && Function->getIdentifier() &&
           Function->getName() == Wanted;
  }

  // Clang's own dynamic-extent tracking for an allocator's return value
  // only fires for a handful of literally-named standard functions:
  // `malloc(n)` gets a real, usable dynamic extent from clang's builtin
  // modeling, but `__malloc(n)` -- the name every allocation inside this
  // tree's OWN code actually goes through, since `malloc` is just this
  // codebase's own public wrapper around it -- does not, leaving
  // spanProven's getDynamicExtentWithOffset() with nothing but an
  // unconstrained placeholder for every buffer this codebase allocates
  // through its own internal entry point. This is the identical gap
  // OwnershipChecker::allocationSizeInBytes fixes for the sibling
  // ValidPointerChecker (see 8a56a66's own extensive writeup); this
  // checker needs its own copy since only ntlibc.MemoryContract and
  // ntlibc.StringSentinel run during the memcontracts stage -- the state
  // ntlibc.Ownership's checkPostCall would set is never produced. Setting
  // the region's real dynamic extent straight from the real size
  // argument(s) is not a new assumption layered on top of what the
  // program does: it is the exact byte count the allocator itself is
  // about to hand back, read directly off the arguments of the call that
  // produced it. strdup/strndup are deliberately left alone, matching
  // 8a56a66: their real size depends on string *content*, not an
  // argument SVal already sitting at the call site.
  static std::optional<SVal> allocationSizeInBytes(const CallEvent &Call,
                                                    CheckerContext &C) {
    SValBuilder &Builder = C.getSValBuilder();
    QualType SizeTy = C.getASTContext().getSizeType();
    unsigned NumArgs = Call.getNumArgs();
    auto Arg = [&](unsigned Index) -> SVal {
      return Index < NumArgs ? Call.getArgSVal(Index) : UnknownVal();
    };
    if (hasName(Call, "malloc") || hasName(Call, "__malloc") ||
        hasName(Call, "valloc"))
      return NumArgs >= 1 ? std::optional<SVal>(Arg(0)) : std::nullopt;
    if (hasName(Call, "calloc"))
      return NumArgs >= 2 ? std::optional<SVal>(Builder.evalBinOp(
                                C.getState(), BO_Mul, Arg(0), Arg(1), SizeTy))
                          : std::nullopt;
    if (hasName(Call, "realloc"))
      return NumArgs >= 2 ? std::optional<SVal>(Arg(1)) : std::nullopt;
    if (hasName(Call, "reallocarray"))
      return NumArgs >= 3 ? std::optional<SVal>(Builder.evalBinOp(
                                C.getState(), BO_Mul, Arg(1), Arg(2), SizeTy))
                          : std::nullopt;
    if (hasName(Call, "aligned_alloc") || hasName(Call, "memalign"))
      return NumArgs >= 2 ? std::optional<SVal>(Arg(1)) : std::nullopt;
    return std::nullopt;
  }

  static SymbolRef stripCasts(SymbolRef Symbol) {
    while (const auto *Cast = dyn_cast_or_null<SymbolCast>(Symbol))
      Symbol = Cast->getOperand();
    return Symbol;
  }

  // Decompose an SVal into (root symbol, constant offset), i.e. treat it
  // as "root + offset" for a bare symbol (offset 0) or a `root + K`
  // SymIntExpr (offset K). Two SVals built from the same root symbol can
  // then be compared by plain integer arithmetic on their offsets alone,
  // with no solver help needed -- clang's range-based constraint solver
  // does not fold "(S + K) >= S" down to "always true (for K >= 0)" for
  // two separately-built compound expressions that merely happen to
  // share a root symbol, confirmed empirically by 8a56a66's identical
  // finding for the sibling ValidPointerChecker's index-in-bounds proof.
  static bool decomposeAffine(SVal V, SymbolRef &Base, int64_t &Offset) {
    SymbolRef Sym = stripCasts(V.getAsSymbol());
    if (!Sym)
      return false;
    if (const auto *IntExpr = dyn_cast<SymIntExpr>(Sym)) {
      if (IntExpr->getOpcode() != BO_Add)
        return false;
      Base = stripCasts(IntExpr->getLHS());
      Offset = IntExpr->getRHS().getExtValue();
      return true;
    }
    Base = Sym;
    Offset = 0;
    return true;
  }

  // For a heap allocation whose real dynamic extent was set (above)
  // directly from its own size ARGUMENT expression, prove a span
  // in-bounds when the operation's LENGTH argument shares that same
  // argument expression's own root symbol: `d = __malloc(l + 1);
  // memcpy(d, s, l);` (extent = l+1, length = l) is strndup.c's own
  // shape, but `n = strlen(s) + 1; p = __malloc(n); memcpy(p, s, n);`
  // (extent = n = L+1, length = n = L+1, the SAME compound expression on
  // both sides -- this tree's own xstrdup, duplicated in
  // src/glob/glob.c, src/sh/execute.c, src/sh/parse.c, and
  // src/wordexp/wordexp.c) is at least as common, and a plain "does the
  // extent's own root symbol match the length's" check (the previous,
  // narrower version of this function) does not recognize it: it only
  // ever looked at the LENGTH side as a bare symbol, never decomposed a
  // compound length into its own root+offset. Decomposing both sides the
  // same way and comparing their offsets once the roots match subsumes
  // that narrower case (offset 0 on the length side) while also covering
  // this one (equal, nonzero offsets on both sides).
  static bool sameSymbolSpanProven(SVal Extent, SVal Length) {
    SymbolRef ExtentBase, LengthBase;
    int64_t ExtentOffset, LengthOffset;
    if (!decomposeAffine(Extent, ExtentBase, ExtentOffset))
      return false;
    if (!decomposeAffine(Length, LengthBase, LengthOffset))
      return false;
    return ExtentBase == LengthBase && ExtentOffset >= LengthOffset;
  }

  // The allocator-extent lemma above only ever closes the DESTINATION
  // side of a memcpy's span obligation: the overwhelming majority of
  // memcpy calls in this tree copy FROM a plain `const char *` parameter
  // with no dynamic extent at all, so even after the destination is
  // proven, checkPreCall's independent check on the SECOND argument
  // still fails and the call is still reported (measured directly: the
  // tree-wide finding count did not move by even one after the
  // allocator-extent lemma alone -- see the commit message). Almost all
  // of those source pointers share one shape: `n = strlen(s) + 1; p =
  // __malloc(n); memcpy(p, s, n);` (this tree's own xstrdup, duplicated
  // in src/glob/glob.c, src/sh/execute.c, src/sh/parse.c, and
  // src/wordexp/wordexp.c, plus src/string/strdup.c and strndup.c
  // themselves) or the strnlen()-bounded equivalent. strlen(s)'s own
  // byte-count contract makes the source side of that copy safe by
  // construction: it returns the exact number of non-NUL bytes before
  // s's terminator, so s is guaranteed to have at least that many bytes
  // PLUS the terminator itself. strnlen(s, n)'s contract is looser --
  // either it found a real terminator (same "+1" reasoning applies) or
  // it walked all n bytes without one, in which case only those n (not
  // n+1) bytes are known-safe -- so this only credits an strnlen()
  // result with a zero-byte, not one-byte, slack. See the StrlenSource/
  // StrnlenSource ProgramState maps and checkPostCall below, which
  // record "this conjured return symbol came from strlen/strnlen(s)" at
  // the call that produces it.
  static bool stringLengthSourceSpanProven(SVal Pointer, SVal Length,
                                           ProgramStateRef State) {
    const MemRegion *PointerRegion = Pointer.getAsRegion();
    if (!PointerRegion)
      return false;
    const MemRegion *Base = PointerRegion->getBaseRegion();
    SymbolRef LengthSym;
    int64_t Slack;
    if (!decomposeAffine(Length, LengthSym, Slack))
      return false;
    if (const MemRegion *const *Source = State->get<StrlenSource>(LengthSym))
      if (*Source == Base && Slack <= 1)
        return true;
    if (const MemRegion *const *Source = State->get<StrnlenSource>(LengthSym))
      if (*Source == Base && Slack <= 0)
        return true;
    return false;
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
    if (sameSymbolSpanProven(Extent, Length))
      return true;
    if (stringLengthSourceSpanProven(Pointer, Length, State))
      return true;
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

  // See allocationSizeInBytes above: give this tree's own __malloc-family
  // allocator calls the same real dynamic extent clang's builtin modeling
  // already gives literally-named "malloc"/"calloc"/etc, so spanProven has
  // real information to work with instead of an unconstrained placeholder.
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    if (std::optional<SVal> SizeInBytes = allocationSizeInBytes(Call, C)) {
      if (std::optional<DefinedOrUnknownSVal> DefinedSize =
              SizeInBytes->getAs<DefinedOrUnknownSVal>()) {
        if (const MemRegion *Region = Call.getReturnValue().getAsRegion()) {
          ProgramStateRef State =
              setDynamicExtent(C.getState(), Region->getBaseRegion(),
                               *DefinedSize, C.getSValBuilder());
          C.addTransition(State);
        }
      }
      return;
    }
    // See stringLengthSourceSpanProven above: record which pointer
    // argument a strlen()/strnlen() call's return symbol was measured
    // from, so a later memcpy/memset/etc using that same (conjured)
    // length against that same pointer can be recognized as in-bounds
    // by construction.
    bool IsStrlen = hasName(Call, "strlen") && Call.getNumArgs() >= 1;
    bool IsStrnlen = hasName(Call, "strnlen") && Call.getNumArgs() >= 2;
    if (!IsStrlen && !IsStrnlen)
      return;
    const MemRegion *ArgRegion = Call.getArgSVal(0).getAsRegion();
    SymbolRef ReturnSym = stripCasts(Call.getReturnValue().getAsSymbol());
    if (!ArgRegion || !ReturnSym)
      return;
    ProgramStateRef State = C.getState();
    State = IsStrlen ? State->set<StrlenSource>(ReturnSym,
                                                ArgRegion->getBaseRegion())
                     : State->set<StrnlenSource>(ReturnSym,
                                                 ArgRegion->getBaseRegion());
    C.addTransition(State);
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
