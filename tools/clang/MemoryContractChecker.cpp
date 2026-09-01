// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/AST/Attr.h"
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
#include "llvm/ADT/StringExtras.h"

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
/* A checked annotated call establishes a span from the exact pointer value,
 * which may be an interior ElementRegion.  DynamicExtent is rooted at the
 * allocation's base and therefore cannot represent two independent
 * contracted suffixes without either losing the offset or overwriting the
 * base's real extent. */
REGISTER_MAP_WITH_PROGRAMSTATE(AssumedSpanExtent, const MemRegion *,
                               DefinedOrUnknownSVal)
using DisjointRegionKey = std::pair<const MemRegion *, const MemRegion *>;
REGISTER_MAP_WITH_PROGRAMSTATE(AssumedDisjointExtent, DisjointRegionKey,
                               DefinedOrUnknownSVal)
REGISTER_SET_WITH_PROGRAMSTATE(AllocatedBaseRegion, const MemRegion *)
REGISTER_MAP_WITH_PROGRAMSTATE(GrantedSpanProof, const ParmVarDecl *,
                               const ParmVarDecl *)
using DisjointParameterKey =
    std::pair<const ParmVarDecl *, const ParmVarDecl *>;
REGISTER_MAP_WITH_PROGRAMSTATE(GrantedDisjointProof, DisjointParameterKey,
                               const ParmVarDecl *)

namespace {

static const TypedefNameDecl *dialectToken(ASTContext &Context,
                                           StringRef Name) {
  IdentifierInfo &Identifier = Context.Idents.get(Name);
  DeclarationName Declaration(&Identifier);
  for (NamedDecl *Candidate :
       Context.getTranslationUnitDecl()->lookup(Declaration))
    if (const auto *Token = dyn_cast<TypedefNameDecl>(Candidate))
      return Token;
  return nullptr;
}

static bool hasDialectQualifier(const TypedefNameDecl *Token,
                                StringRef Qualifier) {
  if (!Token)
    return false;
  for (const AnnotateAttr *Attribute : Token->specific_attrs<AnnotateAttr>())
    if (Attribute->getAnnotation() == Qualifier)
      return true;
  return false;
}

enum class MemoryTokenOperation : unsigned char { Require, Grant };

static bool tokenApplication(const FunctionDecl *Function,
                             StringRef Annotation,
                             MemoryTokenOperation &Operation, StringRef &Family,
                             SmallVectorImpl<unsigned> &Arguments) {
  if (Annotation.consume_front("withtok:"))
    Operation = MemoryTokenOperation::Require;
  else if (Annotation.consume_front("grant:"))
    Operation = MemoryTokenOperation::Grant;
  else
    return false;
  if (!Annotation.ends_with(")"))
    return false;
  size_t Open = Annotation.find('(');
  if (Open == StringRef::npos)
    return false;
  Family = Annotation.take_front(Open).trim();
  StringRef Parameters = Annotation.slice(Open + 1, Annotation.size() - 1);
  while (!Parameters.empty()) {
    auto [Name, Rest] = Parameters.split(',');
    Name = Name.trim();
    bool Found = false;
    for (unsigned Index = 0; Index < Function->getNumParams(); ++Index)
      if (Function->getParamDecl(Index)->getName() == Name) {
        Arguments.push_back(Index);
        Found = true;
        break;
      }
    if (!Found)
      return false;
    Parameters = Rest;
  }
  return !Family.empty();
}

struct SpanContract {
  MemoryTokenOperation Operation;
  unsigned Pointer;
  unsigned Length;
};

struct DisjointContract {
  MemoryTokenOperation Operation;
  unsigned First;
  unsigned Second;
  unsigned Length;
};

static bool operator==(const SpanContract &Left, const SpanContract &Right) {
  return Left.Operation == Right.Operation && Left.Pointer == Right.Pointer &&
         Left.Length == Right.Length;
}

static bool operator==(const DisjointContract &Left,
                       const DisjointContract &Right) {
  return Left.Operation == Right.Operation && Left.First == Right.First &&
         Left.Second == Right.Second && Left.Length == Right.Length;
}

static void tokenContracts(const FunctionDecl *Function,
                           SmallVectorImpl<SpanContract> &Spans,
                           SmallVectorImpl<DisjointContract> &Disjoint) {
  if (!Function)
    return;
  for (const FunctionDecl *Redeclaration : Function->redecls()) {
    for (unsigned Pointer = 0; Pointer < Redeclaration->getNumParams();
         ++Pointer) {
      for (const AnnotateAttr *Attribute : Redeclaration->getParamDecl(Pointer)
                                               ->specific_attrs<AnnotateAttr>()) {
        StringRef Family;
        SmallVector<unsigned, 2> Arguments;
        MemoryTokenOperation Operation;
        if (!tokenApplication(Redeclaration, Attribute->getAnnotation(),
                              Operation, Family, Arguments))
          continue;
        const TypedefNameDecl *Token =
            dialectToken(Function->getASTContext(), Family);
        if (hasDialectQualifier(Token, "qual:extent_at_least") &&
            Arguments.size() == 1) {
          SpanContract Contract{Operation, Pointer, Arguments[0]};
          if (llvm::find(Spans, Contract) == Spans.end())
            Spans.push_back(Contract);
        }
        if (hasDialectQualifier(Token, "qual:disjoint_extent") &&
            Arguments.size() == 2) {
          DisjointContract Contract{
              Operation, Pointer, Arguments[0], Arguments[1]};
          if (llvm::find(Disjoint, Contract) == Disjoint.end())
            Disjoint.push_back(Contract);
        }
      }
    }
  }
}

class MemoryContractChecker
    : public Checker<check::PreCall, check::PostCall, check::BeginFunction,
                     check::EndFunction, check::Bind> {
  mutable std::unique_ptr<BugType> SpanBT;
  mutable std::unique_ptr<BugType> OverlapBT;
  mutable std::unique_ptr<BugType> TokenBT;
  mutable std::unique_ptr<BugType> RedundantBT;

  static bool hasName(const CallEvent &Call, StringRef Wanted) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    return Function && Function->getIdentifier() &&
           Function->getName() == Wanted;
  }

