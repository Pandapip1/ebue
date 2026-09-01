// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "llvm/ADT/APSInt.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace clang;
using namespace ento;

REGISTER_MAP_WITH_PROGRAMSTATE(ArithmeticContractField,
                               const StackFrameContext *, const MemRegion *)

namespace {

class SizeCastChecker : public Checker<check::PreStmt<ExplicitCastExpr>> {
  mutable std::unique_ptr<BugType> BT;

public:
  struct Interval {
    llvm::APSInt Min;
    llvm::APSInt Max;
  };

  static constexpr unsigned MathBits = 256;

  static llvm::APSInt typeMin(ASTContext &Ctx, QualType Type) {
    unsigned Bits = Ctx.getIntWidth(Type);
    bool Unsigned = Type->isUnsignedIntegerOrEnumerationType();
    return llvm::APSInt::getMinValue(Bits, Unsigned);
  }

  static llvm::APSInt typeMax(ASTContext &Ctx, QualType Type) {
    unsigned Bits = Ctx.getIntWidth(Type);
    bool Unsigned = Type->isUnsignedIntegerOrEnumerationType();
    return llvm::APSInt::getMaxValue(Bits, Unsigned);
  }

  static llvm::APSInt asSourceType(const llvm::APSInt &Value, unsigned Bits,
                                   bool Unsigned) {
    llvm::APInt Converted = Value;
    if (Converted.getBitWidth() < Bits)
      Converted =
          Value.isUnsigned() ? Converted.zext(Bits) : Converted.sext(Bits);
    else if (Converted.getBitWidth() > Bits)
      Converted = Converted.trunc(Bits);
    return llvm::APSInt(Converted, Unsigned);
  }

  static llvm::APSInt asMath(const llvm::APSInt &Value) {
    llvm::APInt Converted = Value;
    if (Converted.getBitWidth() < MathBits)
      Converted = Value.isUnsigned() ? Converted.zext(MathBits)
                                     : Converted.sext(MathBits);
    else if (Converted.getBitWidth() > MathBits)
      Converted = Converted.trunc(MathBits);
    return llvm::APSInt(Converted, false);
  }

  static Interval typeInterval(ASTContext &Ctx, QualType Type) {
    return {asMath(typeMin(Ctx, Type)), asMath(typeMax(Ctx, Type))};
  }

  static bool contains(const Interval &Outer, const Interval &Inner) {
    return llvm::APSInt::compareValues(Outer.Min, Inner.Min) <= 0 &&
           llvm::APSInt::compareValues(Outer.Max, Inner.Max) >= 0;
  }

  static llvm::APSInt minValue(std::initializer_list<llvm::APSInt> Values) {
    return *std::min_element(Values.begin(), Values.end(),
                             [](const auto &A, const auto &B) {
                               return llvm::APSInt::compareValues(A, B) < 0;
                             });
  }

  static llvm::APSInt maxValue(std::initializer_list<llvm::APSInt> Values) {
    return *std::max_element(Values.begin(), Values.end(),
                             [](const auto &A, const auto &B) {
                               return llvm::APSInt::compareValues(A, B) < 0;
                             });
  }

  // The tightest of two independently-sound over-approximations is still
  // sound: whatever the symbol's true value is, it lies in both intervals,
  // so it lies in their intersection too. Guards the one way that could
  // stop being true -- a bug in one side computing a genuinely disjoint
  // range -- by falling back to the solver-derived interval alone, which
  // this file already shipped and trusted before this lemma existed,
  // rather than ever handing back an inverted (Min > Max) interval that
  // callers would read as "no value is possible here", which is a
  // stronger and therefore unsound claim.
  static Interval intersectInterval(const Interval &Solver,
                                    const Interval &Symbolic) {
    Interval Result{maxValue({Solver.Min, Symbolic.Min}),
                    minValue({Solver.Max, Symbolic.Max})};
    if (llvm::APSInt::compareValues(Result.Min, Result.Max) > 0)
      return Solver;
    return Result;
  }

  // The binary-search-over-assume() solver query constrainedInterval()
  // already performed for a source Expr, generalized to any NonLoc so
  // symbolInterval() below can run the identical query for a bare
  // SymbolRef that names no Expr of its own (the whole point of that
  // function is to be reachable from a materialized value that was
  // stored into a variable or field and read back later).
  // State is a required, explicit parameter so recursive interval proofs
  // inspect one consistent program point.  The arithmetic-UB stage disables
  // Clang's overlapping DivideZero and BitwiseShift checkers; consequently
  // the current state retains genuine branch constraints without containing
  // a same-operation assumption supplied by a built-in checker.
  static Interval bisectInterval(NonLoc Value, QualType Type,
                                 ProgramStateRef State, CheckerContext &C) {
    ASTContext &Ctx = C.getASTContext();
    unsigned Bits = Ctx.getIntWidth(Type);
    bool Unsigned = Type->isUnsignedIntegerOrEnumerationType();
    llvm::APSInt NativeMin = typeMin(Ctx, Type);
    llvm::APSInt NativeMax = typeMax(Ctx, Type);
    llvm::APSInt One(llvm::APInt(MathBits, 1), false);
    llvm::APSInt Two(llvm::APInt(MathBits, 2), false);

    llvm::APSInt Low = asMath(NativeMin);
    llvm::APSInt High = asMath(NativeMax);
    while (Low < High) {
      llvm::APSInt Mid = Low + (High - Low) / Two;
      llvm::APSInt NativeMid = asSourceType(Mid, Bits, Unsigned);
      if (State->assumeInclusiveRange(Value, NativeMin, NativeMid, true))
        High = Mid;
      else
        Low = Mid + One;
    }
    llvm::APSInt Minimum = Low;

    Low = Minimum;
    High = asMath(NativeMax);
    while (Low < High) {
      llvm::APSInt Mid = Low + (High - Low + One) / Two;
      llvm::APSInt NativeMid = asSourceType(Mid, Bits, Unsigned);
      if (State->assumeInclusiveRange(Value, NativeMid, NativeMax, true))
        Low = Mid;
      else
        High = Mid - One;
    }
    return Interval{Minimum, Low};
  }

  static Interval combineBinary(BinaryOperator::Opcode Op,
                                const Interval &Left, const Interval &Right) {
    switch (Op) {
    case BO_Add:
      return Interval{Left.Min + Right.Min, Left.Max + Right.Max};
    case BO_Sub:
      return Interval{Left.Min - Right.Max, Left.Max - Right.Min};
    case BO_Mul: {
      llvm::APSInt A = Left.Min * Right.Min;
      llvm::APSInt B = Left.Min * Right.Max;
      llvm::APSInt D = Left.Max * Right.Min;
      llvm::APSInt E = Left.Max * Right.Max;
      return Interval{minValue({A, B, D, E}), maxValue({A, B, D, E})};
    }
    default:
      llvm_unreachable("combineBinary called with an unhandled opcode");
    }
  }

  static constexpr unsigned MaxSymbolDepth = 16;

  static bool containsRemainder(SymbolRef Sym, unsigned Depth = 0) {
    if (!Sym || Depth >= MaxSymbolDepth)
      return false;
    if (const auto *Expression = dyn_cast<SymIntExpr>(Sym))
      return Expression->getOpcode() == BO_Rem;
    if (const auto *Expression = dyn_cast<IntSymExpr>(Sym))
      return Expression->getOpcode() == BO_Rem;
    if (const auto *Expression = dyn_cast<SymSymExpr>(Sym))
      return Expression->getOpcode() == BO_Rem;
    if (const auto *Cast = dyn_cast<SymbolCast>(Sym))
      return containsRemainder(Cast->getOperand(), Depth + 1);
    return false;
  }

