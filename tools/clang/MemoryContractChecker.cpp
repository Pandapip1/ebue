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
#include "clang/StaticAnalyzer/Core/PathSensitive/RangedConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "TokenAlgebra.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"

#ifdef NTLIBC_MEMORY_CONTRACT_Z3
#include "ExactCScalarSMT.h"
#include "z3++.h"
#endif

#include <cctype>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

// strlen(s)'s contract is "bytes before s's first NUL", so s is
// guaranteed at least that many bytes plus the terminator -- reading
// strlen(s) + up-to-1 bytes is always safe. strnlen(s, n)'s contract is
// looser: either it found a real terminator within n bytes (same "+1"
// reasoning) or it read all n bytes without finding one, so only n (not
// n+1) bytes are known-safe -- a slack bound of 0, not 1. Recording
// "this conjured return symbol came from strlen/strnlen(s)" at the call
// lets spanProven recognize the common "n = strlen(s) + 1; p =
// __malloc(n); memcpy(p, s, n);" idiom's source argument as in-bounds --
// this tree's own xstrdup and strdup.c/strndup.c share this shape.
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
REGISTER_MAP_WITH_PROGRAMSTATE(AllocatedSpanExtent, const MemRegion *,
                               DefinedOrUnknownSVal)
REGISTER_MAP_WITH_PROGRAMSTATE(GrantedSpanProof, const ParmVarDecl *,
                               const ParmVarDecl *)
using DisjointParameterKey =
    std::pair<const ParmVarDecl *, const ParmVarDecl *>;
REGISTER_MAP_WITH_PROGRAMSTATE(GrantedDisjointProof, DisjointParameterKey,
                               const ParmVarDecl *)
using SymbolRelation = std::pair<SymbolRef, SymbolRef>;
REGISTER_SET_WITH_PROGRAMSTATE(ProvenLessEqual, SymbolRelation)
REGISTER_SET_WITH_PROGRAMSTATE(ProvenLessThan, SymbolRelation)

/* Persisted, path-sensitive tracking for struct fields carrying a
 * readable_elements/writable_elements-style withtok(family(length))
 * contract (see FieldSpanContract below): fieldSpanContract/assumeFieldSpan
 * only ever read these annotations as a transient snapshot to satisfy an
 * OUTGOING call's precondition -- nothing previously enforced that a
 * struct's pointer field and its paired length field actually stay in
 * agreement with each other as the struct is mutated. A helper that
 * reallocates the pointer field without updating the length field (or vice
 * versa) previously went completely undetected.
 *
 * The two fields are written by two separate statements, not atomically
 * (`lb->v = g; lb->cap = newcap;` in src/util/patch.c, or -- the opposite
 * order -- `out.n = out.cap = pglob->gl_pathc;` before `out.v =
 * __malloc(...)` in src/glob/glob.c), so checking the invariant eagerly at
 * every single write would false-positive on the intermediate state of
 * whichever field is written first. checkBind (below) only records which
 * (aggregate, pointer field, length field) triple was disturbed and by
 * which statement; the actual proof is deferred to checkEndFunction (the
 * same stable point GrantedSpanProof's own deferred check already uses),
 * where both fields necessarily hold their settled values for this
 * update -- a CompoundStmt's own exit was tried first and dropped: Clang's
 * ExprEngine never visits an ordinary `{ ... }` block (function body,
 * if-body, ...) as a Stmt in its own right the way it visits expressions,
 * so check::PostStmt<CompoundStmt> silently never fires for one; only a
 * GNU statement-expression (`({ ... })`) would reach it, which is not
 * this shape. checkEndFunction fires exactly once per path regardless,
 * so it alone is both necessary and sufficient here. The proof itself
 * reuses spanProven exactly as the call-boundary check above does, but
 * against the pointer's real DynamicExtent rather than an unverified
 * assumption.
 *
 * The map's VALUE is a snapshot of both fields, not just a Stmt* to
 * re-read the store from later: liveness-driven dead-binding cleanup
 * (SymbolReaper/ProgramState::cleanupState) is free to discard a struct
 * field's binding once nothing downstream reads it again -- which is
 * exactly the common case for a helper that writes a struct's pointer and
 * length fields and then returns without itself reading either back. A
 * deferred re-read via getLValue/getSVal at checkEndFunction can
 * therefore silently observe Unknown for a field this checker itself
 * just watched get bound (confirmed empirically: an earlier version of
 * this mechanism that deferred the read, not just the proof, saw exactly
 * this). Capturing each write's own new value (the Bind callback's own
 * Value parameter) and the OTHER field's then-current value together, at
 * the moment of the write -- carrying the peer value forward from any
 * earlier snapshot for the same key rather than re-deriving it --
 * sidesteps that pruning entirely: by the time the deferred check runs,
 * both slots already hold whatever was last known, with no further store
 * query required. */
using RecordSpanTouchKey =
    std::pair<std::pair<const MemRegion *, const FieldDecl *>,
             const FieldDecl *>;
using RecordSpanSnapshot =
    std::pair<std::pair<DefinedOrUnknownSVal, DefinedOrUnknownSVal>,
             const Stmt *>;
REGISTER_MAP_WITH_PROGRAMSTATE(TouchedRecordSpan, RecordSpanTouchKey,
                               RecordSpanSnapshot)

namespace {

using ntlibc::algebra::findTokenSort;
using ntlibc::algebra::hasQualifier;

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
    SmallVector<StringRef, 2> Factors;
    while (!Name.empty()) {
      auto [Factor, Remaining] = Name.split('*');
      Factors.push_back(Factor.trim());
      Name = Remaining;
    }
    if (Factors.size() > 2)
      return false;
    for (StringRef Factor : Factors) {
      bool Found = false;
      for (unsigned Index = 0; Index < Function->getNumParams(); ++Index)
        if (Function->getParamDecl(Index)->getName() == Factor) {
          Arguments.push_back(Index);
          Found = true;
          break;
        }
      if (!Found)
        return false;
      }
    Parameters = Rest;
  }
  return !Family.empty();
}

struct SpanContract {
  MemoryTokenOperation Operation;
  unsigned Pointer;
  unsigned Length;
  unsigned Multiplier;
  uint64_t Scale;
};

struct DisjointContract {
  MemoryTokenOperation Operation;
  unsigned First;
  unsigned Second;
  unsigned Length;
};

static bool operator==(const SpanContract &Left, const SpanContract &Right) {
  return Left.Operation == Right.Operation && Left.Pointer == Right.Pointer &&
         Left.Length == Right.Length && Left.Multiplier == Right.Multiplier &&
         Left.Scale == Right.Scale;
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
            findTokenSort(Function->getASTContext(), Family);
        bool ByteExtent = hasQualifier(Token, "qual:extent_at_least");
        bool ElementExtent = hasQualifier(Token, "qual:element_extent");
        if ((ByteExtent || ElementExtent) &&
            (Arguments.size() == 1 || Arguments.size() == 2)) {
          uint64_t Scale = 1;
          if (ElementExtent) {
            QualType Type = Redeclaration->getParamDecl(Pointer)->getType();
            if (!Type->isPointerType() ||
                Type->getPointeeType()->isIncompleteType())
              continue;
            Scale = Function->getASTContext()
                        .getTypeSizeInChars(Type->getPointeeType())
                        .getQuantity();
          }
          SpanContract Contract{Operation, Pointer, Arguments[0],
                                Arguments.size() == 2
                                    ? Arguments[1]
                                    : std::numeric_limits<unsigned>::max(),
                                Scale};
          if (llvm::find(Spans, Contract) == Spans.end())
            Spans.push_back(Contract);
        }
        if (hasQualifier(Token, "qual:disjoint_extent") &&
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

static SVal scaledSpanLength(const SpanContract &Contract, SVal Count,
                             SVal Multiplier, ProgramStateRef State,
                             SValBuilder &Builder) {
  if (Count.isUnknownOrUndef())
    return Count;
  if (Contract.Multiplier != std::numeric_limits<unsigned>::max()) {
    if (Multiplier.isUnknownOrUndef())
      return UnknownVal();
    Count = Builder.evalBinOp(State, BO_Mul, Count, Multiplier,
                              Builder.getContext().getSizeType());
  }
  if (Contract.Scale == 1 || Count.isUnknownOrUndef())
    return Count;
  return Builder.evalBinOp(
      State, BO_Mul, Count,
      Builder.makeIntVal(Contract.Scale, Builder.getContext().getSizeType()),
      Builder.getContext().getSizeType());
}

struct FieldSpanContract {
  const MemberExpr *Pointer;
  const FieldDecl *Length;
  uint64_t Scale;
};

static const MemberExpr *pointerMember(const Expr *Expression) {
  if (!Expression)
    return nullptr;
  Expression = Expression->IgnoreParenCasts();
  if (const auto *Member = dyn_cast<MemberExpr>(Expression))
    return Member;
  if (const auto *Binary = dyn_cast<BinaryOperator>(Expression))
    if (Binary->getOpcode() == BO_Add || Binary->getOpcode() == BO_Sub)
      return pointerMember(Binary->getLHS());
  if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Expression))
    return pointerMember(Subscript->getBase());
  return nullptr;
}

/* Shared by fieldSpanContract (which resolves a POINTER FIELD from an
 * access expression and only ever needed its first matching contract) and
 * collectRecordSpanContracts below (which needs every contract on every
 * field of a record, because a single field can carry more than one
 * withtok(...) -- src/util/patch.c's `struct linebuf`'s `v` field carries
 * both readable_elements(n) and writable_elements(cap)). */
