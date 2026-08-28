// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "llvm/ADT/APSInt.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

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

  static std::optional<Interval> constrainedInterval(const Expr *Expr,
                                                     CheckerContext &C) {
    SVal Value = C.getSVal(Expr);
    if (const llvm::APSInt *Integer = Value.getAsInteger()) {
      llvm::APSInt Exact = asMath(*Integer);
      return Interval{Exact, Exact};
    }
    std::optional<NonLoc> Defined = Value.getAs<NonLoc>();
    if (!Defined)
      return std::nullopt;

    ASTContext &Ctx = C.getASTContext();
    QualType Type = Expr->getType();
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
      if (C.getState()->assumeInclusiveRange(*Defined, NativeMin, NativeMid,
                                             true))
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
      if (C.getState()->assumeInclusiveRange(*Defined, NativeMid, NativeMax,
                                             true))
        Low = Mid;
      else
        High = Mid - One;
    }
    return Interval{Minimum, Low};
  }

  static Interval expressionInterval(const Expr *Expr, CheckerContext &C) {
    Expr = Expr->IgnoreParens();
    ASTContext &Ctx = C.getASTContext();
    Interval ResultType = typeInterval(Ctx, Expr->getType());

    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Expr)) {
      if (Cast->getCastKind() == CK_LValueToRValue) {
        if (std::optional<Interval> Known = constrainedInterval(Cast, C))
          return *Known;
        return ResultType;
      }
      Interval Operand = expressionInterval(Cast->getSubExpr(), C);
      return contains(ResultType, Operand) ? Operand : ResultType;
    }

    if (const auto *Unary = dyn_cast<UnaryOperator>(Expr)) {
      Interval Operand = expressionInterval(Unary->getSubExpr(), C);
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
        Interval Right = expressionInterval(Binary->getRHS(), C);
        llvm::APSInt Zero(llvm::APInt(MathBits, 0), false);
        llvm::APSInt One(llvm::APInt(MathBits, 1), false);
        if (Right.Min > Zero) {
          llvm::APSInt Magnitude = Right.Max - One;
          if (Expr->getType()->isUnsignedIntegerOrEnumerationType())
            return Interval{Zero, Magnitude};
          Interval Left = expressionInterval(Binary->getLHS(), C);
          if (Left.Min >= Zero)
            return Interval{Zero, Magnitude};
          if (Left.Max <= Zero)
            return Interval{-Magnitude, Zero};
          return Interval{-Magnitude, Magnitude};
        }
      }
      if (Binary->getOpcode() == BO_And &&
          Expr->getType()->isUnsignedIntegerOrEnumerationType()) {
        Interval Right = expressionInterval(Binary->getRHS(), C);
        if (Right.Min == Right.Max && !Right.Min.isNegative()) {
          llvm::APSInt Zero(llvm::APInt(MathBits, 0), false);
          return Interval{Zero, Right.Max};
        }
      }
      if (Binary->getOpcode() == BO_Shr &&
          Expr->getType()->isUnsignedIntegerOrEnumerationType()) {
        Interval Right = expressionInterval(Binary->getRHS(), C);
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

      Interval Left = expressionInterval(Binary->getLHS(), C);
      Interval Right = expressionInterval(Binary->getRHS(), C);
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

    if (std::optional<Interval> Known = constrainedInterval(Expr, C))
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
static ProgramStateRef arithmeticInputState(CheckerContext &C);

class SignedArithmeticChecker : public Checker<check::PreStmt<BinaryOperator>,
                                               check::PreStmt<UnaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

  static bool outside(const SizeCastChecker::Interval &Range,
                      const SizeCastChecker::Interval &Type) {
    return llvm::APSInt::compareValues(Range.Min, Type.Min) < 0 ||
           llvm::APSInt::compareValues(Range.Max, Type.Max) > 0;
  }

  void report(const Expr *Expression, CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(arithmeticInputState(C));
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
      break;
    case BO_Sub:
    case BO_SubAssign:
      Result =
          SizeCastChecker::Interval{Left.Min - Right.Max, Left.Max - Right.Min};
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

static ProgramStateRef arithmeticInputState(CheckerContext &C) {
  ExplodedNode *Predecessor = C.getPredecessor();
  if (Predecessor && !Predecessor->pred_empty())
    return Predecessor->getFirstPred()->getState();
  return C.getState();
}

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
    ProgramStateRef Violation = arithmeticInputState(C);
    if (std::optional<DefinedOrUnknownSVal> Divisor =
            C.getSVal(Operation->getRHS()).getAs<DefinedOrUnknownSVal>()) {
      Violation = Violation->assume(*Divisor, false);
      if (!Violation)
        return;
    }
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
    ProgramStateRef Violation = arithmeticInputState(C);
    if (std::optional<DefinedOrUnknownSVal> Count =
            C.getSVal(Operation->getRHS()).getAs<DefinedOrUnknownSVal>()) {
      Violation = Violation->assumeInclusiveRange(*Count, Low, High, false);
      if (!Violation)
        return;
    }
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
}