  // expressionInterval() below already knows how to narrow `hash % n`,
  // `mask & bits`, and `value >> shift` past their operands' full type
  // range when it walks those operators inline in the source AST -- but
  // that walk starts over from each new Expr, so it only ever sees a
  // divisor/mask/shift-count that is ITSELF still written out at the use
  // site. src/stdlib/strtod.c's bn_shl() writes `int b = k % 32;` once
  // and rereads plain `b` three lines later in `v >> (32 - b)`;
  // src/stdio/printf.c's fmt_a() writes `int shift = (13 - prec) * 4;`
  // once (prec itself bounded [0,12] by two literal ifs immediately
  // above) and rereads plain `shift` four times after. Both are the same
  // shape: a value ALREADY narrow by construction, materialized into a
  // local and re-read past the point where the source-level walk can see
  // the operator that narrowed it.
  //
  // RegionStore gives an exact answer for what that reread actually
  // finds: absent an intervening call that could have written through an
  // escaped alias, a load from that local returns the very same SVal
  // that was stored -- so the symbol behind a rereard of `b` IS the
  // SymIntExpr for `$k % 32`, not a fresh unconstrained symbol. The
  // default RangeConstraintManager's own solver does not re-derive a
  // tight range for that compound symbol on its own (nothing ever
  // branched on `b`'s value to teach it one), which is exactly why
  // expressionInterval()'s own Rem/And/Shr special cases exist in the
  // first place; symbolInterval() is that same reasoning run over the
  // SymExpr the engine already built instead of over the Expr the
  // programmer wrote, so it reaches a re-read the same way the original
  // reasoning reaches an inline use.
  //
  // Every branch here is a strict subset of what expressionInterval()
  // already computes for the equivalent AST shape (same Rem/And/Shr sign
  // and magnitude rules, same interval arithmetic for Add/Sub/Mul), so
  // this adds no new interval theory, only a second path to the existing
  // one. A shape this cannot decompose (a call result, a load through a
  // pointer, anything past MaxSymbolDepth) falls through to the same
  // solver bisection constrainedInterval() already ran, so this can only
  // ever tighten a result, never replace a sound one with an unsound
  // one -- and combined via intersectInterval(), which itself falls back
  // to the solver-only side on any disagreement.
  static Interval symbolInterval(SymbolRef Sym, ProgramStateRef State,
                                 CheckerContext &C, unsigned Depth) {
    ASTContext &Ctx = C.getASTContext();
    QualType Type = Sym->getType();
    // A sub-symbol whose own type is not an integer -- concretely, a
    // pointer-region-value symbol reached while decomposing an integer
    // cast of pointer arithmetic, e.g. `(long)p - (long)q` -- can never
    // be wrapped in nonloc::SymbolVal at all: that constructor asserts
    // !Loc::isLocType(Sym->getType()), so calling it here is not merely
    // imprecise but a guaranteed analyzer crash (confirmed directly: an
    // earlier version of this function called bisectInterval() on such a
    // symbol unconditionally and crashed clang --analyze on real files,
    // e.g. arch/aarch64/src/ld128_convert.c, with exactly that
    // assertion). Fully unbounded -- MathBits' own widest range -- is
    // always a sound, if useless, answer for a term with no integer type
    // this function has any way to reason about; the solver bisection
    // below is likewise skipped once MaxSymbolDepth is reached, for the
    // same reason expressionInterval() itself never recurses unbounded.
    if (!Type->isIntegerType())
      return {llvm::APSInt::getMinValue(MathBits, false),
             llvm::APSInt::getMaxValue(MathBits, false)};
    if (Depth >= MaxSymbolDepth)
      return bisectInterval(nonloc::SymbolVal(Sym), Type, State, C);
    Interval Bound = typeInterval(Ctx, Type);

    llvm::APSInt Zero(llvm::APInt(MathBits, 0), false);
    llvm::APSInt One(llvm::APInt(MathBits, 1), false);
    bool ResultUnsigned = Type->isUnsignedIntegerOrEnumerationType();

    if (const auto *IntExpr = dyn_cast<SymIntExpr>(Sym)) {
      BinaryOperator::Opcode Op = IntExpr->getOpcode();
      llvm::APSInt Right = asMath(IntExpr->getRHS());
      if (Op == BO_Rem && Right.isStrictlyPositive()) {
        llvm::APSInt Magnitude = Right - One;
        if (ResultUnsigned)
          return intersectInterval(Bound, Interval{Zero, Magnitude});
        Interval Left = symbolInterval(IntExpr->getLHS(), State, C, Depth + 1);
        if (Left.Min >= Zero)
          return intersectInterval(Bound, Interval{Zero, Magnitude});
        if (Left.Max <= Zero)
          return intersectInterval(Bound, Interval{-Magnitude, Zero});
        return intersectInterval(Bound, Interval{-Magnitude, Magnitude});
      }
      if (Op == BO_And && ResultUnsigned && !Right.isNegative())
        return intersectInterval(Bound, Interval{Zero, Right});
      if (Op == BO_Shr && ResultUnsigned) {
        unsigned Width = Ctx.getIntWidth(Type);
        if (!Right.isNegative() && Right.getLimitedValue() < Width) {
          unsigned Shift = static_cast<unsigned>(Right.getLimitedValue());
          llvm::APSInt Maximum = typeMax(Ctx, Type);
          Maximum = llvm::APSInt(Maximum.lshr(Shift), Maximum.isUnsigned());
          return intersectInterval(Bound, Interval{Zero, asMath(Maximum)});
        }
      }
      if (Op == BO_Add || Op == BO_Sub || Op == BO_Mul) {
        Interval Left = symbolInterval(IntExpr->getLHS(), State, C, Depth + 1);
        Interval Result = combineBinary(Op, Left, Interval{Right, Right});
        return intersectInterval(Bound, Result);
      }
    } else if (const auto *SymInt = dyn_cast<IntSymExpr>(Sym)) {
      BinaryOperator::Opcode Op = SymInt->getOpcode();
      if (Op == BO_Add || Op == BO_Sub || Op == BO_Mul) {
        llvm::APSInt Left = asMath(SymInt->getLHS());
        Interval Right = symbolInterval(SymInt->getRHS(), State, C, Depth + 1);
        Interval Result = combineBinary(Op, Interval{Left, Left}, Right);
        return intersectInterval(Bound, Result);
      }
    } else if (const auto *SymExprB = dyn_cast<SymSymExpr>(Sym)) {
      BinaryOperator::Opcode Op = SymExprB->getOpcode();
      if (Op == BO_Rem || Op == BO_Add || Op == BO_Sub || Op == BO_Mul) {
        Interval Left =
            symbolInterval(SymExprB->getLHS(), State, C, Depth + 1);
        Interval Right =
            symbolInterval(SymExprB->getRHS(), State, C, Depth + 1);
        if (Op == BO_Rem && Right.Min > Zero) {
          llvm::APSInt Magnitude = Right.Max - One;
          if (ResultUnsigned)
            return intersectInterval(Bound, Interval{Zero, Magnitude});
          if (Left.Min >= Zero)
            return intersectInterval(Bound, Interval{Zero, Magnitude});
          if (Left.Max <= Zero)
            return intersectInterval(Bound, Interval{-Magnitude, Zero});
          return intersectInterval(Bound,
                                   Interval{-Magnitude, Magnitude});
        }
        if (Op == BO_Add || Op == BO_Sub || Op == BO_Mul) {
          Interval Result = combineBinary(Op, Left, Right);
          return intersectInterval(Bound, Result);
        }
      }
    } else if (const auto *CastSym = dyn_cast<SymbolCast>(Sym)) {
      // Only trusted if the operand's own range already fits inside this
      // cast's destination type without truncation -- the same
      // contains()-gated rule expressionInterval() applies to an
      // ImplicitCastExpr, so a genuinely narrowing cast still falls
      // through to the plain solver bisection below rather than being
      // handed a pre-cast range that a truncation could have invalidated.
      Interval Operand =
          symbolInterval(CastSym->getOperand(), State, C, Depth + 1);
      if (contains(Bound, Operand))
        return Operand;
    }

    return intersectInterval(
        bisectInterval(nonloc::SymbolVal(Sym), Type, State, C), Bound);
  }