static bool parseFieldSpanAnnotation(StringRef Annotation,
                                     const RecordDecl *Record,
                                     ASTContext &Context, QualType Pointee,
                                     const FieldDecl *&Length,
                                     uint64_t &Scale) {
  if (!Annotation.consume_front("withtok:") || !Annotation.ends_with(")"))
    return false;
  size_t Open = Annotation.find('(');
  if (Open == StringRef::npos)
    return false;
  StringRef Family = Annotation.take_front(Open).trim();
  const TypedefNameDecl *Token = findTokenSort(Context, Family);
  bool ByteExtent = hasQualifier(Token, "qual:extent_at_least");
  bool ElementExtent = hasQualifier(Token, "qual:element_extent");
  if (!ByteExtent && !ElementExtent)
    return false;
  StringRef LengthName =
      Annotation.slice(Open + 1, Annotation.size() - 1).trim();
  Length = nullptr;
  for (const FieldDecl *Candidate : Record->fields())
    if (Candidate->getName() == LengthName) {
      Length = Candidate;
      break;
    }
  if (!Length)
    return false;
  Scale = 1;
  if (ElementExtent) {
    if (Pointee->isIncompleteType())
      return false;
    Scale = Context.getTypeSizeInChars(Pointee).getQuantity();
  }
  return true;
}

static std::optional<FieldSpanContract>
fieldSpanContract(const Expr *Expression, ASTContext &Context) {
  const MemberExpr *Member = pointerMember(Expression);
  const auto *Pointer =
      Member ? dyn_cast<FieldDecl>(Member->getMemberDecl()) : nullptr;
  if (!Pointer || !Pointer->getType()->isPointerType())
    return std::nullopt;
  const RecordDecl *Record = Pointer->getParent();
  for (const AnnotateAttr *Attribute : Pointer->specific_attrs<AnnotateAttr>()) {
    const FieldDecl *Length = nullptr;
    uint64_t Scale = 1;
    if (parseFieldSpanAnnotation(Attribute->getAnnotation(), Record, Context,
                                 Pointer->getType()->getPointeeType(), Length,
                                 Scale))
      return FieldSpanContract{Member, Length, Scale};
  }
  return std::nullopt;
}

struct RecordSpanContract {
  const FieldDecl *Pointer;
  const FieldDecl *Length;
  uint64_t Scale;
};

/* Every readable_elements/writable_elements-style contract on Record's
 * fields, keyed by raw FieldDecl rather than by an access expression --
 * unlike fieldSpanContract above, this supports check::Bind (which only
 * ever gives a written MemRegion, never a source Expr) and returns every
 * contract on a field, not just the first. */
static void collectRecordSpanContracts(
    const RecordDecl *Record, ASTContext &Context,
    SmallVectorImpl<RecordSpanContract> &Out) {
  if (!Record)
    return;
  for (const FieldDecl *Pointer : Record->fields()) {
    if (!Pointer->getType()->isPointerType())
      continue;
    for (const AnnotateAttr *Attribute :
        Pointer->specific_attrs<AnnotateAttr>()) {
      const FieldDecl *Length = nullptr;
      uint64_t Scale = 1;
      if (parseFieldSpanAnnotation(Attribute->getAnnotation(), Record,
                                   Context,
                                   Pointer->getType()->getPointeeType(),
                                   Length, Scale))
        Out.push_back({Pointer, Length, Scale});
    }
  }
}

static const MemRegion *carrierRegion(const Expr *Expression,
                                      CheckerContext &C) {
  if (!Expression)
    return nullptr;
  const Expr *Core = Expression->IgnoreParenImpCasts();
  if (const auto *Reference = dyn_cast<DeclRefExpr>(Core))
    if (const auto *Variable = dyn_cast<VarDecl>(Reference->getDecl()))
      return C.getState()
          ->getLValue(Variable, C.getLocationContext())
          .getAsRegion();
  if (const auto *Member = dyn_cast<MemberExpr>(Core)) {
    const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
    if (!Field)
      return nullptr;
    SVal Base = C.getSVal(Member->getBase());
    if (!Member->isArrow()) {
      const MemRegion *BaseRegion = carrierRegion(Member->getBase(), C);
      if (!BaseRegion)
        return nullptr;
      Base = loc::MemRegionVal(BaseRegion);
    }
    return C.getState()->getLValue(Field, Base).getAsRegion();
  }
  return nullptr;
}

static const MemberExpr *memberForField(const Expr *Expression,
                                        const FieldDecl *Field) {
  if (!Expression)
    return nullptr;
  Expression = Expression->IgnoreParenCasts();
  if (const auto *Member = dyn_cast<MemberExpr>(Expression))
    return Member->getMemberDecl() == Field ? Member : nullptr;
  if (const auto *Binary = dyn_cast<BinaryOperator>(Expression)) {
    if (const MemberExpr *Left = memberForField(Binary->getLHS(), Field))
      return Left;
    return memberForField(Binary->getRHS(), Field);
  }
  if (const auto *Unary = dyn_cast<UnaryOperator>(Expression))
    return memberForField(Unary->getSubExpr(), Field);
  return nullptr;
}

static std::optional<SVal>
fieldExtentFromLength(const Expr *Expression, const FieldDecl *Field,
                      SVal Value, SValBuilder &Builder) {
  if (!Expression)
    return std::nullopt;
  Expression = Expression->IgnoreParenCasts();
  if (const auto *Member = dyn_cast<MemberExpr>(Expression))
    return Member->getMemberDecl() == Field && !Value.isUnknownOrUndef()
               ? std::optional<SVal>(Value)
               : std::nullopt;
  const auto *Binary = dyn_cast<BinaryOperator>(Expression);
  if (!Binary)
    return std::nullopt;
  bool InLeft = memberForField(Binary->getLHS(), Field);
  bool InRight = memberForField(Binary->getRHS(), Field);
  if (InLeft == InRight)
    return std::nullopt;
  SymbolRef Symbol = Value.getAsSymbol();
  while (const auto *Cast = dyn_cast_or_null<SymbolCast>(Symbol))
    Symbol = Cast->getOperand();
  if (!Symbol)
    return std::nullopt;
  SymbolRef Operand;
  if (const auto *Expression = dyn_cast<SymIntExpr>(Symbol)) {
    if (!InLeft)
      return std::nullopt;
    Operand = Expression->getLHS();
  } else if (const auto *Expression = dyn_cast<IntSymExpr>(Symbol)) {
    if (!InRight)
      return std::nullopt;
    Operand = Expression->getRHS();
  } else if (const auto *Expression = dyn_cast<SymSymExpr>(Symbol)) {
    Operand = InLeft ? Expression->getLHS() : Expression->getRHS();
  } else {
    return std::nullopt;
  }
  return fieldExtentFromLength(InLeft ? Binary->getLHS() : Binary->getRHS(),
                               Field, Builder.makeSymbolVal(Operand), Builder);
}

static bool sameMemberBase(const MemberExpr *Left, const MemberExpr *Right,
                           CheckerContext &C) {
  if (Left->isArrow() != Right->isArrow())
    return false;
  if (Left->isArrow())
    return C.getSVal(Left->getBase()) == C.getSVal(Right->getBase());
  return carrierRegion(Left->getBase(), C) ==
         carrierRegion(Right->getBase(), C);
}

static ProgramStateRef assumeFieldSpan(const Expr *Expression,
                                       const Expr *LengthExpression,
                                       SVal PointerValue,
                                       SVal RequiredLength,
                                       ProgramStateRef State,
                                       CheckerContext &C) {
  std::optional<FieldSpanContract> Contract =
      fieldSpanContract(Expression, C.getASTContext());
  if (!Contract)
    return State;
  std::optional<DefinedOrUnknownSVal> Length;
  if (const MemberExpr *Mention =
          memberForField(LengthExpression, Contract->Length))
    if (sameMemberBase(Contract->Pointer, Mention, C))
      if (std::optional<SVal> Extent = fieldExtentFromLength(
              LengthExpression, Contract->Length, RequiredLength,
              C.getSValBuilder()))
        Length = Extent->getAs<DefinedOrUnknownSVal>();
  if (!Length) {
    SVal Base = C.getSVal(Contract->Pointer->getBase());
    if (!Contract->Pointer->isArrow()) {
      const MemRegion *BaseRegion =
          carrierRegion(Contract->Pointer->getBase(), C);
      if (!BaseRegion)
        return State;
      Base = loc::MemRegionVal(BaseRegion);
    }
    SVal LengthLocation = State->getLValue(Contract->Length, Base);
    std::optional<Loc> LengthLoc = LengthLocation.getAs<Loc>();
    if (!LengthLoc)
      return State;
    Length = State->getSVal(*LengthLoc).getAs<DefinedOrUnknownSVal>();
  }
  const MemRegion *Pointer = PointerValue.getAsRegion();
  while (const auto *Element = dyn_cast_or_null<ElementRegion>(Pointer))
    Pointer = Element->getSuperRegion();
  if (!Length || !Pointer)
    return State;
  SVal Extent = *Length;
  if (Contract->Scale != 1)
    Extent = C.getSValBuilder().evalBinOp(
        State, BO_Mul, Extent,
        C.getSValBuilder().makeIntVal(Contract->Scale,
                                      C.getASTContext().getSizeType()),
        C.getASTContext().getSizeType());
  std::optional<DefinedOrUnknownSVal> DefinedExtent =
      Extent.getAs<DefinedOrUnknownSVal>();
  return DefinedExtent
             ? State->set<AssumedSpanExtent>(Pointer, *DefinedExtent)
             : State;
}

#ifdef NTLIBC_MEMORY_CONTRACT_Z3
// A narrow bridge from the range constraint manager (and this checker's own
// ProvenLessEqual/ProvenLessThan relations, see checkBranchCondition above)
// to Z3.  It exists solely to discharge the no-wrap side condition that
// sameSymbolSpanProven's shared-affine-root lemma below otherwise proves
// with SValBuilder::getMaxValue(): that call only ever reports a single
// symbol's own range constraints, so it can never combine a relation
// between two *different* symbolic expressions -- exactly what
// ProvenLessEqual/ProvenLessThan exist to record, precisely because the
// range constraint manager cannot represent it -- with a concrete bound on
// the other side. Encoding both kinds of fact as Z3 bit-vector assertions
// and asking Z3 to refute the wrap event proves what the coarse per-symbol
// range check structurally cannot. A missing translation or an
// inconclusive solver result merely leaves the obligation exactly as
// unproven as it was before this fallback existed; it can never turn an
// UNSAT into a false proof.
class MemoryContractZ3Engine {
public:
  z3::context Context;
  z3::solver Solver;