  static bool isManualProofCall(const FunctionDecl *Function) {
    return Function && Function->getIdentifier() &&
           Function->getName().starts_with("__ownership_");
  }

  /* A pointer-returning function carries its byte extent on the function
   * declaration itself: `withtok(writable_span(size))`.  Interpreting the
   * function-level token as a return-value grant keeps allocator knowledge in
   * headers and stubs; the checker never recognizes an allocator by name. */
  static std::optional<SVal>
  declaredReturnSpanExtent(const FunctionDecl *Function,
                           const CallEvent &Call, CheckerContext &C) {
    if (!Function || !Function->getReturnType()->isPointerType())
      return std::nullopt;
    auto ParameterIndex = [](const FunctionDecl *Declaration,
                             StringRef Name) -> std::optional<unsigned> {
      Name = Name.trim();
      for (unsigned Index = 0; Index < Declaration->getNumParams(); ++Index)
        if (Declaration->getParamDecl(Index)->getName() == Name)
          return Index;
      return std::nullopt;
    };
    for (const FunctionDecl *Redeclaration : Function->redecls())
      for (const AnnotateAttr *Attribute :
           Redeclaration->specific_attrs<AnnotateAttr>()) {
        StringRef Annotation = Attribute->getAnnotation();
        if (!Annotation.consume_front("withtok:") ||
            !Annotation.ends_with(")"))
          continue;
        size_t Open = Annotation.find('(');
        if (Open == StringRef::npos)
          continue;
        StringRef Family = Annotation.take_front(Open).trim();
        const TypedefNameDecl *Token =
            dialectToken(Function->getASTContext(), Family);
        if (!hasDialectQualifier(Token, "qual:extent_at_least"))
          continue;
        StringRef Expression =
            Annotation.slice(Open + 1, Annotation.size() - 1).trim();
        auto [LeftName, RightName] = Expression.split('*');
        std::optional<unsigned> Left = ParameterIndex(Redeclaration, LeftName);
        if (!Left || *Left >= Call.getNumArgs())
          continue;
        if (RightName.empty())
          return Call.getArgSVal(*Left);
        std::optional<unsigned> Right =
            ParameterIndex(Redeclaration, RightName);
        if (!Right || *Right >= Call.getNumArgs() ||
            RightName.contains('*'))
          continue;
        return C.getSValBuilder().evalBinOp(
            C.getState(), BO_Mul, Call.getArgSVal(*Left),
            Call.getArgSVal(*Right), C.getASTContext().getSizeType());
      }
    return std::nullopt;
  }

  static SymbolRef stripCasts(SymbolRef Symbol) {
    while (const auto *Cast = dyn_cast_or_null<SymbolCast>(Symbol))
      Symbol = Cast->getOperand();
    return Symbol;
  }