  // State defaults to C.getState(); the explicit overload exists so every
  // recursive query can be pinned to the same program point.
  static std::optional<Interval>
  constrainedInterval(const Expr *Expr, CheckerContext &C,
                      ProgramStateRef State = nullptr) {
    if (!State)
      State = C.getState();
    SVal Value = State->getSVal(Expr, C.getLocationContext());
    // Unary ++/-- hand us their lvalue operand directly, rather than the
    // ImplicitCastExpr(CK_LValueToRValue) that a binary arithmetic operand
    // contains.  Load that location explicitly so the range solver sees the
    // path constraints on the variable.  Treating the location as an unknown
    // integer interval made every guarded loop induction step look capable of
    // overflowing even at `i < 3`.
    if (Expr->isLValue() && Expr->getType()->isIntegerType())
      if (std::optional<Loc> Location = Value.getAs<Loc>())
        Value = State->getSVal(*Location, Expr->getType());
    if (const llvm::APSInt *Integer = Value.getAsInteger()) {
      llvm::APSInt Exact = asMath(*Integer);
      return Interval{Exact, Exact};
    }
    std::optional<NonLoc> Defined = Value.getAs<NonLoc>();
    if (!Defined)
      return std::nullopt;

    SymbolRef Sym = Value.getAsSymbol();
    Interval Result = bisectInterval(*Defined, Expr->getType(), State, C);
    if (Sym) {
      Interval Symbolic = symbolInterval(Sym, State, C, 0);
      // Clang retains an unsigned SymSym remainder beneath a representable
      // cast to a signed materialized local.  Its generic range query applies
      // the signed expression bounds to that unsigned SVal and can return a
      // spurious disjoint extreme.  The remainder interval is independently
      // sound; carry it through only when every possible remainder fits the
      // source expression's destination type.  All other symbol shapes keep
      // the historical solver/intersection behavior.
      Interval ExprRange = typeInterval(C.getASTContext(), Expr->getType());
      if (containsRemainder(Sym) &&
          !C.getASTContext().hasSameType(Sym->getType(), Expr->getType()) &&
          contains(ExprRange, Symbolic))
        return Symbolic;
      Result = intersectInterval(Result, Symbolic);
    }
    return Result;
  }

  // Same State-defaulting rule as constrainedInterval() just above: this
  // function threads one program point through every recursive query.
  static Interval expressionInterval(const Expr *Expr, CheckerContext &C,
                                     ProgramStateRef State = nullptr) {
    if (!State)
      State = C.getState();
    Expr = Expr->IgnoreParens();
    ASTContext &Ctx = C.getASTContext();
    Interval ResultType = typeInterval(Ctx, Expr->getType());

    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Expr)) {
      if (Cast->getCastKind() == CK_LValueToRValue) {
        if (std::optional<Interval> Known =
                constrainedInterval(Cast, C, State))
          return *Known;
        return ResultType;
      }
      Interval Operand = expressionInterval(Cast->getSubExpr(), C, State);
      return contains(ResultType, Operand) ? Operand : ResultType;
    }

    if (const auto *Unary = dyn_cast<UnaryOperator>(Expr)) {
      Interval Operand = expressionInterval(Unary->getSubExpr(), C, State);
      if (Unary->getOpcode() == UO_Plus)
        return Operand;
      if (Unary->getOpcode() == UO_Minus) {
        Interval Result{-Operand.Max, -Operand.Min};
        return contains(ResultType, Result) ? Result : ResultType;
      }
    }

    if (const auto *Binary = dyn_cast<BinaryOperator>(Expr)) {
      /* Terminal range reducers are deliberately handled before the left
       * operand.  A hash may be arbitrarily complicated, but `hash % n` is
       * bounded by n alone; walking the hash to manufacture bounds would be
       * both slower and less precise. */
      if (Binary->getOpcode() == BO_Rem) {
        Interval Right = expressionInterval(Binary->getRHS(), C, State);
        llvm::APSInt Zero(llvm::APInt(MathBits, 0), false);
        llvm::APSInt One(llvm::APInt(MathBits, 1), false);
        if (Right.Min > Zero) {
          llvm::APSInt Magnitude = Right.Max - One;
          if (Expr->getType()->isUnsignedIntegerOrEnumerationType())
            return Interval{Zero, Magnitude};
          Interval Left = expressionInterval(Binary->getLHS(), C, State);
          if (Left.Min >= Zero)
            return Interval{Zero, Magnitude};
          if (Left.Max <= Zero)
            return Interval{-Magnitude, Zero};
          return Interval{-Magnitude, Magnitude};
        }
      }
      if (Binary->getOpcode() == BO_And &&
          Expr->getType()->isUnsignedIntegerOrEnumerationType()) {
        Interval Right = expressionInterval(Binary->getRHS(), C, State);
        if (Right.Min == Right.Max && !Right.Min.isNegative()) {
          llvm::APSInt Zero(llvm::APInt(MathBits, 0), false);
          return Interval{Zero, Right.Max};
        }
      }
      if (Binary->getOpcode() == BO_Shr &&
          Expr->getType()->isUnsignedIntegerOrEnumerationType()) {
        Interval Right = expressionInterval(Binary->getRHS(), C, State);
        unsigned Width = Ctx.getIntWidth(Expr->getType());
        if (Right.Min == Right.Max && !Right.Min.isNegative() &&
            Right.Min.getLimitedValue() < Width) {
          unsigned Shift = static_cast<unsigned>(Right.Min.getLimitedValue());
          llvm::APSInt Zero(llvm::APInt(MathBits, 0), false);
          llvm::APSInt Maximum = typeMax(Ctx, Expr->getType());
          Maximum = llvm::APSInt(Maximum.lshr(Shift), Maximum.isUnsigned());
          return Interval{Zero, asMath(Maximum)};
        }
      }

      Interval Left = expressionInterval(Binary->getLHS(), C, State);
      Interval Right = expressionInterval(Binary->getRHS(), C, State);
      std::optional<Interval> Result;
      switch (Binary->getOpcode()) {
      case BO_Add:
        Result = Interval{Left.Min + Right.Min, Left.Max + Right.Max};
        break;
      case BO_Sub:
        Result = Interval{Left.Min - Right.Max, Left.Max - Right.Min};
        break;
      case BO_Mul: {
        llvm::APSInt A = Left.Min * Right.Min;
        llvm::APSInt B = Left.Min * Right.Max;
        llvm::APSInt D = Left.Max * Right.Min;
        llvm::APSInt E = Left.Max * Right.Max;
        Result = Interval{minValue({A, B, D, E}), maxValue({A, B, D, E})};
        break;
      }
      default:
        break;
      }
      if (Result && contains(ResultType, *Result))
        return *Result;
      return ResultType;
    }