  MemoryContractZ3Engine() : Solver(Context) {
    // Same budget as SizeCastChecker.cpp's ArithmeticZ3Engine: a generous
    // wall-clock stop only for pathological queries, not a knob ordinary
    // proofs are expected to depend on.
    z3::params Parameters(Context);
    Parameters.set("rlimit", 1000000u);
    Parameters.set("timeout", 2000u);
    Solver.set(Parameters);
  }
};

class MemoryContractZ3Proof {
  z3::context &ZCtx;
  z3::solver &Solver;
  ASTContext &AST;
  ntlibc::algebra::ScalarSMT Algebra;

  static bool isUnsigned(QualType Type) {
    return Type->isUnsignedIntegerOrEnumerationType();
  }

  ntlibc::algebra::CType cType(QualType Type) const {
    return {AST.getIntWidth(Type), AST.getIntWidth(Type), isUnsigned(Type)};
  }

  z3::expr bitVector(const llvm::APSInt &Value, unsigned Width) {
    llvm::APInt Bits = Value;
    if (Bits.getBitWidth() < Width)
      Bits = Value.isUnsigned() ? Bits.zext(Width) : Bits.sext(Width);
    else if (Bits.getBitWidth() > Width)
      Bits = Bits.trunc(Width);
    llvm::SmallString<80> Text;
    Bits.toString(Text, 10, false, false);
    return ZCtx.bv_val(Text.c_str(), Width);
  }

  // Deliberately narrower than a general expression translator: this proof
  // only ever needs to reconstruct the plain arithmetic building the root
  // symbol that decomposeAffine/symbolicConstantDifference already
  // isolated, never comparisons or casts. SymbolCast is rejected outright
  // for the same private-FromTy reason SizeCastChecker.cpp's own translator
  // rejects it: Clang 18 does not expose the cast's real source type, so
  // guessing its extension semantics could unsoundly manufacture a proof.
  std::optional<z3::expr> translate(SymbolRef Symbol, unsigned Depth = 0) {
    if (!Symbol || Depth > 24 || Symbol->getType().isNull() ||
        !Symbol->getType()->isIntegerType() || isa<SymbolCast>(Symbol))
      return std::nullopt;
    unsigned Width = AST.getIntWidth(Symbol->getType());
    if (const auto *Data = dyn_cast<SymbolData>(Symbol)) {
      std::string Name = "clang_sym_" + std::to_string(Data->getSymbolID());
      return ZCtx.bv_const(Name.c_str(), Width);
    }
    auto Arithmetic = [&](const z3::expr &Left, const z3::expr &Right,
                          BinaryOperator::Opcode Opcode,
                          QualType OperandType) -> std::optional<z3::expr> {
      if (Left.get_sort().bv_size() != Right.get_sort().bv_size() ||
          (Opcode != BO_Add && Opcode != BO_Sub && Opcode != BO_Mul))
        return std::nullopt;
      ntlibc::algebra::CType Type = cType(OperandType);
      std::optional<ntlibc::algebra::SemanticResult> L =
          Algebra.input(Left, Type);
      std::optional<ntlibc::algebra::SemanticResult> R =
          Algebra.input(Right, Type);
      if (!L || !R)
        return std::nullopt;
      std::optional<ntlibc::algebra::SemanticResult> Result =
          Opcode == BO_Add    ? Algebra.addConverted(*L, *R)
          : Opcode == BO_Sub  ? Algebra.subtractConverted(*L, *R)
                              : Algebra.multiplyConverted(*L, *R);
      return Result ? std::optional<z3::expr>(Result->Value) : std::nullopt;
    };
    if (const auto *Binary = dyn_cast<SymSymExpr>(Symbol)) {
      QualType LeftType = Binary->getLHS()->getType();
      QualType RightType = Binary->getRHS()->getType();
      if (LeftType.isNull() || RightType.isNull() ||
          AST.getIntWidth(LeftType) != Width ||
          AST.getIntWidth(RightType) != Width ||
          isUnsigned(LeftType) != isUnsigned(Symbol->getType()) ||
          isUnsigned(RightType) != isUnsigned(Symbol->getType()))
        return std::nullopt;
      std::optional<z3::expr> Left = translate(Binary->getLHS(), Depth + 1);
      std::optional<z3::expr> Right = translate(Binary->getRHS(), Depth + 1);
      if (!Left || !Right)
        return std::nullopt;
      return Arithmetic(*Left, *Right, Binary->getOpcode(), LeftType);
    }
    if (const auto *Binary = dyn_cast<SymIntExpr>(Symbol)) {
      QualType LeftType = Binary->getLHS()->getType();
      if (LeftType.isNull() || AST.getIntWidth(LeftType) != Width ||
          Binary->getRHS().getBitWidth() != Width ||
          Binary->getRHS().isUnsigned() != isUnsigned(LeftType))
        return std::nullopt;
      std::optional<z3::expr> Left = translate(Binary->getLHS(), Depth + 1);
      if (!Left)
        return std::nullopt;
      z3::expr Right = bitVector(Binary->getRHS(), Width);
      return Arithmetic(*Left, Right, Binary->getOpcode(), LeftType);
    }
    if (const auto *Binary = dyn_cast<IntSymExpr>(Symbol)) {
      QualType RightType = Binary->getRHS()->getType();
      if (RightType.isNull() || AST.getIntWidth(RightType) != Width ||
          Binary->getLHS().getBitWidth() != Width ||
          Binary->getLHS().isUnsigned() != isUnsigned(RightType))
        return std::nullopt;
      std::optional<z3::expr> Right = translate(Binary->getRHS(), Depth + 1);
      if (!Right)
        return std::nullopt;
      z3::expr Left = bitVector(Binary->getLHS(), Width);
      return Arithmetic(Left, *Right, Binary->getOpcode(), RightType);
    }
    return std::nullopt;
  }

  void addRange(const z3::expr &Expression, const RangeSet &Ranges) {
    if (!Expression.is_bv() || Ranges.isEmpty() ||
        Expression.get_sort().bv_size() != Ranges.getBitWidth())
      return;
    std::optional<z3::expr> Union;
    for (const Range &R : Ranges) {
      z3::expr From = bitVector(R.From(), Ranges.getBitWidth());
      z3::expr To = bitVector(R.To(), Ranges.getBitWidth());
      z3::expr Member = R.getConcreteValue()
                            ? Expression == From
                            : Ranges.isUnsigned()
                                  ? z3::ule(From, Expression) &&
                                        z3::ule(Expression, To)
                                  : From <= Expression && Expression <= To;
      Union = Union ? std::optional<z3::expr>(*Union || Member)
                    : std::optional<z3::expr>(Member);
    }
    if (Union && Union->is_bool())
      Solver.add(*Union);
  }

  void addRelation(SymbolRef LeftSymbol, SymbolRef RightSymbol, bool Strict) {
    if (!LeftSymbol || !RightSymbol || LeftSymbol->getType().isNull() ||
        RightSymbol->getType().isNull())
      return;
    std::optional<z3::expr> Left = translate(LeftSymbol);
    std::optional<z3::expr> Right = translate(RightSymbol);
    if (!Left || !Right)
      return;
    std::optional<ntlibc::algebra::SemanticResult> LeftInput =
        Algebra.input(*Left, cType(LeftSymbol->getType()));
    std::optional<ntlibc::algebra::SemanticResult> RightInput =
        Algebra.input(*Right, cType(RightSymbol->getType()));
    if (!LeftInput || !RightInput)
      return;
    // less() applies the real C usual-arithmetic-conversion rules via the
    // shared algebra before comparing, exactly as the source comparison
    // that established this relation did.
    std::optional<z3::expr> LessThanRight = Algebra.less(*LeftInput, *RightInput);
    if (!LessThanRight)
      return;
    z3::expr Fact = *LessThanRight;
    if (!Strict) {
      std::optional<z3::expr> RightBeforeLeft =
          Algebra.less(*RightInput, *LeftInput);
      if (!RightBeforeLeft)
        return;
      Fact = !*RightBeforeLeft;
    }
    if (Fact.is_bool())
      Solver.add(Fact);
  }

  // ProvenLessEqual/ProvenLessThan record relations between two distinct
  // symbolic expressions -- exactly what the range constraint manager (and
  // therefore getMaxValue) cannot represent. Asserting them into the same
  // solver as the concrete range facts above is what lets Z3 chain
  // "a <= b" with a concrete bound on b into a bound on a, closing cases
  // the single-symbol getMaxValue() check can never reach.
  void addRelations(ProgramStateRef State) {
    for (const SymbolRelation &Relation : State->get<ProvenLessEqual>())
      addRelation(Relation.first, Relation.second, /*Strict=*/false);
    for (const SymbolRelation &Relation : State->get<ProvenLessThan>())
      addRelation(Relation.first, Relation.second, /*Strict=*/true);
  }

public:
  MemoryContractZ3Proof(MemoryContractZ3Engine &Engine, ProgramStateRef State,
                        ASTContext &AST)
      : ZCtx(Engine.Context), Solver(Engine.Solver), AST(AST),
        Algebra(ZCtx, cType(AST.IntTy), cType(AST.UnsignedIntTy)) {
    Solver.reset();
    for (const auto &Entry : getConstraintMap(State))
      if (std::optional<z3::expr> Expression = translate(Entry.first))
        addRange(*Expression, Entry.second);
    addRelations(State);
  }

  // Proves that `Base + Offset` cannot overflow/wrap BaseType, given every
  // range and relational fact already known on this path. Only an UNSAT
  // wrap event discharges the obligation; SAT, timeout, and every
  // untranslatable shape leave it exactly as unproven as getMaxValue alone
  // left it.
  bool provesNoWrap(SymbolRef Base, QualType BaseType, uint64_t Offset) {
    if (!Base || BaseType.isNull() || !BaseType->isIntegerType())
      return false;
    std::optional<z3::expr> BaseExpr = translate(Base);
    if (!BaseExpr)
      return false;
    unsigned Width = AST.getIntWidth(BaseType);
    if (BaseExpr->get_sort().bv_size() != Width)
      return false;
    ntlibc::algebra::CType Domain = cType(BaseType);
    std::optional<ntlibc::algebra::SemanticResult> BaseInput =
        Algebra.input(*BaseExpr, Domain);
    if (!BaseInput)
      return false;
    llvm::APSInt OffsetValue(Width, Domain.Unsigned);
    OffsetValue = Offset;
    std::optional<ntlibc::algebra::SemanticResult> OffsetInput =
        Algebra.input(bitVector(OffsetValue, Width), Domain);
    if (!OffsetInput)
      return false;
    std::optional<ntlibc::algebra::SemanticResult> Result =
        Algebra.addConverted(*BaseInput, *OffsetInput);
    if (!Result)
      return false;
    z3::expr Violation = (Domain.Unsigned ? Result->Events.UnsignedWrap
                                          : Result->Events.SignedOverflow)
                             .simplify();
    if (!Violation.is_bool())
      return false;
    Solver.add(Violation);
    return ntlibc::algebra::provesUnsatisfiable(Solver);
  }
};