  // Decompose an SVal into (root symbol, constant offset), i.e. treat it
  // as "root + offset" for a bare symbol (offset 0) or a `root + K`
  // SymIntExpr (offset K). Clang's range-based constraint solver does not
  // fold "(S + K) >= S" for separately-built compound expressions, so the
  // decomposition exposes their common root.  Offset ordering alone is not
  // a proof, however: sameSymbolSpanProven also asks the solver to exclude
  // wrap before using it.
  static bool decomposeAffine(SVal V, SymbolRef &Base, int64_t &Offset) {
    SymbolRef Original = V.getAsSymbol();
    SymbolRef Sym = stripCasts(Original);
    if (!Sym || Sym != Original)
      return false;
    if (const auto *IntExpr = dyn_cast<SymIntExpr>(Sym)) {
      if (IntExpr->getOpcode() != BO_Add)
        return false;
      const llvm::APSInt &Constant = IntExpr->getRHS();
      /* Negative offsets need a corresponding lower-bound/underflow proof,
       * and constants outside int64_t cannot be ordered by Offset below.
       * Neither case occurs in the size expressions this lemma targets. */
      if (Constant.isNegative() || Constant.getActiveBits() > 63)
        return false;
      Base = IntExpr->getLHS();
      if (isa<SymbolCast>(Base))
        return false;
      Offset = static_cast<int64_t>(Constant.getZExtValue());
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
  // same way subsumes that narrower case (offset 0 on the length side)
  // while also covering this one (equal, nonzero offsets on both sides),
  // subject to the solver-backed no-wrap proof below.
  static bool sameSymbolSpanProven(SVal Extent, SVal Length,
                                   ProgramStateRef State,
                                   CheckerContext &C) {
    SymbolRef ExtentSymbol = Extent.getAsSymbol();
    SymbolRef LengthSymbol = Length.getAsSymbol();
    /* Identical symbolic values compare equal even if their common
     * expression wrapped before reaching this point. */
    if (ExtentSymbol && ExtentSymbol == LengthSymbol)
      return true;
    const auto *ExtentProduct = dyn_cast_or_null<SymSymExpr>(ExtentSymbol);
    const auto *LengthProduct = dyn_cast_or_null<SymSymExpr>(LengthSymbol);
    if (ExtentProduct && LengthProduct &&
        ExtentProduct->getOpcode() == BO_Mul &&
        LengthProduct->getOpcode() == BO_Mul) {
      SymbolRef EL = stripCasts(ExtentProduct->getLHS());
      SymbolRef ER = stripCasts(ExtentProduct->getRHS());
      SymbolRef LL = stripCasts(LengthProduct->getLHS());
      SymbolRef LR = stripCasts(LengthProduct->getRHS());
      if ((EL == LL && ER == LR) || (EL == LR && ER == LL))
        return true;
    }
    SymbolRef ExtentBase, LengthBase;
    int64_t ExtentOffset, LengthOffset;
    if (!decomposeAffine(Extent, ExtentBase, ExtentOffset))
      return false;
    if (!decomposeAffine(Length, LengthBase, LengthOffset))
      return false;
    if (ExtentBase != LengthBase || ExtentOffset < LengthOffset)
      return false;
    QualType BaseType = ExtentBase->getType();
    if (!BaseType->isIntegerType())
      return false;
    if (ExtentOffset == LengthOffset)
      return true;

    /* Offset ordering is valid only when the larger addition cannot wrap.
     * Ask the path solver to prove base <= TYPE_MAX - ExtentOffset; a
     * syntactic shared base alone is not enough for unsigned size_t (for
     * example SIZE_MAX + 1 wraps to zero). */
    llvm::APSInt Limit =
        C.getSValBuilder().getBasicValueFactory().getMaxValue(BaseType);
    llvm::APSInt Delta(llvm::APInt(Limit.getBitWidth(), ExtentOffset),
                       Limit.isUnsigned());
    Limit -= Delta;
    const llvm::APSInt *Maximum = C.getSValBuilder().getMaxValue(
        State, C.getSValBuilder().makeSymbolVal(ExtentBase));
    return Maximum && *Maximum <= Limit;
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
      StringRef Title = "Unproven memory overlap";
      if (Reason == "memory operation span is not proven valid")
        Title = "Unproven memory span";
      else if (Reason == "manual memory proof axiom is redundant")
        Title = "Redundant memory proof axiom";
      std::unique_ptr<BugType> New = std::make_unique<BugType>(
          this, Title, categories::MemoryError);
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

  void reportToken(StringRef Reason, const Stmt *Statement,
                   ProgramStateRef State, CheckerContext &C) const {
    if (!Statement)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!TokenBT)
      TokenBT = std::make_unique<BugType>(this,
                                          "Unproven memory token contract",
                                          categories::MemoryError);
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        (Reason + "; origin '" +
         SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
         "'; context '" + context(C) + "'; expression '" + text(Statement, C) +
         "'; site '" + site(Statement, C) + "'")
            .str();
    auto Report =
        std::make_unique<PathSensitiveBugReport>(*TokenBT, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  bool spanProven(SVal Pointer, SVal Length, ProgramStateRef State,
                  CheckerContext &C, bool UseAssumedSpans = true) const {
    if (State->isNull(Length).isConstrainedTrue())
      return true;
    auto ExtentProvesLength = [&](SVal Extent) {
      if (Extent.isUnknownOrUndef() || Length.isUnknownOrUndef())
        return false;
      if (sameSymbolSpanProven(Extent, Length, State, C))
        return true;
      SVal Enough = C.getSValBuilder().evalBinOp(
          State, BO_GE, Extent, Length,
          C.getSValBuilder().getConditionType());
      std::optional<DefinedOrUnknownSVal> Condition =
          Enough.getAs<DefinedOrUnknownSVal>();
      return Condition && !State->assume(*Condition, false);
    };
    if (UseAssumedSpans)
      if (const MemRegion *Region = Pointer.getAsRegion())
        if (const DefinedOrUnknownSVal *Assumed =
                State->get<AssumedSpanExtent>(Region))
          if (ExtentProvesLength(*Assumed))
            return true;
    SVal Extent = getDynamicExtentWithOffset(State, Pointer);
    if (Extent.isUnknownOrUndef() || Length.isUnknownOrUndef())
      return false;
    if (sameSymbolSpanProven(Extent, Length, State, C))
      return true;
    if (stringLengthSourceSpanProven(Pointer, Length, State))
      return true;
    return ExtentProvesLength(Extent);
  }

  bool overlapProven(SVal First, SVal Second, SVal Length,
                     ProgramStateRef State, CheckerContext &C,
                     bool UseAssumedSpans = true) const {
    if (State->isNull(Length).isConstrainedTrue())
      return true;
    const MemRegion *A = First.getAsRegion();
    const MemRegion *B = Second.getAsRegion();
    if (!A || !B)
      return false;
    if (UseAssumedSpans)
      if (const DefinedOrUnknownSVal *Assumed =
              State->get<AssumedDisjointExtent>({A, B})) {
        SVal Enough = C.getSValBuilder().evalBinOp(
            State, BO_GE, *Assumed, Length,
            C.getSValBuilder().getConditionType());
        if (std::optional<DefinedOrUnknownSVal> Condition =
                Enough.getAs<DefinedOrUnknownSVal>())
          if (!State->assume(*Condition, false))
            return true;
      }
    RegionOffset AO = A->getAsOffset();
    RegionOffset BO = B->getAsOffset();
    if (!AO.isValid() || !BO.isValid())
      return false;
    if (AO.getRegion() != BO.getRegion()) {
      /* Two symbolic pointer parameters may still alias even though Clang
       * represents their pointees with distinct SymbolicRegions.  Distinct
       * concrete storage objects (separate locals/globals) cannot overlap;
       * distinct symbolic roots alone are not such a proof. */
      if (AO.getRegion()->getMemorySpace() != BO.getRegion()->getMemorySpace())
        return true;
      if (State->contains<AllocatedBaseRegion>(AO.getRegion()) ||
          State->contains<AllocatedBaseRegion>(BO.getRegion()))
        return true;
      if (isa<SymbolicRegion>(AO.getRegion()) ||
          isa<SymbolicRegion>(BO.getRegion()))
        return false;
      return true;
    }
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

  static DefinedOrUnknownSVal protocolSucceeded(
      const FunctionDecl *Function, DefinedOrUnknownSVal Return,
      SValBuilder &Builder, ProgramStateRef State) {
    DefinedOrUnknownSVal IsZero = Builder.evalEQ(
        State, Return, Builder.makeZeroVal(Function->getReturnType()));
    if (!Function->getReturnType()->isPointerType())
      return IsZero;
    return Builder
        .evalBinOp(State, BO_EQ, IsZero, Builder.makeTruthVal(false),
                   Builder.getConditionType())
        .castAs<DefinedOrUnknownSVal>();
  }

  static const ParmVarDecl *argumentParameter(const CallEvent &Call,
                                              unsigned Argument) {
    if (Argument >= Call.getNumArgs())
      return nullptr;
    const Expr *Expression = Call.getArgExpr(Argument);
    Expression = Expression ? Expression->IgnoreParenImpCasts() : nullptr;
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Expression);
    return Reference ? dyn_cast<ParmVarDecl>(Reference->getDecl()) : nullptr;
  }

  static ProgramStateRef applyGrants(
      ProgramStateRef State, const CallEvent &Call,
      ArrayRef<SpanContract> Spans, ArrayRef<DisjointContract> Disjoint) {
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Grant ||
          Contract.Pointer >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs())
        continue;
      const MemRegion *Region =
          Call.getArgSVal(Contract.Pointer).getAsRegion();
      std::optional<DefinedOrUnknownSVal> Extent =
          Call.getArgSVal(Contract.Length).getAs<DefinedOrUnknownSVal>();
      if (Region && Extent)
        State = State->set<AssumedSpanExtent>(Region, *Extent);
    }
    for (const DisjointContract &Contract : Disjoint) {
      if (Contract.Operation != MemoryTokenOperation::Grant ||
          Contract.First >= Call.getNumArgs() ||
          Contract.Second >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs())
        continue;
      const MemRegion *First = Call.getArgSVal(Contract.First).getAsRegion();
      const MemRegion *Second = Call.getArgSVal(Contract.Second).getAsRegion();
      std::optional<DefinedOrUnknownSVal> Extent =
          Call.getArgSVal(Contract.Length).getAs<DefinedOrUnknownSVal>();
      if (First && Second && Extent)
        State = State->set<AssumedDisjointExtent>({First, Second}, *Extent);
    }
    return State;
  }

  static ProgramStateRef recordGrantProofs(
      ProgramStateRef State, const CallEvent &Call,
      ArrayRef<SpanContract> Spans, ArrayRef<DisjointContract> Disjoint) {
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Grant ||
          Contract.Pointer >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs())
        continue;
      const ParmVarDecl *Parameter =
          argumentParameter(Call, Contract.Pointer);
      const ParmVarDecl *Length = argumentParameter(Call, Contract.Length);
      if (Parameter && Length)
        State = State->set<GrantedSpanProof>(Parameter, Length);
    }
    for (const DisjointContract &Contract : Disjoint) {
      if (Contract.Operation != MemoryTokenOperation::Grant ||
          Contract.First >= Call.getNumArgs() ||
          Contract.Second >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs())
        continue;
      const ParmVarDecl *First = argumentParameter(Call, Contract.First);
      const ParmVarDecl *Second = argumentParameter(Call, Contract.Second);
      const ParmVarDecl *Length = argumentParameter(Call, Contract.Length);
      if (First && Second && Length)
        State = State->set<GrantedDisjointProof>({First, Second}, Length);
    }
    return State;
  }