    if (std::optional<Interval> Known = constrainedInterval(Expr, C, State))
      return *Known;
    return ResultType;
  }

  static std::string sourceText(const Expr *Expr, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Begin = SM.getSpellingLoc(Expr->getBeginLoc());
    SourceLocation End = SM.getSpellingLoc(Expr->getEndLoc());
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
    if (Result.empty() && Expr->getBeginLoc().isMacroID())
      Result = Lexer::getImmediateMacroNameForDiagnostics(Expr->getBeginLoc(),
                                                          SM, C.getLangOpts())
                   .str();
    if (Result.empty())
      Result = Expr->getStmtClassName();
    return Result;
  }

  static std::string sourceOrigin(const Expr *Expr, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    return SM.getFilename(SM.getExpansionLoc(Expr->getBeginLoc())).str();
  }

  static std::string sourceSite(const Expr *Expr, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Location = SM.getExpansionLoc(Expr->getBeginLoc());
    FileID File = SM.getFileID(Location);
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(File, &Invalid);
    if (Invalid)
      return Expr->getStmtClassName();
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

public:
  void checkPreStmt(const ExplicitCastExpr *Cast, CheckerContext &C) const {
    if (Cast->getCastKind() != CK_IntegralCast &&
        Cast->getCastKind() != CK_IntegralToBoolean)
      return;

    QualType Source = Cast->getSubExpr()->getType();
    QualType Dest = Cast->getType();
    if (!Source->isIntegerType() || !Dest->isIntegerType())
      return;

    ASTContext &Ctx = C.getASTContext();
    llvm::APSInt SourceMin = typeMin(Ctx, Source);
    llvm::APSInt SourceMax = typeMax(Ctx, Source);
    llvm::APSInt DestMin = typeMin(Ctx, Dest);
    llvm::APSInt DestMax = typeMax(Ctx, Dest);
    unsigned SourceBits = Ctx.getIntWidth(Source);
    unsigned DestBits = Ctx.getIntWidth(Dest);

    if (llvm::APSInt::compareValues(DestMin, SourceMin) <= 0 &&
        llvm::APSInt::compareValues(DestMax, SourceMax) >= 0)
      return;

    const llvm::APSInt &Lower =
        llvm::APSInt::compareValues(SourceMin, DestMin) >= 0 ? SourceMin
                                                             : DestMin;
    const llvm::APSInt &Upper =
        llvm::APSInt::compareValues(SourceMax, DestMax) <= 0 ? SourceMax
                                                             : DestMax;
    bool Disjoint = llvm::APSInt::compareValues(Lower, Upper) > 0;
    SVal Value = C.getSVal(Cast->getSubExpr());
    std::optional<NonLoc> Defined = Value.getAs<NonLoc>();
    ProgramStateRef Outside = C.getState();
    if (Defined && !Disjoint) {
      bool SourceUnsigned = Source->isUnsignedIntegerOrEnumerationType();
      llvm::APSInt From = asSourceType(Lower, SourceBits, SourceUnsigned);
      llvm::APSInt To = asSourceType(Upper, SourceBits, SourceUnsigned);
      Outside = C.getState()->assumeInclusiveRange(*Defined, From, To, false);
      if (!Outside)
        return;
    }
    if (SourceBits <= MathBits && DestBits <= MathBits &&
        contains(typeInterval(Ctx, Dest),
                 expressionInterval(Cast->getSubExpr(), C)))
      return;

    ExplodedNode *Node = C.generateNonFatalErrorNode(Outside);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven integer cast",
                                     categories::LogicError);

    const Decl *Current = C.getLocationContext()->getDecl();
    std::string Context = Current ? Current->getDeclKindName() : "unknown";
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      Context = Named->getQualifiedNameAsString();
    std::string Message =
        "integer cast from '" + Source.getAsString() + "' to '" +
        Dest.getAsString() + "' is not proven to preserve its value; origin '" +
        sourceOrigin(Cast, C) + "'; context '" + Context + "'; cast '" +
        sourceText(Cast, C) + "'; site '" + sourceSite(Cast, C) + "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Cast->getSourceRange());
    C.emitReport(std::move(Report));
  }
};

class ArrayIndexChecker : public Checker<check::PreStmt<ArraySubscriptExpr>> {
  mutable std::unique_ptr<BugType> BT;

  static std::string sourceText(const Expr *Expr, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Begin = SM.getSpellingLoc(Expr->getBeginLoc());
    SourceLocation End = SM.getSpellingLoc(Expr->getEndLoc());
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
    if (Result.empty() && Expr->getBeginLoc().isMacroID())
      Result = Lexer::getImmediateMacroNameForDiagnostics(Expr->getBeginLoc(),
                                                          SM, C.getLangOpts())
                   .str();
    if (Result.empty())
      Result = Expr->getStmtClassName();
    return Result;
  }

  static std::string sourceOrigin(const Expr *Expr, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    return SM.getFilename(SM.getExpansionLoc(Expr->getBeginLoc())).str();
  }

  static std::string sourceSite(const Expr *Expr, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Location = SM.getExpansionLoc(Expr->getBeginLoc());
    FileID File = SM.getFileID(Location);
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(File, &Invalid);
    if (Invalid)
      return Expr->getStmtClassName();
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

public:
  void checkPreStmt(const ArraySubscriptExpr *Subscript,
                    CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    SVal Base = C.getSVal(Subscript->getBase());
    DefinedOrUnknownSVal Count =
        getDynamicElementCountWithOffset(State, Base, Subscript->getType());
    SVal Index = C.getSVal(Subscript->getIdx());
    std::optional<NonLoc> DefinedIndex = Index.getAs<NonLoc>();
    ProgramStateRef Outside = State;
    if (DefinedIndex) {
      Outside = State->assumeInBound(*DefinedIndex, Count, false,
                                     Subscript->getIdx()->getType());
      if (!Outside)
        return;
    }

    ExplodedNode *Node = C.generateNonFatalErrorNode(Outside);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven array index",
                                     categories::LogicError);

    const Decl *Current = C.getLocationContext()->getDecl();
    std::string Context = Current ? Current->getDeclKindName() : "unknown";
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      Context = Named->getQualifiedNameAsString();
    std::string Message = "array index is not proven in bounds; origin '" +
                          sourceOrigin(Subscript, C) + "'; context '" +
                          Context + "'; subscript '" +
                          sourceText(Subscript, C) + "'; site '" +
                          sourceSite(Subscript, C) + "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Subscript->getSourceRange());
    C.emitReport(std::move(Report));
  }
};

static std::string arithmeticOrigin(const Expr *Expression, CheckerContext &C);
static std::string arithmeticText(const Stmt *Statement, CheckerContext &C);
static std::string arithmeticSite(const Expr *Expression, CheckerContext &C);
static std::string arithmeticContext(CheckerContext &C);