static MemoryContractZ3Engine &memoryContractZ3Engine() {
  // Static-analyzer callbacks are serial within one translation-unit
  // process; lint.sh provides process parallelism across translation
  // units. Reusing both context and solver avoids repeated Z3
  // initialization, matching SizeCastChecker.cpp's identical engine.
  static thread_local MemoryContractZ3Engine Engine;
  return Engine;
}

static bool noWrapProvenZ3(SymbolRef Base, QualType BaseType, uint64_t Offset,
                           ProgramStateRef State, ASTContext &AST) {
  MemoryContractZ3Proof Proof(memoryContractZ3Engine(), State, AST);
  return Proof.provesNoWrap(Base, BaseType, Offset);
}
#endif // NTLIBC_MEMORY_CONTRACT_Z3

class MemoryContractChecker
    : public Checker<check::PreCall, check::PostCall, check::BeginFunction,
                     check::EndFunction, check::Bind,
                     check::BranchCondition> {
  mutable std::unique_ptr<BugType> SpanBT;
  mutable std::unique_ptr<BugType> OverlapBT;
  mutable std::unique_ptr<BugType> TokenBT;
  mutable std::unique_ptr<BugType> RedundantBT;
  mutable std::unique_ptr<BugType> MovableBT;
  mutable std::unique_ptr<BugType> FieldSpanBT;

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
            findTokenSort(Function->getASTContext(), Family);
        if (!hasQualifier(Token, "qual:extent_at_least"))
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

  static bool declaredFreshAllocation(const FunctionDecl *Function) {
    if (!Function || !Function->getReturnType()->isPointerType())
      return false;
    for (const FunctionDecl *Redeclaration : Function->redecls())
      for (const AnnotateAttr *Attribute :
           Redeclaration->specific_attrs<AnnotateAttr>()) {
        StringRef Annotation = Attribute->getAnnotation();
        if (!Annotation.consume_front("withtok:"))
          continue;
        StringRef Family = Annotation.split('(').first.trim();
        const TypedefNameDecl *Token =
            findTokenSort(Function->getASTContext(), Family);
        if (hasQualifier(Token, "qual:dynamic_storage"))
          return true;
      }
    return false;
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

  static bool decomposeSignedAffine(SVal V, SymbolRef &Base,
                                    int64_t &Offset) {
    SymbolRef Original = V.getAsSymbol();
    SymbolRef Sym = stripCasts(Original);
    if (!Sym || Sym != Original)
      return false;
    if (const auto *IntExpr = dyn_cast<SymIntExpr>(Sym)) {
      if (IntExpr->getOpcode() != BO_Add &&
          IntExpr->getOpcode() != BO_Sub)
        return false;
      const llvm::APSInt &Constant = IntExpr->getRHS();
      if (Constant.isNegative() || Constant.getActiveBits() > 63)
        return false;
      Base = IntExpr->getLHS();
      if (isa<SymbolCast>(Base))
        return false;
      int64_t Magnitude = static_cast<int64_t>(Constant.getZExtValue());
      Offset = IntExpr->getOpcode() == BO_Add ? Magnitude : -Magnitude;
      return true;
    }
    Base = Sym;
    Offset = 0;
    return true;
  }

  struct LinearSymbolForm {
    std::map<SymbolRef, int64_t> Terms;
    __int128 Constant = 0;
  };

  static bool addLinearSymbol(SymbolRef Symbol, int64_t Sign,
                              LinearSymbolForm &Form) {
    Symbol = stripCasts(Symbol);
    if (!Symbol)
      return false;
    if (const auto *Expression = dyn_cast<SymSymExpr>(Symbol)) {
      if (Expression->getOpcode() != BO_Add &&
          Expression->getOpcode() != BO_Sub)
        goto atomic;
      return addLinearSymbol(Expression->getLHS(), Sign, Form) &&
             addLinearSymbol(Expression->getRHS(),
                             Expression->getOpcode() == BO_Add ? Sign : -Sign,
                             Form);
    }
    if (const auto *Expression = dyn_cast<SymIntExpr>(Symbol)) {
      if (Expression->getOpcode() != BO_Add &&
          Expression->getOpcode() != BO_Sub)
        goto atomic;
      const llvm::APSInt &Value = Expression->getRHS();
      if (!Value.isSignedIntN(64))
        return false;
      Form.Constant += static_cast<__int128>(Sign) * Value.getSExtValue() *
                       (Expression->getOpcode() == BO_Add ? 1 : -1);
      return addLinearSymbol(Expression->getLHS(), Sign, Form);
    }
    if (const auto *Expression = dyn_cast<IntSymExpr>(Symbol)) {
      if (Expression->getOpcode() != BO_Add &&
          Expression->getOpcode() != BO_Sub)
        goto atomic;
      const llvm::APSInt &Value = Expression->getLHS();
      if (!Value.isSignedIntN(64))
        return false;
      Form.Constant += static_cast<__int128>(Sign) * Value.getSExtValue();
      return addLinearSymbol(Expression->getRHS(),
                             Expression->getOpcode() == BO_Add ? Sign : -Sign,
                             Form);
    }
  atomic:
    Form.Terms[Symbol] += Sign;
    return true;
  }

  static std::optional<int64_t> symbolicConstantDifference(SVal Left,
                                                            SVal Right) {
    SymbolRef L = Left.getAsSymbol();
    SymbolRef R = Right.getAsSymbol();
    if (!L || !R)
      return std::nullopt;
    LinearSymbolForm Difference;
    if (!addLinearSymbol(L, 1, Difference) ||
        !addLinearSymbol(R, -1, Difference))
      return std::nullopt;
    for (const auto &[Symbol, Coefficient] : Difference.Terms)
      if (Coefficient != 0)
        return std::nullopt;
    if (Difference.Constant < std::numeric_limits<int64_t>::min() ||
        Difference.Constant > std::numeric_limits<int64_t>::max())
      return std::nullopt;
    return static_cast<int64_t>(Difference.Constant);
  }

  static bool symbolicallyEquivalent(SVal Left, SVal Right) {
    return symbolicConstantDifference(Left, Right) == 0;
  }

  // For a heap allocation whose dynamic extent was set from its own size
  // argument expression, prove a span in-bounds when the length argument
  // shares that same expression's root symbol: `d = __malloc(l + 1);
  // memcpy(d, s, l);` (extent = l+1, length = l) is strndup.c's shape,
  // but `n = strlen(s) + 1; p = __malloc(n); memcpy(p, s, n);` (extent =
  // length = the same compound expression, this tree's own xstrdup) is
  // at least as common, and a bare-symbol match doesn't recognize it.
  // Decomposing both sides into root+offset subsumes the bare-symbol
  // case (offset 0 on the length side) and covers equal-nonzero-offset
  // cases too, subject to the solver-backed no-wrap proof below.
  static bool sameSymbolSpanProven(SVal Extent, SVal Length,
                                   ProgramStateRef State,
                                   CheckerContext &C) {
    SymbolRef ExtentSymbol = Extent.getAsSymbol();
    SymbolRef LengthSymbol = Length.getAsSymbol();
    /* Identical symbolic values compare equal even if their common
     * expression wrapped before reaching this point. */
    if (ExtentSymbol && ExtentSymbol == LengthSymbol)
      return true;
    if (symbolicallyEquivalent(Extent, Length))
      return true;
    if (std::optional<int64_t> Difference =
            symbolicConstantDifference(Extent, Length)) {
      if (*Difference > 0 && LengthSymbol) {
        QualType LengthType = LengthSymbol->getType();
        if (LengthType->isIntegerType()) {
          llvm::APSInt Limit =
              C.getSValBuilder().getBasicValueFactory().getMaxValue(LengthType);
          llvm::APSInt Delta(
              llvm::APInt(Limit.getBitWidth(),
                          static_cast<uint64_t>(*Difference)),
              Limit.isUnsigned());
          Limit -= Delta;
          const llvm::APSInt *Maximum =
              C.getSValBuilder().getMaxValue(State, Length);
          if (Maximum && *Maximum <= Limit)
            return true;
#ifdef NTLIBC_MEMORY_CONTRACT_Z3
          // getMaxValue only sees LengthSymbol's own range constraints; it
          // cannot combine a proven relation to another symbol (see
          // MemoryContractZ3Proof's comment above) with that other
          // symbol's own range.  Fall back to a real Z3 proof before
          // giving up on this side condition.
          if (noWrapProvenZ3(LengthSymbol, LengthType,
                             static_cast<uint64_t>(*Difference), State,
                             C.getASTContext()))
            return true;
#endif
        }
      }
    }
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
    if (Maximum && *Maximum <= Limit)
      return true;
#ifdef NTLIBC_MEMORY_CONTRACT_Z3
    // Same fallback as above: getMaxValue(ExtentBase) alone cannot use a
    // proven relation to a different bounded symbol.
    if (noWrapProvenZ3(ExtentBase, BaseType,
                       static_cast<uint64_t>(ExtentOffset), State,
                       C.getASTContext()))
      return true;
#endif
    return false;
  }

  // The allocator-extent lemma above only closes the DESTINATION side of
  // a memcpy's span obligation: most memcpy calls here copy FROM a plain
  // `const char *` with no dynamic extent, so checkPreCall's check on
  // the source argument still fails even after the destination is
  // proven (measured: the tree-wide finding count didn't move after the
  // allocator-extent lemma alone). Almost all of those sources share the
  // "n = strlen(s) + 1; p = __malloc(n); memcpy(p, s, n);" shape (this
  // tree's own xstrdup, strdup.c, strndup.c) or the strnlen()-bounded
  // equivalent. strlen(s)'s byte-count contract makes the source safe by
  // construction: s has at least that many bytes plus the terminator.
  // strnlen(s, n) is looser -- only n (not n+1) bytes are known-safe if
  // it walked all n without finding a terminator -- so this credits it
  // with zero-byte, not one-byte, slack. See the StrlenSource/
  // StrnlenSource ProgramState maps and checkPostCall below.
  static bool stringLengthSourceSpanProven(SVal Pointer, SVal Length,
                                           ProgramStateRef State,
                                           CheckerContext &C) {
    const MemRegion *PointerRegion = Pointer.getAsRegion();
    if (!PointerRegion)
      return false;
    RegionOffset PointerOffset = PointerRegion->getAsOffset();
    if (!PointerOffset.isValid() || PointerOffset.hasSymbolicOffset() ||
        PointerOffset.getOffset() < 0 || PointerOffset.getOffset() % 8 != 0)
      return false;
    auto RelativePointerBytes = [&](const MemRegion *Source)
        -> std::optional<int64_t> {
      RegionOffset SourceOffset = Source->getAsOffset();
      if (!SourceOffset.isValid() || SourceOffset.hasSymbolicOffset() ||
          SourceOffset.getRegion() != PointerOffset.getRegion() ||
          SourceOffset.getOffset() % 8 != 0)
        return std::nullopt;
      int64_t Difference =
          (PointerOffset.getOffset() - SourceOffset.getOffset()) / 8;
      return Difference < 0 ? std::nullopt
                            : std::optional<int64_t>(Difference);
    };
    SymbolRef LengthSym;
    int64_t Slack;
    if (decomposeSignedAffine(Length, LengthSym, Slack)) {
      if (const MemRegion *const *Source = State->get<StrlenSource>(LengthSym))
        if (std::optional<int64_t> Bytes = RelativePointerBytes(*Source))
          if (!(Slack > 0 &&
                *Bytes > std::numeric_limits<int64_t>::max() - Slack) &&
              *Bytes + Slack <= 1)
            return true;
      if (const MemRegion *const *Source =
              State->get<StrnlenSource>(LengthSym))
        if (std::optional<int64_t> Bytes = RelativePointerBytes(*Source))
          if (!(Slack > 0 &&
                *Bytes > std::numeric_limits<int64_t>::max() - Slack) &&
              *Bytes + Slack <= 0)
            return true;
    }

    /* Once a guard proves B <= strlen(P), the unsigned subtraction cannot
     * underflow and strlen(P) - B is necessarily a readable prefix of P. */
    const auto *Difference =
        dyn_cast_or_null<SymSymExpr>(Length.getAsSymbol());
    if (Difference && Difference->getOpcode() == BO_Sub) {
      SymbolRef Measured = stripCasts(Difference->getLHS());
      SymbolRef Removed = stripCasts(Difference->getRHS());
      const MemRegion *const *Source = State->get<StrlenSource>(Measured);
      if (Source && RelativePointerBytes(*Source) == 0) {
        SVal NoUnderflow = C.getSValBuilder().evalBinOp(
            State, BO_GE, C.getSValBuilder().makeSymbolVal(Measured),
            C.getSValBuilder().makeSymbolVal(Removed),
            C.getSValBuilder().getConditionType());
        std::optional<DefinedOrUnknownSVal> Condition =
            NoUnderflow.getAs<DefinedOrUnknownSVal>();
        if (Condition && !State->assume(*Condition, false))
          return true;
      }
    }

    SymbolRef Bounded = stripCasts(Length.getAsSymbol());
    if (!Bounded)
      return false;
    auto RelationProvesStringBound = [&](SymbolRef Candidate) {
      for (const SymbolRelation &Relation : State->get<ProvenLessEqual>()) {
        if (stripCasts(Relation.first) != Candidate)
          continue;
        SymbolRef Bound = stripCasts(Relation.second);
        const MemRegion *const *Strlen = State->get<StrlenSource>(Bound);
        const MemRegion *const *Strnlen = State->get<StrnlenSource>(Bound);
        if ((Strlen || Strnlen) &&
            RelativePointerBytes(Strlen ? *Strlen : *Strnlen) == 0)
          return true;
      }
      return false;
    };
    if (RelationProvesStringBound(Bounded))
      return true;
    if (const auto *DifferenceLength = dyn_cast<SymIntExpr>(Bounded)) {
      if (DifferenceLength->getOpcode() != BO_Sub ||
          DifferenceLength->getRHS().isNegative())
        return false;
      SymbolRef Base = stripCasts(DifferenceLength->getLHS());
      SVal NoUnderflow = C.getSValBuilder().evalBinOp(
          State, BO_GE, C.getSValBuilder().makeSymbolVal(Base),
          C.getSValBuilder().makeIntVal(DifferenceLength->getRHS()),
          C.getSValBuilder().getConditionType());
      std::optional<DefinedOrUnknownSVal> Condition =
          NoUnderflow.getAs<DefinedOrUnknownSVal>();
      if (!Condition || State->assume(*Condition, false))
        return false;
      Bounded = Base;
    }
    return RelationProvesStringBound(Bounded);
  }

  static std::optional<SVal>
  callerArgumentValue(unsigned Argument, ProgramStateRef State,
                      const LocationContext *Context) {
    const auto *Frame = dyn_cast_or_null<StackFrameContext>(Context);
    if (!Frame || Frame->inTopFrame())
      return std::nullopt;
    const auto *Call = dyn_cast_or_null<CallExpr>(Frame->getCallSite());
    if (!Call || Argument >= Call->getNumArgs())
      return std::nullopt;
    SVal Value = State->getSVal(Call->getArg(Argument), Frame->getParent());
    return Value.isUnknownOrUndef() ? std::nullopt
                                    : std::optional<SVal>(Value);
  }

public:
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const {
    const auto *Comparison = dyn_cast<BinaryOperator>(Condition);
    if (!Comparison || !Comparison->isComparisonOp())
      return;
    ProgramStateRef State = C.getState();
    bool IsTrue =
        State->isNonNull(C.getSVal(Condition)).isConstrainedTrue();
    bool IsFalse = State->isNull(C.getSVal(Condition)).isConstrainedTrue();
    if (!IsTrue && !IsFalse)
      return;
    SymbolRef Left = C.getSVal(Comparison->getLHS()).getAsSymbol();
    SymbolRef Right = C.getSVal(Comparison->getRHS()).getAsSymbol();
    if (!Left || !Right)
      return;
    bool LeftLessEqual =
        (IsTrue && (Comparison->getOpcode() == BO_LE ||
                    Comparison->getOpcode() == BO_LT)) ||
        (IsFalse && (Comparison->getOpcode() == BO_GT ||
                     Comparison->getOpcode() == BO_GE));
    bool RightLessEqual =
        (IsTrue && (Comparison->getOpcode() == BO_GE ||
                    Comparison->getOpcode() == BO_GT)) ||
        (IsFalse && (Comparison->getOpcode() == BO_LT ||
                     Comparison->getOpcode() == BO_LE));
    if (LeftLessEqual)
      State = State->add<ProvenLessEqual>({Left, Right});
    if (RightLessEqual)
      State = State->add<ProvenLessEqual>({Right, Left});
    bool LeftLessThan =
        (IsTrue && Comparison->getOpcode() == BO_LT) ||
        (IsFalse && Comparison->getOpcode() == BO_GE);
    bool RightLessThan =
        (IsTrue && Comparison->getOpcode() == BO_GT) ||
        (IsFalse && Comparison->getOpcode() == BO_LE);
    if (LeftLessThan)
      State = State->add<ProvenLessThan>({Left, Right});
    if (RightLessThan)
      State = State->add<ProvenLessThan>({Right, Left});
    if (State != C.getState())
      C.addTransition(State);
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
      else if (Reason == "manual memory proof axiom can be narrowed")
        Title = "Overbroad memory proof axiom";
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

  void reportFieldSpan(StringRef Reason, const Stmt *Statement,
                       ProgramStateRef State, CheckerContext &C) const {
    if (!Statement)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!FieldSpanBT)
      FieldSpanBT = std::make_unique<BugType>(
          this, "Unproven paired field span", categories::MemoryError);
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        (Reason + "; origin '" +
         SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
         "'; context '" + context(C) + "'; expression '" + text(Statement, C) +
         "'; site '" + site(Statement, C) + "'")
            .str();
    auto Report =
        std::make_unique<PathSensitiveBugReport>(*FieldSpanBT, Message, Node);
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
      SymbolRef ExtentSymbol = stripCasts(Extent.getAsSymbol());
      SymbolRef LengthSymbol = stripCasts(Length.getAsSymbol());
      if (ExtentSymbol && LengthSymbol) {
        if (State->contains<ProvenLessEqual>({LengthSymbol, ExtentSymbol}))
          return true;
        /* A strict upper bound is stronger than the non-strict bound a
         * span requires.  BranchCondition records `length < remaining`
         * separately (notably from the false edge of
         * `if (length >= remaining) return`), so retain that proof here
         * instead of requiring source code to spell the equivalent `>`
         * guard merely for the analyzer. */
        if (State->contains<ProvenLessThan>({LengthSymbol, ExtentSymbol}))
          return true;
        if (const auto *OffsetLength = dyn_cast<SymIntExpr>(LengthSymbol))
          if (OffsetLength->getOpcode() == BO_Add &&
              !OffsetLength->getRHS().isNegative() &&
              OffsetLength->getRHS().getLimitedValue() == 1 &&
              State->contains<ProvenLessThan>(
                  {stripCasts(OffsetLength->getLHS()), ExtentSymbol}))
            return true;
        if (const auto *Shorter = dyn_cast<SymIntExpr>(LengthSymbol))
          if (Shorter->getOpcode() == BO_Sub &&
              !Shorter->getRHS().isNegative() &&
              stripCasts(Shorter->getLHS()) == ExtentSymbol) {
            SVal NoUnderflow = C.getSValBuilder().evalBinOp(
                State, BO_GE,
                C.getSValBuilder().makeSymbolVal(ExtentSymbol),
                C.getSValBuilder().makeIntVal(Shorter->getRHS()),
                C.getSValBuilder().getConditionType());
            std::optional<DefinedOrUnknownSVal> Condition =
                NoUnderflow.getAs<DefinedOrUnknownSVal>();
            if (Condition && !State->assume(*Condition, false))
              return true;
          }
      }
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
    if (UseAssumedSpans)
      if (const auto *Element =
              dyn_cast_or_null<ElementRegion>(Pointer.getAsRegion()))
        {
          const DefinedOrUnknownSVal *BaseExtent =
              State->get<AssumedSpanExtent>(Element->getSuperRegion());
          if (!BaseExtent)
            BaseExtent = State->get<AllocatedSpanExtent>(
                Element->getSuperRegion()->getBaseRegion());
          if (BaseExtent) {
          SVal ElementBytes = getElementExtent(Element->getElementType(),
                                               C.getSValBuilder());
          SVal Offset = Element->getIndex();
          const llvm::APSInt *KnownElementBytes =
              C.getSValBuilder().getKnownValue(State, ElementBytes);
          if (!KnownElementBytes || KnownElementBytes->getZExtValue() != 1)
            Offset = C.getSValBuilder().evalBinOp(
                State, BO_Mul, Offset, ElementBytes,
                C.getASTContext().getSizeType());
          SVal OffsetFits = C.getSValBuilder().evalBinOp(
              State, BO_GE, *BaseExtent, Offset,
              C.getSValBuilder().getConditionType());
          std::optional<DefinedOrUnknownSVal> Fits =
              OffsetFits.getAs<DefinedOrUnknownSVal>();
          if (Fits && !State->assume(*Fits, false)) {
            SVal Remaining = C.getSValBuilder().evalBinOp(
                State, BO_Sub, *BaseExtent, Offset,
                C.getASTContext().getSizeType());
            if (ExtentProvesLength(Remaining))
              return true;
          }
        }
        }
    SVal Extent = getDynamicExtentWithOffset(State, Pointer);
    if (Extent.isUnknownOrUndef() || Length.isUnknownOrUndef())
      return false;
    if (sameSymbolSpanProven(Extent, Length, State, C))
      return true;
    if (stringLengthSourceSpanProven(Pointer, Length, State, C))
      return true;
    return ExtentProvesLength(Extent);
  }

  bool typedObjectSpanProven(const Expr *PointerExpression,
                             const Expr *LengthExpression, SVal Length,
                             ProgramStateRef State, CheckerContext &C,
                             bool RequireConstantLength = false) const {
    if (!PointerExpression || Length.isUnknownOrUndef())
      return false;
    const Expr *Object = PointerExpression->IgnoreParenCasts();
    QualType ObjectType = Object->getType();
    QualType ExtentType;
    if (const auto *Address = dyn_cast<UnaryOperator>(Object);
        Address && Address->getOpcode() == UO_AddrOf) {
      Object = Address->getSubExpr()->IgnoreParenImpCasts();
      ExtentType = Object->getType();
    } else if (ObjectType->isArrayType()) {
      ExtentType = ObjectType;
    } else {
      if (!isa<DeclRefExpr>(Object) && !isa<MemberExpr>(Object))
        return false;
      if (!ObjectType->isPointerType())
        return false;
      ExtentType = ObjectType->getPointeeType();
    }
    if (ExtentType.isNull() || ExtentType->isVoidType() ||
        ExtentType->isFunctionType() || ExtentType->isIncompleteType() ||
        ExtentType->isVariableArrayType())
      return false;
    CharUnits Bytes = C.getASTContext().getTypeSizeInChars(ExtentType);
    if (RequireConstantLength) {
      Expr::EvalResult Result;
      if (!LengthExpression ||
          !LengthExpression->EvaluateAsInt(Result, C.getASTContext()))
        return false;
      const llvm::APSInt &Constant = Result.Val.getInt();
      return !Constant.isNegative() && Constant.getActiveBits() <= 64 &&
             Constant.getZExtValue() <=
                 static_cast<uint64_t>(Bytes.getQuantity());
    }
    SVal Extent = C.getSValBuilder().makeIntVal(
        static_cast<uint64_t>(Bytes.getQuantity()),
        C.getASTContext().getSizeType());
    SVal Enough = C.getSValBuilder().evalBinOp(
        State, BO_GE, Extent, Length,
        C.getSValBuilder().getConditionType());
    std::optional<DefinedOrUnknownSVal> Condition =
        Enough.getAs<DefinedOrUnknownSVal>();
    return Condition && !State->assume(*Condition, false);
  }

  bool typedDisjointSpanProven(const Expr *FirstExpression,
                               const Expr *SecondExpression,
                               const Expr *LengthExpression,
                               CheckerContext &C) const {
    if (!FirstExpression || !SecondExpression || !LengthExpression)
      return false;
    const auto *First = dyn_cast<DeclRefExpr>(
        FirstExpression->IgnoreParenCasts());
    const auto *Second = dyn_cast<DeclRefExpr>(
        SecondExpression->IgnoreParenCasts());
    if (!First || !Second || First->getDecl() == Second->getDecl() ||
        !First->getType()->isArrayType() ||
        !Second->getType()->isArrayType() ||
        First->getType()->isVariableArrayType() ||
        Second->getType()->isVariableArrayType())
      return false;
    Expr::EvalResult Result;
    if (!LengthExpression->EvaluateAsInt(Result, C.getASTContext()))
      return false;
    const llvm::APSInt &Length = Result.Val.getInt();
    if (Length.isNegative() || Length.getActiveBits() > 64)
      return false;
    uint64_t Bytes = Length.getZExtValue();
    return Bytes <= static_cast<uint64_t>(
                        C.getASTContext().getTypeSizeInChars(First->getType())
                            .getQuantity()) &&
           Bytes <= static_cast<uint64_t>(
                        C.getASTContext().getTypeSizeInChars(Second->getType())
                            .getQuantity());
  }

  static const ValueDecl *rootRestrictedPointer(const Expr *Expression) {
    if (!Expression)
      return nullptr;
    Expression = Expression->IgnoreParenCasts();
    if (const auto *Reference = dyn_cast<DeclRefExpr>(Expression))
      if (const auto *Value = dyn_cast<ValueDecl>(Reference->getDecl()))
        return Value;
    if (const auto *Binary = dyn_cast<BinaryOperator>(Expression))
      if (Binary->getOpcode() == BO_Add || Binary->getOpcode() == BO_Sub)
        return rootRestrictedPointer(Binary->getLHS());
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Expression))
      return rootRestrictedPointer(Subscript->getBase());
    if (const auto *Member = dyn_cast<MemberExpr>(Expression))
      return rootRestrictedPointer(Member->getBase());
    if (const auto *Address = dyn_cast<UnaryOperator>(Expression))
      if (Address->getOpcode() == UO_AddrOf)
        return rootRestrictedPointer(Address->getSubExpr());
    return nullptr;
  }

  static bool restrictDisjointSpanProven(const Expr *FirstExpression,
                                         const Expr *SecondExpression,
                                         SVal First, SVal Second) {
    const ValueDecl *A = rootRestrictedPointer(FirstExpression);
    const ValueDecl *B = rootRestrictedPointer(SecondExpression);
    if ((!A || !A->getType().isRestrictQualified()) &&
        (!B || !B->getType().isRestrictQualified()))
      return false;
    const MemRegion *FirstRegion = First.getAsRegion();
    const MemRegion *SecondRegion = Second.getAsRegion();
    if (!FirstRegion || !SecondRegion)
      return false;
    RegionOffset FirstOffset = FirstRegion->getAsOffset();
    RegionOffset SecondOffset = SecondRegion->getAsOffset();
    return FirstOffset.isValid() && SecondOffset.isValid() &&
           FirstOffset.getRegion() != SecondOffset.getRegion();
  }

  bool derivedContractSpanProven(SVal Pointer, SVal Length,
                                 ProgramStateRef State,
                                 CheckerContext &C) const {
    const auto *Element =
        dyn_cast_or_null<ElementRegion>(Pointer.getAsRegion());
    if (!Element)
      return false;
    const auto *CurrentFunction = dyn_cast_or_null<FunctionDecl>(
        C.getLocationContext()->getDecl());
    if (!CurrentFunction)
      return false;
    SmallVector<SpanContract, 2> Spans;
    SmallVector<DisjointContract, 1> Disjoint;
    tokenContracts(CurrentFunction, Spans, Disjoint);
    std::optional<DefinedOrUnknownSVal> Extent;
    if (const DefinedOrUnknownSVal *Assumed =
            State->get<AssumedSpanExtent>(Element->getSuperRegion()))
      Extent = *Assumed;
    for (const SpanContract &Contract : Spans) {
      if (Extent)
        break;
      if (Contract.Operation != MemoryTokenOperation::Require)
        continue;
      const ParmVarDecl *PointerParameter =
          CurrentFunction->getParamDecl(Contract.Pointer);
      const MemRegion *Base = State->getSVal(
          State->getLValue(PointerParameter, C.getLocationContext()))
                                  .getAsRegion();
      if (!Base || Element->getSuperRegion()->getBaseRegion() !=
                       Base->getBaseRegion())
        continue;
      const ParmVarDecl *LengthParameter =
          CurrentFunction->getParamDecl(Contract.Length);
      SVal LengthValue = callerArgumentValue(
          Contract.Length, State, C.getLocationContext()).value_or(
          State->getSVal(
              State->getLValue(LengthParameter, C.getLocationContext())));
      SVal Multiplier = UnknownVal();
      if (Contract.Multiplier != std::numeric_limits<unsigned>::max()) {
        const ParmVarDecl *Parameter =
            CurrentFunction->getParamDecl(Contract.Multiplier);
        Multiplier = callerArgumentValue(
            Contract.Multiplier, State, C.getLocationContext()).value_or(
            State->getSVal(
                State->getLValue(Parameter, C.getLocationContext())));
      }
      LengthValue = scaledSpanLength(Contract, LengthValue, Multiplier, State,
                                     C.getSValBuilder());
      Extent = LengthValue.getAs<DefinedOrUnknownSVal>();
      break;
    }
    if (!Extent || Length.isUnknownOrUndef())
      return false;
    SVal Offset = Element->getIndex();
    SVal ElementBytes =
        getElementExtent(Element->getElementType(), C.getSValBuilder());
    const llvm::APSInt *KnownElementBytes =
        C.getSValBuilder().getKnownValue(State, ElementBytes);
    if (!KnownElementBytes || KnownElementBytes->getZExtValue() != 1)
      Offset = C.getSValBuilder().evalBinOp(
          State, BO_Mul, Offset, ElementBytes,
          C.getASTContext().getSizeType());
    SVal Remaining = C.getSValBuilder().evalBinOp(
        State, BO_Sub, *Extent, Offset, C.getASTContext().getSizeType());
    for (const SymbolRelation &Relation : State->get<ProvenLessEqual>()) {
      SVal Smaller = C.getSValBuilder().makeSymbolVal(Relation.first);
      SVal Larger = C.getSValBuilder().makeSymbolVal(Relation.second);
      if (symbolicallyEquivalent(Length, Smaller) &&
          symbolicallyEquivalent(Remaining, Larger))
        return true;
    }
    SVal Enough = C.getSValBuilder().evalBinOp(
        State, BO_GE, Remaining, Length,
        C.getSValBuilder().getConditionType());
    std::optional<DefinedOrUnknownSVal> Condition =
        Enough.getAs<DefinedOrUnknownSVal>();
    if (Condition && !State->assume(*Condition, false))
      return true;
    SymbolRef RemainingSymbol = Remaining.getAsSymbol();
    SymbolRef LengthSymbol = Length.getAsSymbol();
    if (!RemainingSymbol || !LengthSymbol)
      return false;
    if (State->contains<ProvenLessEqual>({LengthSymbol, RemainingSymbol}))
      return true;
    if (State->contains<ProvenLessThan>({LengthSymbol, RemainingSymbol}))
      return true;
    SymbolRef DynamicRemaining =
        getDynamicExtentWithOffset(State, Pointer).getAsSymbol();
    if (DynamicRemaining) {
      const auto *DynamicDifference = dyn_cast<IntSymExpr>(DynamicRemaining);
      for (const SymbolRelation &Relation : State->get<ProvenLessEqual>()) {
        const auto *BoundDifference = dyn_cast<IntSymExpr>(Relation.second);
        if (Relation.first == LengthSymbol && DynamicDifference &&
            BoundDifference && DynamicDifference->getOpcode() == BO_Sub &&
            BoundDifference->getOpcode() == BO_Sub &&
            DynamicDifference->getRHS() == BoundDifference->getRHS() &&
            llvm::APSInt::compareValues(DynamicDifference->getLHS(),
                                        BoundDifference->getLHS()) == 0)
          return true;
      }
    }
    SymbolRef ExtentSymbol = Extent->getAsSymbol();
    SymbolRef OffsetSymbol = Offset.getAsSymbol();
    if (!OffsetSymbol)
      return false;
    for (const SymbolRelation &Relation : State->get<ProvenLessEqual>()) {
      if (Relation.first != LengthSymbol)
        continue;
      const auto *Difference = dyn_cast<SymSymExpr>(Relation.second);
      if (ExtentSymbol && Difference && Difference->getOpcode() == BO_Sub &&
          Difference->getRHS() == OffsetSymbol) {
        SymbolRef Minuend = Difference->getLHS();
        if (Minuend == ExtentSymbol)
          return true;
      }
      const auto *ConstantDifference = dyn_cast<IntSymExpr>(Relation.second);
      const llvm::APSInt *KnownExtent =
          C.getSValBuilder().getKnownValue(State, *Extent);
      if (!KnownExtent || !ConstantDifference ||
          ConstantDifference->getOpcode() != BO_Sub ||
          ConstantDifference->getRHS() != OffsetSymbol ||
          llvm::APSInt::compareValues(ConstantDifference->getLHS(),
                                      *KnownExtent) > 0)
        continue;
      SVal OffsetWithinBound = C.getSValBuilder().evalBinOp(
          State, BO_LE, Offset,
          C.getSValBuilder().makeIntVal(ConstantDifference->getLHS()),
          C.getSValBuilder().getConditionType());
      std::optional<DefinedOrUnknownSVal> Within =
          OffsetWithinBound.getAs<DefinedOrUnknownSVal>();
      if (Within && !State->assume(*Within, false))
        return true;
    }
    return false;
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
      auto IsFreshAllocation = [&](const MemRegion *Region) {
        const MemRegion *Base = Region->getBaseRegion();
        for (const MemRegion *Allocated :
             State->get<AllocatedBaseRegion>())
          if (Allocated == Region || Allocated == Base ||
              Allocated->getBaseRegion() == Base)
            return true;
        return false;
      };
      if (IsFreshAllocation(AO.getRegion()) ||
          IsFreshAllocation(BO.getRegion()))
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

  /* Array-to-pointer decay represents the array's base as an ElementRegion
   * at index zero.  Store a span granted to that exact base on the array
   * region itself, so later bounded suffixes (`base + offset`) can consume
   * the remaining extent.  An interior-pointer grant deliberately stays on
   * its ElementRegion: promoting that would incorrectly prove bytes before
   * the pointer. */
  static const MemRegion *spanProofRegion(const MemRegion *Region,
                                          ProgramStateRef State) {
    const auto *Element = dyn_cast_or_null<ElementRegion>(Region);
    if (Element && State->isNull(Element->getIndex()).isConstrainedTrue())
      return Element->getSuperRegion();
    return Region;
  }

  static ProgramStateRef applyGrants(
      ProgramStateRef State, const CallEvent &Call,
      ArrayRef<SpanContract> Spans, ArrayRef<DisjointContract> Disjoint,
      SValBuilder &Builder) {
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Grant ||
          Contract.Pointer >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs() ||
          (Contract.Multiplier != std::numeric_limits<unsigned>::max() &&
           Contract.Multiplier >= Call.getNumArgs()))
        continue;
      const MemRegion *Region =
          Call.getArgSVal(Contract.Pointer).getAsRegion();
      SVal Multiplier =
          Contract.Multiplier == std::numeric_limits<unsigned>::max()
              ? UnknownVal()
              : Call.getArgSVal(Contract.Multiplier);
      SVal Length = scaledSpanLength(Contract,
                                     Call.getArgSVal(Contract.Length),
                                     Multiplier, State, Builder);
      std::optional<DefinedOrUnknownSVal> Extent =
          Length.getAs<DefinedOrUnknownSVal>();
      Region = spanProofRegion(Region, State);
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

  // Called from checkEndFunction, the one point (see the TouchedRecordSpan
  // comment above for why CompoundStmt-exit does not work) every touched
  // pair's update is guaranteed to have fully settled. Consumes the
  // (Pointer, Length) value snapshot checkBind already captured for each
  // touched pair -- not a fresh store re-read, which dead-binding cleanup
  // may have already discarded by this point (see TouchedRecordSpan's
  // comment) -- so a pointer-then-length or length-then-pointer update is
  // judged only once both writes have actually landed, using whichever
  // value each field was last known to hold.
  ProgramStateRef
  flushRecordSpanObligations(ProgramStateRef State, CheckerContext &C) const {
    for (const auto &Entry : State->get<TouchedRecordSpan>()) {
      const FieldDecl *Pointer = Entry.first.first.second;
      const FieldDecl *Length = Entry.first.second;
      SVal PointerValue = Entry.second.first.first;
      SVal LengthValue = Entry.second.first.second;
      const Stmt *Site = Entry.second.second;
      State = State->remove<TouchedRecordSpan>(Entry.first);
      if (PointerValue.isUnknownOrUndef() || LengthValue.isUnknownOrUndef())
        continue;
      SmallVector<RecordSpanContract, 2> Contracts;
      collectRecordSpanContracts(Pointer->getParent(), C.getASTContext(),
                                 Contracts);
      uint64_t Scale = 1;
      bool Found = false;
      for (const RecordSpanContract &Contract : Contracts)
        if (Contract.Pointer == Pointer && Contract.Length == Length) {
          Scale = Contract.Scale;
          Found = true;
          break;
        }
      if (!Found)
        continue;
      if (Scale != 1)
        LengthValue = C.getSValBuilder().evalBinOp(
            State, BO_Mul, LengthValue,
            C.getSValBuilder().makeIntVal(Scale,
                                          C.getASTContext().getSizeType()),
            C.getASTContext().getSizeType());
      if (!spanProven(PointerValue, LengthValue, State, C,
                      /*UseAssumedSpans=*/false))
        reportFieldSpan(
            "paired length field is not proven within its pointer field's "
            "real allocation extent",
            Site, State, C);
    }
    return State;
  }

  // checkBind fires once per write, BEFORE the engine applies it -- so
  // C.getState() here still reflects every EARLIER write but not this one.
  // For the field actually being written, its new value is exactly Value
  // (the Bind callback's own parameter); for the field's PAIRED partner,
  // reuse whatever value the last touch of this same key already
  // captured (Prior), and only fall back to reading the store when this
  // is the pair's first touch -- the same read-back this file's other
  // mechanisms already rely on, and still safe here because nothing has
  // had a chance to prune it between then and now.
  static ProgramStateRef recordFieldSpanTouch(
      ProgramStateRef State, const RecordSpanTouchKey &Key,
      bool WrittenIsPointer, SVal Value, const Stmt *BindStmt,
      const MemRegion *Base, const FieldDecl *OtherField,
      CheckerContext &C) {
    DefinedOrUnknownSVal NewValue =
        Value.getAs<DefinedOrUnknownSVal>().value_or(UnknownVal());
    DefinedOrUnknownSVal OtherValue = UnknownVal();
    if (const RecordSpanSnapshot *Prior = State->get<TouchedRecordSpan>(Key)) {
      OtherValue = WrittenIsPointer ? Prior->first.second : Prior->first.first;
    } else if (std::optional<Loc> OtherLoc =
                   State->getLValue(OtherField, loc::MemRegionVal(Base))
                       .getAs<Loc>()) {
      OtherValue =
          State->getSVal(*OtherLoc).getAs<DefinedOrUnknownSVal>().value_or(
              UnknownVal());
    }
    DefinedOrUnknownSVal PointerValue = WrittenIsPointer ? NewValue : OtherValue;
    DefinedOrUnknownSVal LengthValue = WrittenIsPointer ? OtherValue : NewValue;
    return State->set<TouchedRecordSpan>(
        Key, {{PointerValue, LengthValue}, BindStmt});
  }

public:
  void checkBind(SVal Location, SVal Value, const Stmt *BindStmt,
                CheckerContext &C) const {
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
    if (const auto *Field = dyn_cast<FieldRegion>(Bound)) {
      SmallVector<RecordSpanContract, 2> Contracts;
      collectRecordSpanContracts(Field->getDecl()->getParent(),
                                 C.getASTContext(), Contracts);
      const MemRegion *Base = Field->getSuperRegion();
      for (const RecordSpanContract &Contract : Contracts) {
        bool WrittenIsPointer = Contract.Pointer == Field->getDecl();
        bool WrittenIsLength = Contract.Length == Field->getDecl();
        if (!WrittenIsPointer && !WrittenIsLength)
          continue;
        RecordSpanTouchKey Key{{Base, Contract.Pointer}, Contract.Length};
        const FieldDecl *OtherField =
            WrittenIsPointer ? Contract.Length : Contract.Pointer;
        State = recordFieldSpanTouch(State, Key, WrittenIsPointer, Value,
                                     BindStmt, Base, OtherField, C);
      }
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
      if (std::optional<SVal> CallerLength = callerArgumentValue(
              Contract.Length, State, C.getLocationContext()))
        LengthValue = *CallerLength;
      SVal Multiplier = UnknownVal();
      if (Contract.Multiplier != std::numeric_limits<unsigned>::max()) {
        const ParmVarDecl *Parameter =
            Function->getParamDecl(Contract.Multiplier);
        Multiplier = State->getSVal(
            State->getLValue(Parameter, C.getLocationContext()));
        if (std::optional<SVal> Caller = callerArgumentValue(
                Contract.Multiplier, State, C.getLocationContext()))
          Multiplier = *Caller;
      }
      LengthValue = scaledSpanLength(Contract, LengthValue, Multiplier, State,
                                     C.getSValBuilder());
      const MemRegion *Region = PointerValue.getAsRegion();
      std::optional<DefinedOrUnknownSVal> DefinedLength =
          LengthValue.getAs<DefinedOrUnknownSVal>();
      Region = spanProofRegion(Region, State);
      if (Region && DefinedLength) {
        State = State->set<AssumedSpanExtent>(Region, *DefinedLength);
        /* A contract on a base pointer is also a conservative dynamic extent
         * for that function-local symbolic region.  Do not overwrite an
         * allocation's base extent when the contracted parameter itself is
         * already interior. */
        if (Region == Region->getBaseRegion())
          State = setDynamicExtent(State, Region, *DefinedLength,
                                   C.getSValBuilder());
      }
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
    const auto *Frame =
        dyn_cast_or_null<StackFrameContext>(C.getLocationContext());
    /* A proof axiom that is necessary in its defining function can become
     * redundant after that function is inlined into a stronger caller.  Such
     * a caller-specific fact cannot be used to narrow the source-level axiom;
     * diagnose migration scaffolding only in the function's own entry frame. */
    if (isManualProofCall(Function) && Frame && Frame->inTopFrame()) {
      bool Reported = false;
      for (const SpanContract &Contract : Spans) {
        if (Contract.Operation != MemoryTokenOperation::Grant ||
            Contract.Pointer >= Call.getNumArgs() ||
            Contract.Length >= Call.getNumArgs())
          continue;
        bool Redundant = typedObjectSpanProven(
            Call.getArgExpr(Contract.Pointer),
            Call.getArgExpr(Contract.Length),
            Call.getArgSVal(Contract.Length), C.getState(), C, true);
        bool ProvenOnPath = Redundant ||
            spanProven(Call.getArgSVal(Contract.Pointer),
                       Call.getArgSVal(Contract.Length), C.getState(), C,
                       false) ||
            typedObjectSpanProven(Call.getArgExpr(Contract.Pointer),
                                  Call.getArgExpr(Contract.Length),
                                  Call.getArgSVal(Contract.Length),
                                  C.getState(), C);
        if (ProvenOnPath) {
          BugType *Type = Redundant ? RedundantBT.get() : MovableBT.get();
          report(Redundant ? "manual memory proof axiom is redundant"
                           : "manual memory proof axiom can be narrowed",
                 Type, Call, C.getState(), C);
          if (Redundant && !RedundantBT && Type)
            RedundantBT.reset(Type);
          if (!Redundant && !MovableBT && Type)
            MovableBT.reset(Type);
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
          bool Redundant = typedDisjointSpanProven(
              Call.getArgExpr(Contract.First),
              Call.getArgExpr(Contract.Second),
              Call.getArgExpr(Contract.Length), C);
          bool ProvenOnPath = Redundant || overlapProven(
              Call.getArgSVal(Contract.First),
              Call.getArgSVal(Contract.Second),
              Call.getArgSVal(Contract.Length), C.getState(), C, false);
          if (ProvenOnPath) {
            BugType *Type = Redundant ? RedundantBT.get() : MovableBT.get();
            report(Redundant ? "manual memory proof axiom is redundant"
                             : "manual memory proof axiom can be narrowed",
                   Type, Call, C.getState(), C);
            if (Redundant && !RedundantBT && Type)
              RedundantBT.reset(Type);
            if (!Redundant && !MovableBT && Type)
              MovableBT.reset(Type);
            break;
          }
        }
      }
    }
    for (const SpanContract &Contract : Spans) {
      if (Contract.Operation != MemoryTokenOperation::Require)
        continue;
      if (Contract.Pointer >= Call.getNumArgs() ||
          Contract.Length >= Call.getNumArgs() ||
          (Contract.Multiplier != std::numeric_limits<unsigned>::max() &&
           Contract.Multiplier >= Call.getNumArgs()))
        continue;
      const MemRegion *Region =
          Call.getArgSVal(Contract.Pointer).getAsRegion();
      SVal Multiplier =
          Contract.Multiplier == std::numeric_limits<unsigned>::max()
              ? UnknownVal()
              : Call.getArgSVal(Contract.Multiplier);
      SVal Length = scaledSpanLength(Contract,
                                     Call.getArgSVal(Contract.Length),
                                     Multiplier, C.getState(),
                                     C.getSValBuilder());
      std::optional<DefinedOrUnknownSVal> DefinedLength =
          Length.getAs<DefinedOrUnknownSVal>();
      Region = spanProofRegion(Region, ContractState);
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
      SVal Multiplier =
          Contract.Multiplier == std::numeric_limits<unsigned>::max()
              ? UnknownVal()
              : Call.getArgSVal(Contract.Multiplier);
      SVal Length = scaledSpanLength(Contract,
                                     Call.getArgSVal(Contract.Length),
                                     Multiplier, C.getState(),
                                     C.getSValBuilder());
      ProgramStateRef ProofState = assumeFieldSpan(
          Call.getArgExpr(Contract.Pointer),
          Call.getArgExpr(Contract.Length),
          Call.getArgSVal(Contract.Pointer),
          Call.getArgSVal(Contract.Length), C.getState(), C);
      if (!spanProven(Call.getArgSVal(Contract.Pointer), Length,
                      ProofState, C) &&
          !typedObjectSpanProven(Call.getArgExpr(Contract.Pointer),
                                 Call.getArgExpr(Contract.Length),
                                 Length, ProofState, C) &&
          !derivedContractSpanProven(Call.getArgSVal(Contract.Pointer),
                                     Length, ProofState, C)) {
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
      if (!restrictDisjointSpanProven(Call.getArgExpr(Contract.First),
                                     Call.getArgExpr(Contract.Second), First,
                                     Second) &&
          !overlapProven(First, Second, Length, C.getState(), C)) {
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
    if (declaredFreshAllocation(Function))
      if (const MemRegion *Region = Call.getReturnValue().getAsRegion())
        State = State->add<AllocatedBaseRegion>(Region->getBaseRegion());
    if (std::optional<SVal> Extent =
            declaredReturnSpanExtent(Function, Call, C)) {
      if (std::optional<DefinedOrUnknownSVal> DefinedSize =
              Extent->getAs<DefinedOrUnknownSVal>())
        if (const MemRegion *Region = Call.getReturnValue().getAsRegion()) {
          const MemRegion *Base = Region->getBaseRegion();
          State = setDynamicExtent(State, Base, *DefinedSize,
                                   C.getSValBuilder());
          State = State->set<AllocatedSpanExtent>(Base, *DefinedSize);
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
        State = IsStrlen ? State->set<StrlenSource>(ReturnSym, ArgRegion)
                         : State->set<StrnlenSource>(ReturnSym, ArgRegion);
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
      C.addTransition(applyGrants(State, Call, Spans, Disjoint,
                                  C.getSValBuilder()));
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
      C.addTransition(applyGrants(Succeeded, Call, Spans, Disjoint,
                                  C.getSValBuilder()));
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
    // Discharge every TouchedRecordSpan obligation this function's own
    // frame accumulated (see that map's comment above for why this is
    // the only reliable point to do it). Flush unconditionally and
    // first, before any of the return-type-driven early returns below,
    // so a struct desync a helper leaves behind is still caught at that
    // helper's own exit regardless of its return type.
    ProgramStateRef Flushed = flushRecordSpanObligations(State, C);
    if (Flushed != State) {
      C.addTransition(Flushed);
      State = Flushed;
    }
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
      SVal Multiplier = UnknownVal();
      if (Contract.Multiplier != std::numeric_limits<unsigned>::max()) {
        const ParmVarDecl *Parameter =
            Function->getParamDecl(Contract.Multiplier);
        Multiplier = State->getSVal(State->getLValue(Parameter, LC));
      }
      LengthValue = scaledSpanLength(Contract, LengthValue, Multiplier, State,
                                     C.getSValBuilder());
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