  static ProgramStateRef removeGrantProofs(
      ProgramStateRef State, const CallEvent &Call,
      ArrayRef<SpanContract> Spans, ArrayRef<DisjointContract> Disjoint) {
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Grant)
        continue;
      if (const ParmVarDecl *Parameter =
              argumentParameter(Call, Contract.Pointer))
        State = State->remove<GrantedSpanProof>(Parameter);
    }
    for (const DisjointContract &Contract : Disjoint) {
      if (Contract.Operation != MemoryTokenOperation::Grant)
        continue;
      const ParmVarDecl *First = argumentParameter(Call, Contract.First);
      const ParmVarDecl *Second = argumentParameter(Call, Contract.Second);
      if (First && Second)
        State = State->remove<GrantedDisjointProof>({First, Second});
    }
    return State;
  }

public:
  void checkBind(SVal Location, SVal, const Stmt *, CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    const MemRegion *Bound = Location.getAsRegion();
    if (!Function || !Bound)
      return;
    ProgramStateRef State = C.getState();
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      if (State->getLValue(Parameter, C.getLocationContext()).getAsRegion() !=
          Bound)
        continue;
      SmallVector<const ParmVarDecl *, 2> RemoveSpans;
      for (const auto &Proof : State->get<GrantedSpanProof>())
        if (Proof.first == Parameter || Proof.second == Parameter)
          RemoveSpans.push_back(Proof.first);
      for (const ParmVarDecl *Key : RemoveSpans)
        State = State->remove<GrantedSpanProof>(Key);
      SmallVector<DisjointParameterKey, 2> Remove;
      for (const auto &Proof : State->get<GrantedDisjointProof>())
        if (Proof.first.first == Parameter || Proof.first.second == Parameter ||
            Proof.second == Parameter)
          Remove.push_back(Proof.first);
      for (const DisjointParameterKey &Key : Remove)
        State = State->remove<GrantedDisjointProof>(Key);
      break;
    }
    if (State != C.getState())
      C.addTransition(State);
  }

  void checkBeginFunction(CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    SmallVector<SpanContract, 2> Spans;
    SmallVector<DisjointContract, 1> Disjoint;
    tokenContracts(Function, Spans, Disjoint);
    ProgramStateRef State = C.getState();
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Require)
        continue;
      const ParmVarDecl *PointerParameter =
          Function->getParamDecl(Contract.Pointer);
      const ParmVarDecl *LengthParameter =
          Function->getParamDecl(Contract.Length);
      SVal PointerValue = State->getSVal(
          State->getLValue(PointerParameter, C.getLocationContext()));
      SVal LengthValue = State->getSVal(
          State->getLValue(LengthParameter, C.getLocationContext()));
      const MemRegion *Region = PointerValue.getAsRegion();
      std::optional<DefinedOrUnknownSVal> DefinedLength =
          LengthValue.getAs<DefinedOrUnknownSVal>();
      if (Region && DefinedLength)
        State = State->set<AssumedSpanExtent>(Region, *DefinedLength);
    }
    for (const DisjointContract &Contract : Disjoint) {
      if (Contract.Operation != MemoryTokenOperation::Require)
        continue;
      const ParmVarDecl *First = Function->getParamDecl(Contract.First);
      const ParmVarDecl *Second = Function->getParamDecl(Contract.Second);
      const ParmVarDecl *Length = Function->getParamDecl(Contract.Length);
      const MemRegion *A = State->getSVal(
          State->getLValue(First, C.getLocationContext())).getAsRegion();
      const MemRegion *B = State->getSVal(
          State->getLValue(Second, C.getLocationContext())).getAsRegion();
      std::optional<DefinedOrUnknownSVal> Extent = State->getSVal(
          State->getLValue(Length, C.getLocationContext()))
          .getAs<DefinedOrUnknownSVal>();
      if (A && B && Extent)
        State = State->set<AssumedDisjointExtent>({A, B}, *Extent);
    }
    if (State != C.getState())
      C.addTransition(State);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    SmallVector<SpanContract, 2> Spans;
    SmallVector<DisjointContract, 1> Disjoint;
    tokenContracts(Function, Spans, Disjoint);
    ProgramStateRef ContractState = C.getState();
    if (isManualProofCall(Function)) {
      bool Reported = false;
      for (const SpanContract &Contract : Spans) {
        if (Contract.Operation != MemoryTokenOperation::Grant ||
            Contract.Pointer >= Call.getNumArgs() ||
            Contract.Length >= Call.getNumArgs())
          continue;
        if (spanProven(Call.getArgSVal(Contract.Pointer),
                       Call.getArgSVal(Contract.Length), C.getState(), C,
                       false)) {
          BugType *Type = RedundantBT.get();
          report("manual memory proof axiom is redundant", Type, Call,
                 C.getState(), C);
          if (!RedundantBT && Type)
            RedundantBT.reset(Type);
          Reported = true;
          break;
        }
      }
      if (!Reported) {
        for (const DisjointContract &Contract : Disjoint) {
          if (Contract.Operation != MemoryTokenOperation::Grant ||
              Contract.First >= Call.getNumArgs() ||
              Contract.Second >= Call.getNumArgs() ||
              Contract.Length >= Call.getNumArgs())
            continue;
          if (overlapProven(Call.getArgSVal(Contract.First),
                            Call.getArgSVal(Contract.Second),
                            Call.getArgSVal(Contract.Length), C.getState(),
                            C, false)) {
            BugType *Type = RedundantBT.get();
            report("manual memory proof axiom is redundant", Type, Call,
                   C.getState(), C);
            if (!RedundantBT && Type)
              RedundantBT.reset(Type);
            break;
          }
        }
      }
    }
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Require)
        continue;
      if (Contract.Pointer >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs())
        continue;
      const MemRegion *Region =
          Call.getArgSVal(Contract.Pointer).getAsRegion();
      std::optional<DefinedOrUnknownSVal> DefinedLength =
          Call.getArgSVal(Contract.Length).getAs<DefinedOrUnknownSVal>();
      if (Region && DefinedLength)
        ContractState =
            ContractState->set<AssumedSpanExtent>(Region, *DefinedLength);
    }
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Require)
        continue;
      if (Contract.Pointer >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs())
        continue;
      if (!spanProven(Call.getArgSVal(Contract.Pointer),
                      Call.getArgSVal(Contract.Length), C.getState(), C)) {
        BugType *Type = SpanBT.get();
        report("memory operation span is not proven valid", Type, Call,
               ContractState, C);
        if (!SpanBT && Type)
          SpanBT.reset(Type);
        break;
      }
    }
    for (const DisjointContract &Contract : Disjoint) {
      if (Contract.Operation != MemoryTokenOperation::Require)
        continue;
      if (Contract.First >= Call.getNumArgs() ||
          Contract.Second >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs())
        continue;
      SVal First = Call.getArgSVal(Contract.First);
      SVal Second = Call.getArgSVal(Contract.Second);
      SVal Length = Call.getArgSVal(Contract.Length);
      if (!overlapProven(First, Second, Length, C.getState(), C)) {
        BugType *Type = OverlapBT.get();
        report("memcpy ranges are not proven nonoverlapping", Type, Call,
               C.getState(), C);
        if (!OverlapBT && Type)
          OverlapBT.reset(Type);
      }
      const MemRegion *A = First.getAsRegion();
      const MemRegion *B = Second.getAsRegion();
      std::optional<DefinedOrUnknownSVal> Extent =
          Length.getAs<DefinedOrUnknownSVal>();
      if (A && B && Extent)
        ContractState =
            ContractState->set<AssumedDisjointExtent>({A, B}, *Extent);
    }
    ContractState =
        recordGrantProofs(ContractState, Call, Spans, Disjoint);
    if (ContractState != C.getState())
      C.addTransition(ContractState);
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (std::optional<SVal> Extent =
            declaredReturnSpanExtent(Function, Call, C)) {
      if (std::optional<DefinedOrUnknownSVal> DefinedSize =
              Extent->getAs<DefinedOrUnknownSVal>())
        if (const MemRegion *Region = Call.getReturnValue().getAsRegion()) {
          const MemRegion *Base = Region->getBaseRegion();
          State = setDynamicExtent(State, Base, *DefinedSize,
                                   C.getSValBuilder());
          State = State->add<AllocatedBaseRegion>(Base);
        }
      }
    // See stringLengthSourceSpanProven above: record which pointer
    // argument a strlen()/strnlen() call's return symbol was measured
    // from, so a later memcpy/memset/etc using that same (conjured)
    // length against that same pointer can be recognized as in-bounds
    // by construction.
    bool IsStrlen = hasName(Call, "strlen") && Call.getNumArgs() >= 1;
    bool IsStrnlen = hasName(Call, "strnlen") && Call.getNumArgs() >= 2;
    if (IsStrlen || IsStrnlen) {
      const MemRegion *ArgRegion = Call.getArgSVal(0).getAsRegion();
      SymbolRef ReturnSym = stripCasts(Call.getReturnValue().getAsSymbol());
      if (ArgRegion && ReturnSym)
        State = IsStrlen
                    ? State->set<StrlenSource>(ReturnSym,
                                               ArgRegion->getBaseRegion())
                    : State->set<StrnlenSource>(ReturnSym,
                                                ArgRegion->getBaseRegion());
    }

    SmallVector<SpanContract, 2> Spans;
    SmallVector<DisjointContract, 1> Disjoint;
    tokenContracts(Function, Spans, Disjoint);
    bool HasGrant = llvm::any_of(Spans, [](const SpanContract &Contract) {
      return Contract.Operation == MemoryTokenOperation::Grant;
    }) || llvm::any_of(Disjoint, [](const DisjointContract &Contract) {
      return Contract.Operation == MemoryTokenOperation::Grant;
    });
    if (!HasGrant) {
      if (State != C.getState())
        C.addTransition(State);
      return;
    }
    if (Function->getReturnType()->isVoidType()) {
      C.addTransition(applyGrants(State, Call, Spans, Disjoint));
      return;
    }
    std::optional<DefinedOrUnknownSVal> Return =
        Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
    if (!Return) {
      ProgramStateRef Unproven =
          removeGrantProofs(State, Call, Spans, Disjoint);
      if (Unproven != C.getState())
        C.addTransition(Unproven);
      return;
    }
    DefinedOrUnknownSVal Success = protocolSucceeded(
        Function, *Return, C.getSValBuilder(), State);
    auto [Succeeded, Failed] = State->assume(Success);
    if (Succeeded)
      C.addTransition(applyGrants(Succeeded, Call, Spans, Disjoint));
    if (Failed)
      C.addTransition(removeGrantProofs(Failed, Call, Spans, Disjoint));
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function || !Function->doesThisDeclarationHaveABody())
      return;
    SmallVector<SpanContract, 2> Spans;
    SmallVector<DisjointContract, 1> Disjoint;
    tokenContracts(Function, Spans, Disjoint);
    ProgramStateRef State = C.getState();
    if (!Function->getReturnType()->isVoidType()) {
      if (!Return || !Return->getRetValue())
        return;
      std::optional<DefinedOrUnknownSVal> Result =
          C.getSVal(Return->getRetValue()).getAs<DefinedOrUnknownSVal>();
      if (!Result)
        return;
      DefinedOrUnknownSVal Success = protocolSucceeded(
          Function, *Result, C.getSValBuilder(), State);
      State = State->assume(Success).first;
      if (!State)
        return;
    }
    const LocationContext *LC = C.getLocationContext();
    const Stmt *Site =
        Return ? static_cast<const Stmt *>(Return) : Function->getBody();
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Grant)
        continue;
      const ParmVarDecl *Pointer = Function->getParamDecl(Contract.Pointer);
      const ParmVarDecl *Length = Function->getParamDecl(Contract.Length);
      SVal PointerValue =
          State->getSVal(State->getLValue(Pointer, LC));
      SVal LengthValue = State->getSVal(State->getLValue(Length, LC));
      const MemRegion *Region = PointerValue.getAsRegion();
      bool Proven = State->isNull(LengthValue).isConstrainedTrue();
      if (!Proven)
        Proven = spanProven(PointerValue, LengthValue, State, C, false);
      if (Region)
        if (const ParmVarDecl *const *ProvenLength =
                State->get<GrantedSpanProof>(Pointer)) {
          if (*ProvenLength == Length) {
            if (std::optional<DefinedOrUnknownSVal> Extent =
                    LengthValue.getAs<DefinedOrUnknownSVal>()) {
              ProgramStateRef ProofState =
                  State->set<AssumedSpanExtent>(Region, *Extent);
              Proven = spanProven(PointerValue, LengthValue, ProofState, C);
            }
          }
        }
      if (!Proven) {
        reportToken("declared memory token addition is not proven by function "
                    "body",
                    Site, State, C);
        return;
      }
    }
    for (const DisjointContract &Contract : Disjoint) {
      if (Contract.Operation != MemoryTokenOperation::Grant)
        continue;
      const ParmVarDecl *First = Function->getParamDecl(Contract.First);
      const ParmVarDecl *Second = Function->getParamDecl(Contract.Second);
      const ParmVarDecl *Length = Function->getParamDecl(Contract.Length);
      SVal FirstValue = State->getSVal(State->getLValue(First, LC));
      SVal SecondValue = State->getSVal(State->getLValue(Second, LC));
      SVal LengthValue = State->getSVal(State->getLValue(Length, LC));
      const MemRegion *FirstRegion = FirstValue.getAsRegion();
      const MemRegion *SecondRegion = SecondValue.getAsRegion();
      bool Proven = State->isNull(LengthValue).isConstrainedTrue();
      if (FirstRegion && SecondRegion)
        if (const ParmVarDecl *const *ProvenLength =
                State->get<GrantedDisjointProof>({First, Second})) {
          if (*ProvenLength == Length) {
            if (std::optional<DefinedOrUnknownSVal> Extent =
                    LengthValue.getAs<DefinedOrUnknownSVal>()) {
              ProgramStateRef ProofState =
                  State->set<AssumedDisjointExtent>(
                      {FirstRegion, SecondRegion}, *Extent);
              Proven = overlapProven(FirstValue, SecondValue, LengthValue,
                                     ProofState, C);
            }
          }
        }
      if (!Proven) {
        reportToken("declared memory token addition is not proven by function "
                    "body",
                    Site, State, C);
        return;
      }
    }
  }
};

} // namespace

void registerMemoryContractChecker(CheckerRegistry &Registry) {
  Registry.addChecker<MemoryContractChecker>(
      "ntlibc.MemoryContract",
      "Proves memory spans and memcpy non-overlap contracts", "");
}

#ifndef OWNERSHIP_CHECKER_BUNDLE
extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  registerMemoryContractChecker(Registry);
}
#endif