class SignedArithmeticChecker : public Checker<check::PreStmt<BinaryOperator>,
                                               check::PreStmt<UnaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

  static std::optional<NonLoc> integerValue(const Expr *Expression,
                                             ProgramStateRef State,
                                             CheckerContext &C) {
    SVal Value = State->getSVal(Expression, C.getLocationContext());
    if (Expression->isLValue())
      if (std::optional<Loc> Location = Value.getAs<Loc>())
        Value = State->getSVal(*Location, Expression->getType());
    return Value.getAs<NonLoc>();
  }

  static ProgramStateRef assumeComparison(ProgramStateRef State, NonLoc Left,
                                          NonLoc Right,
                                          BinaryOperator::Opcode Opcode,
                                          CheckerContext &C) {
    SVal Comparison = C.getSValBuilder().evalBinOpNN(
        State, Opcode, Left, Right, C.getASTContext().IntTy);
    std::optional<DefinedOrUnknownSVal> Defined =
        Comparison.getAs<DefinedOrUnknownSVal>();
    // An unmodelled comparison cannot establish safety.
    return Defined ? State->assume(*Defined, true) : State;
  }

  static std::optional<NonLoc>
  evaluate(ProgramStateRef State, NonLoc Left, NonLoc Right,
           BinaryOperator::Opcode Opcode, QualType Type, CheckerContext &C) {
    return C.getSValBuilder()
        .evalBinOpNN(State, Opcode, Left, Right, Type)
        .getAs<NonLoc>();
  }

  // Independent operand intervals lose relational facts such as `i < n`
  // together with n's own type bound.  Ask the path solver the standard
  // overflow predicates before reporting.  This is only an additional proof
  // of safety: any predicate the solver cannot model remains feasible.
  static bool addOrSubOverflowFeasible(const BinaryOperator *Operation,
                                       CheckerContext &C, bool Subtract) {
    ProgramStateRef Input = C.getState();
    QualType Type = Operation->getType();
    std::optional<NonLoc> Left =
        integerValue(Operation->getLHS(), Input, C);
    std::optional<NonLoc> Right =
        integerValue(Operation->getRHS(), Input, C);
    if (!Left || !Right)
      return true;
    SValBuilder &Builder = C.getSValBuilder();
    NonLoc Zero = Builder.makeIntVal(0, Type).castAs<NonLoc>();
    NonLoc Maximum = Builder.makeIntVal(
        SizeCastChecker::typeMax(C.getASTContext(), Type));
    NonLoc Minimum = Builder.makeIntVal(
        SizeCastChecker::typeMin(C.getASTContext(), Type));

    // Relational guards can prove subtraction safe without manufacturing a
    // MIN+right or MAX+right boundary expression.  If 0 <= right <= left,
    // the result is in [0, MAX]; symmetrically, left <= right <= 0 puts it in
    // [MIN, 0].  These are sufficient conditions only: an unmodelled or
    // feasible negation falls through to the general overflow predicates.
    if (Subtract) {
      if (!assumeComparison(Input, *Right, Zero, BO_LT, C) &&
          !assumeComparison(Input, *Left, *Right, BO_LT, C))
        return false;
      if (!assumeComparison(Input, *Right, Zero, BO_GT, C) &&
          !assumeComparison(Input, *Left, *Right, BO_GT, C))
        return false;
    }

    ProgramStateRef Positive =
        assumeComparison(Input, *Right, Zero, BO_GT, C);
    if (Positive) {
      BinaryOperator::Opcode LimitOp = Subtract ? BO_Add : BO_Sub;
      NonLoc BoundBase = Subtract ? Minimum : Maximum;
      std::optional<NonLoc> Limit =
          evaluate(Positive, BoundBase, *Right, LimitOp, Type, C);
      if (!Limit || assumeComparison(Positive, *Left, *Limit,
                                     Subtract ? BO_LT : BO_GT, C))
        return true;
    }

    ProgramStateRef Negative =
        assumeComparison(Input, *Right, Zero, BO_LT, C);
    if (Negative) {
      BinaryOperator::Opcode LimitOp = Subtract ? BO_Add : BO_Sub;
      NonLoc BoundBase = Subtract ? Maximum : Minimum;
      std::optional<NonLoc> Limit =
          evaluate(Negative, BoundBase, *Right, LimitOp, Type, C);
      if (!Limit || assumeComparison(Negative, *Left, *Limit,
                                     Subtract ? BO_GT : BO_LT, C))
        return true;
    }
    return false;
  }

  static bool multiplicationOverflowFeasible(const BinaryOperator *Operation,
                                              CheckerContext &C) {
    ProgramStateRef Input = C.getState();
    QualType Type = Operation->getType();
    std::optional<NonLoc> Left =
        integerValue(Operation->getLHS(), Input, C);
    std::optional<NonLoc> Right =
        integerValue(Operation->getRHS(), Input, C);
    if (!Left || !Right)
      return true;
    SValBuilder &Builder = C.getSValBuilder();
    NonLoc Zero = Builder.makeIntVal(0, Type).castAs<NonLoc>();
    NonLoc MinusOne = Builder.makeIntVal(llvm::APSInt(
        llvm::APInt::getAllOnes(C.getASTContext().getIntWidth(Type)), false));
    NonLoc Maximum = Builder.makeIntVal(
        SizeCastChecker::typeMax(C.getASTContext(), Type));
    NonLoc Minimum = Builder.makeIntVal(
        SizeCastChecker::typeMin(C.getASTContext(), Type));

    struct SignCase {
      BinaryOperator::Opcode LeftSign;
      BinaryOperator::Opcode RightSign;
      NonLoc RightBound;
      NonLoc Numerator;
      BinaryOperator::Opcode OverflowComparison;
    };
    const SignCase Cases[] = {
        {BO_GT, BO_GT, Zero, Maximum, BO_GT},
        {BO_GT, BO_LT, MinusOne, Minimum, BO_GT},
        {BO_LT, BO_GT, Zero, Minimum, BO_LT},
        {BO_LT, BO_LT, Zero, Maximum, BO_LT},
    };
    for (const SignCase &Case : Cases) {
      ProgramStateRef State =
          assumeComparison(Input, *Left, Zero, Case.LeftSign, C);
      if (!State)
        continue;
      State = assumeComparison(State, *Right, Case.RightBound, Case.RightSign,
                               C);
      if (!State)
        continue;
      std::optional<NonLoc> Limit =
          evaluate(State, Case.Numerator, *Right, BO_Div, Type, C);
      if (!Limit || assumeComparison(State, *Left, *Limit,
                                     Case.OverflowComparison, C))
        return true;
    }
    return false;
  }

  static bool outside(const SizeCastChecker::Interval &Range,
                      const SizeCastChecker::Interval &Type) {
    return llvm::APSInt::compareValues(Range.Min, Type.Min) < 0 ||
           llvm::APSInt::compareValues(Range.Max, Type.Max) > 0;
  }

  void report(const Expr *Expression, CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(C.getState());
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven signed arithmetic",
                                     categories::LogicError);
    std::string Message =
        "signed arithmetic result is not proven representable; origin '" +
        arithmeticOrigin(Expression, C) + "'; context '" +
        arithmeticContext(C) + "'; expression '" +
        arithmeticText(Expression, C) + "'; site '" +
        arithmeticSite(Expression, C) + "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Expression->getSourceRange());
    C.emitReport(std::move(Report));
  }

public:
  void checkPreStmt(const BinaryOperator *Operation, CheckerContext &C) const {
    // Pointer subtraction has a signed ptrdiff_t result, but its validity is
    // not an ordinary signed-integer overflow question: C requires both
    // common array provenance and a representable element distance.  Those
    // are pointer-provenance/object-bound obligations.  Applying the generic
    // integer interval rule here loses the pointer regions and independently
    // reports on an operation whose validity it cannot decide.
    if (Operation->getOpcode() == BO_Sub &&
        Operation->getLHS()->getType()->isPointerType() &&
        Operation->getRHS()->getType()->isPointerType())
      return;
    QualType Type = Operation->getType();
    if (!Type->isSignedIntegerType())
      return;
    auto Left = SizeCastChecker::expressionInterval(Operation->getLHS(), C);
    auto Right = SizeCastChecker::expressionInterval(Operation->getRHS(), C);
    auto Bounds = SizeCastChecker::typeInterval(C.getASTContext(), Type);
    std::optional<SizeCastChecker::Interval> Result;
    switch (Operation->getOpcode()) {
    case BO_Add:
    case BO_AddAssign:
      Result =
          SizeCastChecker::Interval{Left.Min + Right.Min, Left.Max + Right.Max};
      if (outside(*Result, Bounds) &&
          !addOrSubOverflowFeasible(Operation, C, false))
        return;
      break;
    case BO_Sub:
    case BO_SubAssign:
      Result =
          SizeCastChecker::Interval{Left.Min - Right.Max, Left.Max - Right.Min};
      if (outside(*Result, Bounds) &&
          !addOrSubOverflowFeasible(Operation, C, true))
        return;
      break;
    case BO_Mul:
    case BO_MulAssign: {
      llvm::APSInt A = Left.Min * Right.Min;
      llvm::APSInt B = Left.Min * Right.Max;
      llvm::APSInt D = Left.Max * Right.Min;
      llvm::APSInt E = Left.Max * Right.Max;
      Result =
          SizeCastChecker::Interval{SizeCastChecker::minValue({A, B, D, E}),
                                    SizeCastChecker::maxValue({A, B, D, E})};
      if (outside(*Result, Bounds) &&
          !multiplicationOverflowFeasible(Operation, C))
        return;
      break;
    }
    case BO_Shl:
    case BO_ShlAssign: {
      unsigned Width = C.getASTContext().getIntWidth(Type);
      if (Left.Min.isNegative() || Right.Min.isNegative() ||
          Right.Max.getLimitedValue() >= Width) {
        report(Operation, C);
        return;
      }
      unsigned LowShift = static_cast<unsigned>(Right.Min.getLimitedValue());
      unsigned HighShift = static_cast<unsigned>(Right.Max.getLimitedValue());
      Result = SizeCastChecker::Interval{
          llvm::APSInt(Left.Min.shl(LowShift), false),
          llvm::APSInt(Left.Max.shl(HighShift), false)};
      break;
    }
    case BO_Div:
    case BO_Rem:
    case BO_DivAssign:
    case BO_RemAssign: {
      llvm::APSInt MinusOne(llvm::APInt(SizeCastChecker::MathBits, 1), false);
      MinusOne = -MinusOne;
      if (Left.Min <= Bounds.Min && Left.Max >= Bounds.Min &&
          Right.Min <= MinusOne && Right.Max >= MinusOne)
        report(Operation, C);
      return;
    }
    default:
      return;
    }
    if (Result && outside(*Result, Bounds))
      report(Operation, C);
  }

  void checkPreStmt(const UnaryOperator *Operation, CheckerContext &C) const {
    UnaryOperatorKind Opcode = Operation->getOpcode();
    if (Opcode != UO_Minus && Opcode != UO_PreInc && Opcode != UO_PostInc &&
        Opcode != UO_PreDec && Opcode != UO_PostDec)
      return;
    QualType Type = Operation->getType();
    if (!Type->isSignedIntegerType())
      return;
    auto Operand =
        SizeCastChecker::expressionInterval(Operation->getSubExpr(), C);
    auto Bounds = SizeCastChecker::typeInterval(C.getASTContext(), Type);
    bool Unsafe = Opcode == UO_Minus ? Operand.Min <= Bounds.Min
                  : (Opcode == UO_PreInc || Opcode == UO_PostInc)
                      ? Operand.Max >= Bounds.Max
                      : Operand.Min <= Bounds.Min;
    if (Unsafe)
      report(Operation, C);
  }
};

static std::string arithmeticOrigin(const Expr *Expression, CheckerContext &C) {
  const SourceManager &SM = C.getSourceManager();
  return SM.getFilename(SM.getExpansionLoc(Expression->getBeginLoc())).str();
}

static std::string arithmeticText(const Stmt *Statement, CheckerContext &C) {
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

static std::string arithmeticSite(const Expr *Expression, CheckerContext &C) {
  const SourceManager &SM = C.getSourceManager();
  SourceLocation Location = SM.getExpansionLoc(Expression->getBeginLoc());
  FileID File = SM.getFileID(Location);
  bool Invalid = false;
  StringRef Buffer = SM.getBufferData(File, &Invalid);
  if (Invalid)
    return Expression->getStmtClassName();
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

static std::string arithmeticContext(CheckerContext &C) {
  const Decl *Current = C.getLocationContext()->getDecl();
  if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
    return Named->getQualifiedNameAsString();
  return Current ? Current->getDeclKindName() : "unknown";
}

class ArithmeticContractChecker
    : public Checker<eval::Call, check::BeginFunction, check::PreCall,
                     check::PostCall, check::EndFunction> {
  mutable std::unique_ptr<BugType> BT;

  struct RangeContract {
    int64_t Minimum;
    int64_t Maximum;
  };

  struct FieldContract {
    unsigned Argument;
    StringRef Field;
  };

  static std::optional<RangeContract> rangeContract(const ParmVarDecl *Param) {
    for (const AnnotateAttr *Attr : Param->specific_attrs<AnnotateAttr>()) {
      StringRef Text = Attr->getAnnotation();
      if (!Text.consume_front("ntlibc_arith_range:"))
        continue;
      auto Parts = Text.split(':');
      int64_t Minimum, Maximum;
      if (Parts.first.getAsInteger(10, Minimum) ||
          Parts.second.getAsInteger(10, Maximum) || Minimum > Maximum)
        return std::nullopt;
      return RangeContract{Minimum, Maximum};
    }
    return std::nullopt;
  }

  static bool hasAnnotation(const FunctionDecl *Function, StringRef Name) {
    for (const FunctionDecl *Redeclaration : Function->redecls())
      for (const AnnotateAttr *Attr :
           Redeclaration->specific_attrs<AnnotateAttr>())
        if (Attr->getAnnotation() == Name)
          return true;
    return false;
  }

  class AddressUseVisitor : public RecursiveASTVisitor<AddressUseVisitor> {
    const FunctionDecl *Target;
    ASTContext &Ctx;

    bool isDirectCallee(const DeclRefExpr *Reference) const {
      DynTypedNode Node = DynTypedNode::create(*Reference);
      for (;;) {
        auto Parents = Ctx.getParents(Node);
        if (Parents.size() != 1)
          return false;
        if (const auto *Call = Parents[0].get<CallExpr>()) {
          const FunctionDecl *Direct = Call->getDirectCallee();
          return Direct &&
                 Direct->getCanonicalDecl() == Target->getCanonicalDecl();
        }
        const Expr *Parent = Parents[0].get<Expr>();
        if (!Parent || (!isa<ImplicitCastExpr>(Parent) &&
                        !isa<ParenExpr>(Parent)))
          return false;
        Node = DynTypedNode::create(*Parent);
      }
    }

  public:
    bool AddressTaken = false;

    AddressUseVisitor(const FunctionDecl *Target, ASTContext &Ctx)
        : Target(Target), Ctx(Ctx) {}

    bool VisitDeclRefExpr(const DeclRefExpr *Reference) {
      const auto *Function = dyn_cast<FunctionDecl>(Reference->getDecl());
      if (Function &&
          Function->getCanonicalDecl() == Target->getCanonicalDecl() &&
          !isDirectCallee(Reference))
        AddressTaken = true;
      return !AddressTaken;
    }
  };

  static bool directOnlyRangeCallee(const FunctionDecl *Function,
                                    ASTContext &Ctx) {
    if (Function->getStorageClass() != SC_Static)
      return false;
    AddressUseVisitor Visitor(Function, Ctx);
    Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    return !Visitor.AddressTaken;
  }

  static bool verifiedScalarNoop(const FunctionDecl *Function) {
    const FunctionDecl *Definition = Function->getDefinition();
    if (!Definition)
      return true; // The annotated declaration's defining TU is linted too.
    const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
    if (!Body || Body->size() != 1 || Definition->getNumParams() != 1)
      return false;
    const auto *Call = dyn_cast<CallExpr>(*Body->body_begin());
    if (!Call || Call->getNumArgs() != 1)
      return false;
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || Callee->getName() != "free")
      return false;
    const auto *Argument = dyn_cast<DeclRefExpr>(
        Call->getArg(0)->IgnoreParenImpCasts());
    return Argument &&
           Argument->getDecl() == Definition->getParamDecl(0);
  }

  static std::optional<FieldContract>
  fieldContract(const FunctionDecl *Function) {
    for (const FunctionDecl *Redeclaration : Function->redecls()) {
      for (const AnnotateAttr *Attr :
           Redeclaration->specific_attrs<AnnotateAttr>()) {
        StringRef Text = Attr->getAnnotation();
        if (!Text.consume_front(
                "ntlibc_arith_nonzero_field_on_success:"))
          continue;
        auto Parts = Text.split(':');
        unsigned Argument;
        if (Parts.first.getAsInteger(10, Argument) || Parts.second.empty())
          return std::nullopt;
        return FieldContract{Argument, Parts.second};
      }
    }
    return std::nullopt;
  }

  static const FunctionDecl *function(const CallEvent &Call) {
    return dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  }

  static const FunctionDecl *definition(const FunctionDecl *Function) {
    if (!Function)
      return nullptr;
    if (const FunctionDecl *Definition = Function->getDefinition())
      return Definition;
    return Function;
  }

  static std::optional<std::pair<llvm::APSInt, llvm::APSInt>>
  nativeRange(QualType Type, const RangeContract &Contract,
              CheckerContext &C) {
    if (Type.isNull() || !Type->isIntegerType())
      return std::nullopt;
    llvm::APSInt MinimumMath(
        llvm::APInt(SizeCastChecker::MathBits,
                    static_cast<uint64_t>(Contract.Minimum), true),
        false);
    llvm::APSInt MaximumMath(
        llvm::APInt(SizeCastChecker::MathBits,
                    static_cast<uint64_t>(Contract.Maximum), true),
        false);
    SizeCastChecker::Interval Bounds =
        SizeCastChecker::typeInterval(C.getASTContext(), Type);
    if (MinimumMath < Bounds.Min || MaximumMath > Bounds.Max)
      return std::nullopt;
    unsigned Bits = C.getASTContext().getIntWidth(Type);
    bool Unsigned = Type->isUnsignedIntegerOrEnumerationType();
    return std::pair{
        SizeCastChecker::asSourceType(MinimumMath, Bits, Unsigned),
        SizeCastChecker::asSourceType(MaximumMath, Bits, Unsigned)};
  }

  static const FieldDecl *field(const ParmVarDecl *Parameter,
                                StringRef Name) {
    QualType Type = Parameter->getType();
    if (!Type->isPointerType())
      return nullptr;
    const RecordType *Record = Type->getPointeeType()->getAs<RecordType>();
    if (!Record)
      return nullptr;
    for (const FieldDecl *Candidate : Record->getDecl()->fields())
      if (Candidate->getName() == Name)
        return Candidate;
    return nullptr;
  }

  static std::optional<DefinedOrUnknownSVal>
  fieldValue(ProgramStateRef State, SVal Pointer, const FieldDecl *Field,
             CheckerContext &C) {
    if (!Pointer.getAsRegion() || !Field)
      return std::nullopt;
    SVal Location = State->getLValue(Field, Pointer);
    return State->getSVal(Location.castAs<Loc>())
        .getAs<DefinedOrUnknownSVal>();
  }

  void report(const Stmt *Statement, ProgramStateRef State, StringRef Detail,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Violated arithmetic contract",
                                     categories::LogicError);
    std::string Message =
        "arithmetic contract is not proven: " + Detail.str() + "; origin '" +
        arithmeticOrigin(cast<Expr>(Statement), C) + "'; context '" +
        arithmeticContext(C) + "'; expression '" +
        arithmeticText(Statement, C) + "'; site '" +
        arithmeticSite(cast<Expr>(Statement), C) + "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  void reportInvalidNoop(const FunctionDecl *Function,
                         CheckerContext &C) const {
    const Stmt *Body = Function->getBody();
    ExplodedNode *Node = C.generateNonFatalErrorNode(C.getState());
    if (!Node || !Body)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Violated arithmetic contract",
                                     categories::LogicError);
    const SourceManager &SM = C.getSourceManager();
    std::string Origin =
        SM.getFilename(SM.getExpansionLoc(Body->getBeginLoc())).str();
    std::string Message =
        "arithmetic contract is not proven: annotated scalar no-op is not "
        "the exact free(parameter) wrapper; origin '" +
        Origin + "'; context '" + Function->getQualifiedNameAsString() +
        "'; expression 'function body'; site 'annotated function "
        "definition'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Body->getSourceRange());
    C.emitReport(std::move(Report));
  }

public:
  bool evalCall(const CallEvent &Call, CheckerContext &C) const {
    const FunctionDecl *Function = function(Call);
    if (!Function || Function->getName() != "__free" ||
        Call.getNumArgs() != 1 || !Function->getReturnType()->isVoidType() ||
        !hasAnnotation(Function, "ntlibc_arith_scalar_noop") ||
        !verifiedScalarNoop(Function))
      return false;
    // ntlibc's internal __free is a one-line allocator wrapper.  It changes
    // only allocation lifetime/allocator-private bookkeeping; it cannot
    // mutate the caller's scalar objects or globals.  The arithmetic stage
    // does not model heap lifetime, so treating this wrapper as an arithmetic
    // no-op preserves relational guards across cleanup loops without making
    // any claim about memory that remains legal to access after the free.
    C.addTransition(C.getState());
    return true;
  }

  void checkBeginFunction(CheckerContext &C) const {
    const auto *Function = definition(
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl()));
    if (!Function)
      return;
    if (Function->getName() == "__free" &&
        hasAnnotation(Function, "ntlibc_arith_scalar_noop") &&
        !verifiedScalarNoop(Function)) {
      reportInvalidNoop(Function, C);
      return;
    }
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      auto Contract = rangeContract(Parameter);
      if (!Contract || !directOnlyRangeCallee(Function, C.getASTContext()))
        continue;
      auto Bounds = nativeRange(Parameter->getType(), *Contract, C);
      if (!Bounds)
        continue;
      SVal Value = State->getSVal(
          State->getLValue(Parameter, C.getLocationContext()));
      auto Defined = Value.getAs<DefinedOrUnknownSVal>();
      if (!Defined)
        continue;
      ProgramStateRef Restricted = State->assumeInclusiveRange(
          *Defined, Bounds->first, Bounds->second, true);
      if (Restricted && Restricted != State) {
        State = Restricted;
        Changed = true;
      }
    }
    if (auto Contract = fieldContract(Function)) {
      if (Contract->Argument < Function->getNumParams()) {
        const ParmVarDecl *Parameter =
            Function->getParamDecl(Contract->Argument);
        const FieldDecl *Field = field(Parameter, Contract->Field);
        SVal Pointer = State->getSVal(
            State->getLValue(Parameter, C.getLocationContext()));
        if (Pointer.getAsRegion() && Field) {
          SVal Location = State->getLValue(Field, Pointer);
          if (const MemRegion *Region = Location.getAsRegion()) {
            State = State->set<ArithmeticContractField>(
                C.getLocationContext()->getStackFrame(), Region);
            Changed = true;
          }
        }
      }
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const FunctionDecl *Function = function(Call);
    if (!Function)
      return;
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (unsigned Index = 0;
         Index < Function->getNumParams() && Index < Call.getNumArgs();
         ++Index) {
      auto Contract = rangeContract(Function->getParamDecl(Index));
      if (!Contract)
        continue;
      auto Bounds =
          nativeRange(Function->getParamDecl(Index)->getType(), *Contract, C);
      auto Argument = Call.getArgSVal(Index).getAs<DefinedOrUnknownSVal>();
      if (!Bounds || !Argument)
        continue;
      ProgramStateRef Violation = State->assumeInclusiveRange(
          *Argument, Bounds->first, Bounds->second, false);
      if (Violation) {
        const Expr *Expression = Call.getArgExpr(Index);
        std::string Detail = "argument " + std::to_string(Index + 1) +
                             " is outside declared range [" +
                             std::to_string(Contract->Minimum) + ", " +
                             std::to_string(Contract->Maximum) + "]";
        report(Expression, Violation, Detail, C);
      }
      State = State->assumeInclusiveRange(*Argument, Bounds->first,
                                          Bounds->second, true);
      if (!State)
        return;
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    const FunctionDecl *Function = function(Call);
    const FunctionDecl *Definition = Function ? Function->getDefinition()
                                              : nullptr;
    auto Contract = Definition ? fieldContract(Definition) : std::nullopt;
    if (!Contract || Contract->Argument >= Call.getNumArgs() ||
        Contract->Argument >= Function->getNumParams())
      return;
    auto Return = Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
    const FieldDecl *Field = field(
        Function->getParamDecl(Contract->Argument), Contract->Field);
    if (!Return || !Field)
      return;
    ProgramStateRef State = C.getState();
    ProgramStateRef Failure = State->assume(*Return, false);
    ProgramStateRef Success = State->assume(*Return, true);
    if (Success) {
      SVal Pointer = Call.getArgSVal(Contract->Argument);
      SVal Location = Success->getLValue(Field, Pointer);
      if (auto FieldLocation = Location.getAs<Loc>()) {
        DefinedOrUnknownSVal Fresh =
            C.getSValBuilder().conjureSymbolVal(
                this, Call.getOriginExpr(), C.getLocationContext(),
                Field->getType(), C.blockCount());
        Success =
            Success->bindLoc(*FieldLocation, Fresh, C.getLocationContext());
        Success = Success->assume(Fresh, true);
      }
    }
    if (Failure)
      C.addTransition(Failure);
    if (Success)
      C.addTransition(Success);
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    const auto *Function = definition(
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl()));
    auto Contract = Function ? fieldContract(Function) : std::nullopt;
    if (!Contract || !Return || !Return->getRetValue() ||
        Contract->Argument >= Function->getNumParams())
      return;
    ProgramStateRef State = C.getState();
    auto Returned = C.getSVal(Return->getRetValue())
                        .getAs<DefinedOrUnknownSVal>();
    if (!Returned)
      return;
    ProgramStateRef Success = State->assume(*Returned, true);
    if (!Success)
      return;
    const MemRegion *const *Region = Success->get<ArithmeticContractField>(
        C.getLocationContext()->getStackFrame());
    auto Value = Region ? Success->getSVal(*Region)
                              .getAs<DefinedOrUnknownSVal>()
                        : std::nullopt;
    if (!Value) {
      report(Return->getRetValue(), Success,
             "successful return cannot prove nonzero field '" +
                 Contract->Field.str() + "'",
             C);
      return;
    }
    ProgramStateRef Violation = Success->assume(*Value, false);
    if (Violation)
      report(Return->getRetValue(), Violation,
             "successful return does not establish nonzero field '" +
                 Contract->Field.str() + "'",
             C);
  }
};

class DivisorChecker : public Checker<check::PreStmt<BinaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

public:
  void checkPreStmt(const BinaryOperator *Operation, CheckerContext &C) const {
    BinaryOperatorKind Opcode = Operation->getOpcode();
    if (Opcode != BO_Div && Opcode != BO_Rem && Opcode != BO_DivAssign &&
        Opcode != BO_RemAssign)
      return;
    if (!Operation->getLHS()->getType()->isIntegerType() ||
        !Operation->getRHS()->getType()->isIntegerType())
      return;
    ProgramStateRef Input = C.getState();
    ProgramStateRef Violation = Input;
    if (std::optional<DefinedOrUnknownSVal> Divisor =
            C.getSVal(Operation->getRHS()).getAs<DefinedOrUnknownSVal>()) {
      Violation = Violation->assume(*Divisor, false);
      if (!Violation)
        return;
    }
    // The assume() above asks the path-sensitive solver alone, which
    // (being the default RangeConstraintManager, not an SMT backend)
    // does not re-derive a divisor's range from an already-narrow
    // SymExpr the way SizeCastChecker::expressionInterval() does --
    // that reasoning (the BO_Rem/BO_And/BO_Shr special cases, plus the
    // symbolInterval() decomposition of a materialized local or field
    // behind the same SVal) was already built for SignedArithmeticChecker
    // and applies just as soundly here: any divisor the solver alone
    // could not rule out zero for, but whose statically-computed range
    // provably excludes zero, is a second, independent, purely-additive
    // proof this checker never tried before.  The stage disables Clang's
    // core.DivideZero checker, so querying the current state cannot borrow a
    // same-operation nonzero assumption from that overlapping checker.
    SizeCastChecker::Interval Range = SizeCastChecker::expressionInterval(
        Operation->getRHS(), C, Input);
    llvm::APSInt Zero(llvm::APInt(SizeCastChecker::MathBits, 0), false);
    if (Range.Min > Zero || Range.Max < Zero)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(Violation);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven nonzero divisor",
                                     categories::LogicError);
    std::string Message = "divisor is not proven nonzero; origin '" +
                          arithmeticOrigin(Operation, C) + "'; context '" +
                          arithmeticContext(C) + "'; expression '" +
                          arithmeticText(Operation, C) + "'; site '" +
                          arithmeticSite(Operation, C) + "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Operation->getRHS()->getSourceRange());
    C.emitReport(std::move(Report));
  }
};

class ShiftCountChecker : public Checker<check::PreStmt<BinaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

public:
  void checkPreStmt(const BinaryOperator *Operation, CheckerContext &C) const {
    BinaryOperatorKind Opcode = Operation->getOpcode();
    if (Opcode != BO_Shl && Opcode != BO_Shr && Opcode != BO_ShlAssign &&
        Opcode != BO_ShrAssign)
      return;
    ASTContext &Context = C.getASTContext();
    QualType ValueType = Operation->getLHS()->getType();
    QualType CountType = Operation->getRHS()->getType();
    if (!ValueType->isIntegerType() || !CountType->isIntegerType())
      return;
    unsigned Width = Context.getIntWidth(ValueType);
    unsigned CountBits = Context.getIntWidth(CountType);
    bool CountUnsigned = CountType->isUnsignedIntegerOrEnumerationType();
    llvm::APSInt Low(llvm::APInt(CountBits, 0), CountUnsigned);
    llvm::APSInt High(llvm::APInt(CountBits, Width - 1), CountUnsigned);
    ProgramStateRef Input = C.getState();
    ProgramStateRef Violation = Input;
    if (std::optional<DefinedOrUnknownSVal> Count =
            C.getSVal(Operation->getRHS()).getAs<DefinedOrUnknownSVal>()) {
      Violation = Violation->assumeInclusiveRange(*Count, Low, High, false);
      if (!Violation)
        return;
    }
    // Same second, independent proof avenue as DivisorChecker just above.
    // The stage likewise disables core.BitwiseShift, so the raw solver's
    // assumeInclusiveRange() only sees a shift count's own SVal, not the
    // Rem/And/Shr/Add/Sub/Mul structure expressionInterval() (and,
    // through it, symbolInterval() for a materialized local or field)
    // already reconstructs -- e.g. `b = k % 32;` used three lines later
    // as `v >> (32 - b)` was previously provable as SignedArithmetic's
    // `32 - b` result but NOT as this checker's own shift count, purely
    // because this checker never consulted the same interval before
    // falling back to reporting.
    SizeCastChecker::Interval CountRange = SizeCastChecker::expressionInterval(
        Operation->getRHS(), C, Input);
    SizeCastChecker::Interval Safe{SizeCastChecker::asMath(Low),
                                   SizeCastChecker::asMath(High)};
    if (SizeCastChecker::contains(Safe, CountRange))
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(Violation);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven shift count",
                                     categories::LogicError);
    std::string Message = "shift count is not proven in range [0, " +
                          std::to_string(Width) + "); origin '" +
                          arithmeticOrigin(Operation, C) + "'; context '" +
                          arithmeticContext(C) + "'; expression '" +
                          arithmeticText(Operation, C) + "'; site '" +
                          arithmeticSite(Operation, C) + "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Operation->getRHS()->getSourceRange());
    C.emitReport(std::move(Report));
  }
};

class TaggedResultChecker : public Checker<check::Location> {
  mutable std::unique_ptr<BugType> BT;

  static std::string sourceOrigin(const Stmt *Statement, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    return SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())).str();
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

public:
  void checkLocation(SVal Location, bool IsLoad, const Stmt *Statement,
                     CheckerContext &C) const {
    if (!IsLoad)
      return;
    const auto *Field = dyn_cast_or_null<FieldRegion>(Location.getAsRegion());
    if (!Field)
      return;
    const FieldDecl *Accessed = Field->getDecl();
    StringRef FieldName = Accessed->getName();
    bool WantsNormal = FieldName == "normal";
    bool WantsSpecial = FieldName == "special";
    if (!WantsNormal && !WantsSpecial)
      return;
    const RecordDecl *Record = Accessed->getParent();
    if (!Record->getName().ends_with("_variant_result"))
      return;

    const FieldDecl *Kind = nullptr;
    for (const FieldDecl *Candidate : Record->fields()) {
      if (Candidate->getName() == "kind") {
        Kind = Candidate;
        break;
      }
    }
    ProgramStateRef State = C.getState();
    ProgramStateRef Violation = State;
    const auto *Super = dyn_cast<SubRegion>(Field->getSuperRegion());
    if (Kind && Super) {
      const FieldRegion *KindRegion =
          C.getSValBuilder().getRegionManager().getFieldRegion(Kind, Super);
      SVal KindValue = State->getSVal(KindRegion);
      if (std::optional<NonLoc> Defined = KindValue.getAs<NonLoc>()) {
        Violation = State->assume(*Defined, WantsNormal);
        if (!Violation)
          return;
      }
    }

    ExplodedNode *Node = C.generateNonFatalErrorNode(Violation);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unselected tagged result field",
                                     categories::LogicError);
    const Decl *Current = C.getLocationContext()->getDecl();
    std::string Context = Current ? Current->getDeclKindName() : "unknown";
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      Context = Named->getQualifiedNameAsString();
    std::string Message = "tagged result field '" + FieldName.str() +
                          "' is not proven selected; origin '" +
                          sourceOrigin(Statement, C) + "'; context '" +
                          Context + "'; access '" + sourceText(Statement, C) +
                          "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<SizeCastChecker>(
      "ntlibc.SizeCast", "Proves that explicit integer casts preserve values",
      "");
  Registry.addChecker<ArrayIndexChecker>(
      "ntlibc.ArrayIndex", "Proves that array indices are in bounds", "");
  Registry.addChecker<TaggedResultChecker>(
      "ntlibc.TaggedResult",
      "Proves that tagged normal and special result fields are selected", "");
  Registry.addChecker<DivisorChecker>(
      "ntlibc.Divisor", "Proves that integer divisors are nonzero", "");
  Registry.addChecker<ShiftCountChecker>(
      "ntlibc.ShiftCount", "Proves that integer shift counts are in range", "");
  Registry.addChecker<SignedArithmeticChecker>(
      "ntlibc.SignedArithmetic",
      "Proves that signed arithmetic results are representable", "");
  Registry.addChecker<ArithmeticContractChecker>(
      "ntlibc.ArithmeticContract",
      "Enforces arithmetic parameter and successful-call contracts", "");
}
