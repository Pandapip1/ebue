// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace clang;

namespace {

class TotalityVisitor : public RecursiveASTVisitor<TotalityVisitor> {
  ASTContext &Context;
  SourceManager &SM;
  const FunctionDecl *Current = nullptr;

  enum class ProgressKind { Up, Down };

  struct Progress {
    const ValueDecl *Variable;
    ProgressKind Kind;
    const ValueDecl *Base;
    /* Non-null only for an unsigned non-unit subtraction.  Such a change
     * is strict progress only when the loop condition also proves that the
     * subtraction cannot wrap. */
    const Expr *GuardedStep = nullptr;
    bool VolatileAccess = false;
    bool RequiresNonzeroCondition = false;
    bool UnitStep = false;
    bool UnitOnly = false;
  };

  std::string file(SourceLocation Location) const {
    return SM.getFilename(SM.getExpansionLoc(Location)).str();
  }

  unsigned line(SourceLocation Location) const {
    return SM.getExpansionLineNumber(Location);
  }

  static std::string clean(StringRef Raw) {
    std::string Result;
    bool Space = false;
    for (char Character : Raw) {
      if (std::isspace(static_cast<unsigned char>(Character))) {
        Space = !Result.empty();
      } else {
        if (Space)
          Result += ' ';
        Result += Character == '\t' ? ' ' : Character;
        Space = false;
      }
    }
    return Result;
  }

  std::string text(const Stmt *Statement) const {
    SourceLocation Begin = SM.getSpellingLoc(Statement->getBeginLoc());
    SourceLocation End = SM.getSpellingLoc(Statement->getEndLoc());
    return clean(Lexer::getSourceText(
        CharSourceRange::getTokenRange(Begin, End), SM, Context.getLangOpts()));
  }

  std::string key(const FunctionDecl *Function) const {
    std::string Name = Function->getQualifiedNameAsString();
    if (Function->getFormalLinkage() == Linkage::Internal)
      return file(Function->getLocation()) + "::" + Name;
    return Name;
  }

  static const Expr *ignore(const Expr *Expression) {
    return Expression ? Expression->IgnoreParenImpCasts() : nullptr;
  }

  static const ParmVarDecl *parameter(const Expr *Expression) {
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(ignore(Expression));
    return Reference ? dyn_cast<ParmVarDecl>(Reference->getDecl()) : nullptr;
  }

  static const ValueDecl *value(const Expr *Expression) {
    Expression = ignore(Expression);
    if (const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Expression))
      return dyn_cast<ValueDecl>(Reference->getDecl());
    if (const auto *Member = dyn_cast_or_null<MemberExpr>(Expression))
      return Member->getMemberDecl();
    return nullptr;
  }

  static bool unitInteger(const Expr *Expression) {
    const auto *Literal = dyn_cast_or_null<IntegerLiteral>(ignore(Expression));
    return Literal && Literal->getValue() == 1;
  }

  static bool integerGreaterThanOne(const Expr *Expression) {
    const auto *Literal = dyn_cast_or_null<IntegerLiteral>(ignore(Expression));
    return Literal && Literal->getValue().ugt(1);
  }

  static bool positiveInteger(const Expr *Expression) {
    const auto *Literal = dyn_cast_or_null<IntegerLiteral>(ignore(Expression));
    return Literal && !Literal->getValue().isZero();
  }

  static bool positiveConstantStep(const Expr *Expression) {
    if (positiveInteger(Expression))
      return true;
    const auto *Trait =
        dyn_cast_or_null<UnaryExprOrTypeTraitExpr>(ignore(Expression));
    return Trait && (Trait->getKind() == UETT_SizeOf ||
                     Trait->getKind() == UETT_AlignOf);
  }

  static bool unsignedByteValue(const Expr *Expression) {
    Expression = ignore(Expression);
    if (const auto *Cast = dyn_cast_or_null<ExplicitCastExpr>(Expression))
      Expression = ignore(Cast->getSubExpr());
    const auto *Builtin = Expression
                              ? Expression->getType()->getAs<BuiltinType>()
                              : nullptr;
    return Builtin && (Builtin->getKind() == BuiltinType::UChar ||
                       Builtin->getKind() == BuiltinType::Char_U);
  }

  static bool strictlyPositive(const Expr *Expression) {
    Expression = ignore(Expression);
    if (positiveInteger(Expression))
      return true;
    const auto *Binary = dyn_cast_or_null<BinaryOperator>(Expression);
    if (!Binary || Binary->getOpcode() != BO_Add)
      return false;
    const Expr *Left = ignore(Binary->getLHS());
    const Expr *Right = ignore(Binary->getRHS());
    /* An arbitrary unsigned value plus one can wrap to zero.  The byte
     * source is the one useful bounded form in this tree: after integer
     * promotion its maximum plus one is still strictly positive. */
    return (unitInteger(Left) && unsignedByteValue(Right)) ||
           (unitInteger(Right) && unsignedByteValue(Left));
  }

  static bool zeroInteger(const Expr *Expression) {
    const auto *Literal = dyn_cast_or_null<IntegerLiteral>(ignore(Expression));
    return Literal && Literal->getValue().isZero();
  }

  static bool nonzeroWhen(const Expr *Condition, const ValueDecl *Parameter,
                          bool Truth) {
    Condition = ignore(Condition);
    if (!Condition)
      return false;
    if (value(Condition) == Parameter)
      return Truth;
    if (const auto *Unary = dyn_cast<UnaryOperator>(Condition)) {
      if (Unary->getOpcode() == UO_LNot)
        return nonzeroWhen(Unary->getSubExpr(), Parameter, !Truth);
    }
    const auto *Binary = dyn_cast<BinaryOperator>(Condition);
    if (!Binary)
      return false;
    if (Binary->getOpcode() == BO_LAnd) {
      if (Truth)
        return nonzeroWhen(Binary->getLHS(), Parameter, true) ||
               nonzeroWhen(Binary->getRHS(), Parameter, true);
      return nonzeroWhen(Binary->getLHS(), Parameter, false) &&
             nonzeroWhen(Binary->getRHS(), Parameter, false);
    }
    if (Binary->getOpcode() == BO_LOr) {
      if (Truth)
        return nonzeroWhen(Binary->getLHS(), Parameter, true) &&
               nonzeroWhen(Binary->getRHS(), Parameter, true);
      return nonzeroWhen(Binary->getLHS(), Parameter, false) ||
             nonzeroWhen(Binary->getRHS(), Parameter, false);
    }
    bool ParameterLeft =
        value(Binary->getLHS()) == Parameter && zeroInteger(Binary->getRHS());
    bool ParameterRight =
        value(Binary->getRHS()) == Parameter && zeroInteger(Binary->getLHS());
    if (!ParameterLeft && !ParameterRight)
      return false;
    switch (Binary->getOpcode()) {
    case BO_NE:
      return Truth;
    case BO_EQ:
      return !Truth;
    case BO_GT:
      return ParameterLeft && Truth;
    case BO_LT:
      return ParameterRight && Truth;
    case BO_LE:
      return ParameterLeft && !Truth;
    case BO_GE:
      return ParameterRight && !Truth;
    default:
      return false;
    }
  }

  bool guardedNonzero(const CallExpr *Call,
                      const ParmVarDecl *Parameter) const {
    DynTypedNode Node = DynTypedNode::create(*Call);
    while (true) {
      const Stmt *Child = Node.get<Stmt>();
      DynTypedNodeList Parents = Context.getParents(Node);
      if (Parents.size() != 1)
        return false;
      const DynTypedNode &Parent = Parents[0];
      if (const auto *If = Parent.get<IfStmt>()) {
        if (Child == If->getThen() &&
            nonzeroWhen(If->getCond(), Parameter, true))
          return true;
        if (Child == If->getElse() &&
            nonzeroWhen(If->getCond(), Parameter, false))
          return true;
      }
      if (const auto *While = Parent.get<WhileStmt>())
        if (Child == While->getBody() &&
            nonzeroWhen(While->getCond(), Parameter, true))
          return true;
      if (const auto *For = Parent.get<ForStmt>())
        if (Child == For->getBody() &&
            nonzeroWhen(For->getCond(), Parameter, true))
          return true;
      if (const auto *Do = Parent.get<DoStmt>())
        if (Child == Do->getBody() &&
            nonzeroWhen(Do->getCond(), Parameter, true))
          return true;
      if (const auto *Binary = Parent.get<BinaryOperator>()) {
        if (Child == Binary->getRHS() && Binary->getOpcode() == BO_LAnd &&
            nonzeroWhen(Binary->getLHS(), Parameter, true))
          return true;
        if (Child == Binary->getRHS() && Binary->getOpcode() == BO_LOr &&
            nonzeroWhen(Binary->getLHS(), Parameter, false))
          return true;
      }
      if (const auto *Conditional = Parent.get<ConditionalOperator>()) {
        if (Child == Conditional->getTrueExpr() &&
            nonzeroWhen(Conditional->getCond(), Parameter, true))
          return true;
        if (Child == Conditional->getFalseExpr() &&
            nonzeroWhen(Conditional->getCond(), Parameter, false))
          return true;
      }
      Node = Parent;
    }
  }

  const ParmVarDecl *strictlySmallerThanParameter(const Expr *Expression,
                                                  const CallExpr *Call) const {
    const auto *Binary = dyn_cast_or_null<BinaryOperator>(ignore(Expression));
    if (!Binary || Binary->getOpcode() != BO_Sub ||
        !unitInteger(Binary->getRHS()))
      return nullptr;
    const ParmVarDecl *Left = parameter(Binary->getLHS());
    if (!Left || !Left->getType()->isIntegerType() ||
        !guardedNonzero(Call, Left))
      return nullptr;
    return Left;
  }

  std::string callRelations(const CallExpr *Call,
                            const FunctionDecl *Callee) const {
    std::string Result;
    for (unsigned Destination = 0; Destination < Call->getNumArgs() &&
                                   Destination < Callee->getNumParams();
         ++Destination) {
      const Expr *Argument = Call->getArg(Destination);
      const ParmVarDecl *Source = parameter(Argument);
      char Relation = '=';
      if (!Source) {
        Source = strictlySmallerThanParameter(Argument, Call);
        Relation = '<';
      }
      if (!Source)
        continue;
      unsigned SourceIndex = 0;
      while (SourceIndex < Current->getNumParams() &&
             Current->getParamDecl(SourceIndex) != Source)
        ++SourceIndex;
      if (SourceIndex == Current->getNumParams())
        continue;
      if (!Result.empty())
        Result += ',';
      Result += std::to_string(Destination) + ':' +
                std::to_string(SourceIndex) + ':' + Relation;
    }
    return Result.empty() ? "-" : Result;
  }

  static Progress makeProgress(const ValueDecl *Variable, ProgressKind Kind,
                               const ValueDecl *Base, const Expr *Access,
                               const Expr *GuardedStep = nullptr,
                               bool RequiresNonzeroCondition = false,
                               bool UnitStep = false) {
    return Progress{Variable, Kind, Base, GuardedStep,
                    Access && Access->getType().isVolatileQualified(),
                    RequiresNonzeroCondition, UnitStep, false};
  }

  static bool sameRank(const Progress &Left, const Progress &Right) {
    if (Left.Variable != Right.Variable)
      return false;
    return !isa<FieldDecl>(Left.Variable) || Left.Base == Right.Base;
  }

  static bool rankAccess(const Expr *Expression, const Progress &Rank) {
    Expression = ignore(Expression);
    if (const auto *Field = dyn_cast<FieldDecl>(Rank.Variable)) {
      const auto *Member = dyn_cast_or_null<MemberExpr>(Expression);
      return Member && Member->getMemberDecl() == Field &&
             value(Member->getBase()) == Rank.Base;
    }
    return value(Expression) == Rank.Variable;
  }

  static bool basedOn(const Expr *Expression, const Progress &Rank) {
    Expression = ignore(Expression);
    if (rankAccess(Expression, Rank))
      return true;
    if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Expression))
      return basedOn(Unary->getSubExpr(), Rank);
    if (const auto *Member = dyn_cast_or_null<MemberExpr>(Expression))
      return basedOn(Member->getBase(), Rank);
    if (const auto *Subscript =
            dyn_cast_or_null<ArraySubscriptExpr>(Expression))
      return basedOn(Subscript->getBase(), Rank) ||
             basedOn(Subscript->getIdx(), Rank);
    return false;
  }

  static std::optional<Progress> progress(const Expr *Expression) {
    Expression = ignore(Expression);
    if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Expression)) {
      const ValueDecl *Variable = value(Unary->getSubExpr());
      if (!Variable)
        return std::nullopt;
      const auto *Member = dyn_cast_or_null<MemberExpr>(
          ignore(Unary->getSubExpr()));
      const ValueDecl *Base = Member ? value(Member->getBase()) : nullptr;
      if (Unary->isIncrementOp())
        return makeProgress(Variable, ProgressKind::Up, Base,
                            Unary->getSubExpr(), nullptr, false, true);
      if (Unary->isDecrementOp())
        return makeProgress(Variable, ProgressKind::Down, Base,
                            Unary->getSubExpr(), nullptr, false, true);
    }
    const auto *Binary = dyn_cast_or_null<BinaryOperator>(Expression);
    if (!Binary)
      return std::nullopt;
    const ValueDecl *Variable = value(Binary->getLHS());
    if (!Variable)
      return std::nullopt;
    const auto *Member =
        dyn_cast_or_null<MemberExpr>(ignore(Binary->getLHS()));
    const ValueDecl *Base = Member ? value(Member->getBase()) : nullptr;
    /* A step larger than one can jump over the bound and wrap.  In
     * particular, `for (unsigned i = 0; i < UINT_MAX; i += 2)` does not
     * terminate.  Unit steps are the only context-free scalar proof. */
    if (Binary->getOpcode() == BO_AddAssign &&
        (unitInteger(Binary->getRHS()) ||
         (Variable->getType()->isSignedIntegerType() &&
          positiveInteger(Binary->getRHS())) ||
         (Variable->getType()->isPointerType() &&
          strictlyPositive(Binary->getRHS()))))
      return makeProgress(Variable, ProgressKind::Up, Base, Binary->getLHS(),
                          nullptr, false, unitInteger(Binary->getRHS()));
    if (Binary->getOpcode() == BO_SubAssign &&
        (unitInteger(Binary->getRHS()) ||
         (Variable->getType()->isSignedIntegerType() &&
          positiveInteger(Binary->getRHS())) ||
         (Variable->getType()->isUnsignedIntegerType() &&
          positiveConstantStep(Binary->getRHS()))))
      return makeProgress(
          Variable, ProgressKind::Down, Base, Binary->getLHS(),
          Variable->getType()->isUnsignedIntegerType() &&
                  !unitInteger(Binary->getRHS())
              ? Binary->getRHS()
              : nullptr,
          false, unitInteger(Binary->getRHS()));
    /* For an unsigned value tested for nonzero, division by a constant
     * greater than one is a strict descent to zero.  This is the common
     * integer-to-text digit loop (`while (u) u /= 10`); unlike a non-unit
     * additive step it cannot skip a bound and wrap back around. */
    if (Binary->getOpcode() == BO_DivAssign &&
        Variable->getType()->isIntegerType() &&
        integerGreaterThanOne(Binary->getRHS()))
      return makeProgress(Variable, ProgressKind::Down, Base,
                          Binary->getLHS(), nullptr, true);
    if (Binary->getOpcode() == BO_ShrAssign &&
        Variable->getType()->isUnsignedIntegerType() &&
        positiveInteger(Binary->getRHS()))
      return makeProgress(Variable, ProgressKind::Down, Base,
                          Binary->getLHS(), nullptr, true);
    if (!Binary->isAssignmentOp())
      return std::nullopt;
    const Expr *Right = ignore(Binary->getRHS());
    if (const auto *Operation = dyn_cast<BinaryOperator>(Right)) {
      if (value(Operation->getLHS()) == Variable) {
        if (Operation->getOpcode() == BO_Add &&
            (unitInteger(Operation->getRHS()) ||
             (Variable->getType()->isSignedIntegerType() &&
              positiveInteger(Operation->getRHS())) ||
             (Variable->getType()->isPointerType() &&
              strictlyPositive(Operation->getRHS()))))
          return makeProgress(Variable, ProgressKind::Up, Base,
                              Binary->getLHS(), nullptr, false,
                              unitInteger(Operation->getRHS()));
        if (Operation->getOpcode() == BO_Sub &&
            (unitInteger(Operation->getRHS()) ||
             (Variable->getType()->isSignedIntegerType() &&
              positiveInteger(Operation->getRHS())) ||
             (Variable->getType()->isUnsignedIntegerType() &&
              positiveConstantStep(Operation->getRHS()))))
          return makeProgress(
              Variable, ProgressKind::Down, Base, Binary->getLHS(),
              Variable->getType()->isUnsignedIntegerType() &&
                      !unitInteger(Operation->getRHS())
                      ? Operation->getRHS()
                      : nullptr,
              false, unitInteger(Operation->getRHS()));
        if (Operation->getOpcode() == BO_Div &&
            Variable->getType()->isIntegerType() &&
            integerGreaterThanOne(Operation->getRHS()))
          return makeProgress(Variable, ProgressKind::Down, Base,
                              Binary->getLHS(), nullptr, true);
        if (Operation->getOpcode() == BO_Shr &&
            Variable->getType()->isUnsignedIntegerType() &&
            positiveInteger(Operation->getRHS()))
          return makeProgress(Variable, ProgressKind::Down, Base,
                              Binary->getLHS(), nullptr, true);
      }
    }
    /* `node = node->next` is progress only when the structure is acyclic.
     * C's type system does not carry that invariant, so recognizing the
     * assignment syntactically would "prove" a circular list. */
    return std::nullopt;
  }

  enum FlowOutcome : unsigned {
    FallWithoutProgress = 1,
    FallWithProgress = 2,
    BackWithoutProgress = 4,
    BackWithProgress = 8,
    ExitsLoop = 16,
  };

  struct Flow {
    unsigned Outcomes;
    bool Invalid;
  };

  enum class Mutation { None, Good, Bad };

  static Mutation mergeMutation(Mutation Left, Mutation Right) {
    if (Left == Mutation::Bad || Right == Mutation::Bad)
      return Mutation::Bad;
    if (Left == Mutation::Good || Right == Mutation::Good)
      return Mutation::Good;
    return Mutation::None;
  }

  static Mutation mutation(const Stmt *Statement, const Progress &Expected) {
    if (!Statement)
      return Mutation::None;
    if (const auto *Expression = dyn_cast<Expr>(Statement)) {
      if (std::optional<Progress> Change = progress(Expression)) {
        if (sameRank(*Change, Expected)) {
          if (Change->Kind != Expected.Kind)
            return Mutation::Bad;
          /* A condition guarding one unsigned chunk size says nothing about
           * a different chunk on another path.  Requiring the very same AST
           * update keeps the guarded proof single-step.  A proof which
           * specifically requires a unit step must likewise not be
           * satisfied by a wider signed update on another path. */
          if ((Change->GuardedStep || Expected.GuardedStep) &&
              Change->GuardedStep != Expected.GuardedStep)
            return Mutation::Bad;
          if (Change->RequiresNonzeroCondition !=
              Expected.RequiresNonzeroCondition)
            return Mutation::Bad;
          if (Expected.UnitOnly && !Change->UnitStep)
            return Mutation::Bad;
          return Mutation::Good;
        }
      }
      const Expr *Plain = ignore(Expression);
      if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Plain)) {
        if ((Unary->isIncrementDecrementOp() ||
             Unary->getOpcode() == UO_AddrOf) &&
            rankAccess(Unary->getSubExpr(), Expected))
          return Mutation::Bad;
      }
      if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Plain))
        if (Binary->isAssignmentOp() &&
            rankAccess(Binary->getLHS(), Expected))
          return Mutation::Bad;
    }
    Mutation Result = Mutation::None;
    for (const Stmt *Child : Statement->children())
      Result = mergeMutation(Result, mutation(Child, Expected));
    return Result;
  }

  static Flow sequence(Flow First, Flow Second) {
    if (First.Invalid || Second.Invalid)
      return {0, true};
    unsigned Result =
        First.Outcomes & (BackWithoutProgress | BackWithProgress | ExitsLoop);
    if (First.Outcomes & FallWithoutProgress)
      Result |= Second.Outcomes;
    if (First.Outcomes & FallWithProgress) {
      if (Second.Outcomes & (FallWithoutProgress | FallWithProgress))
        Result |= FallWithProgress;
      if (Second.Outcomes & (BackWithoutProgress | BackWithProgress))
        Result |= BackWithProgress;
      if (Second.Outcomes & ExitsLoop)
        Result |= ExitsLoop;
    }
    return {Result, false};
  }

  static Flow flow(const Stmt *Statement, const Progress &Expected) {
    if (!Statement)
      return {FallWithoutProgress, false};
    if (const auto *Expression = dyn_cast<Expr>(Statement)) {
      const Expr *Plain = ignore(Expression);
      if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Plain)) {
        if (Binary->getOpcode() == BO_Comma)
          return sequence(flow(Binary->getLHS(), Expected),
                          flow(Binary->getRHS(), Expected));
        if (Binary->getOpcode() == BO_LAnd || Binary->getOpcode() == BO_LOr) {
          Flow Left = flow(Binary->getLHS(), Expected);
          Flow WithRight = sequence(Left, flow(Binary->getRHS(), Expected));
          return {Left.Outcomes | WithRight.Outcomes,
                  Left.Invalid || WithRight.Invalid};
        }
      }
      if (const auto *Conditional =
              dyn_cast_or_null<ConditionalOperator>(Plain)) {
        Flow Condition = flow(Conditional->getCond(), Expected);
        Flow True = flow(Conditional->getTrueExpr(), Expected);
        Flow False = flow(Conditional->getFalseExpr(), Expected);
        Flow Arms{True.Outcomes | False.Outcomes,
                  True.Invalid || False.Invalid};
        return sequence(Condition, Arms);
      }
    }
    if (const auto *Compound = dyn_cast<CompoundStmt>(Statement)) {
      Flow Result{FallWithoutProgress, false};
      for (const Stmt *Child : Compound->body())
        Result = sequence(Result, flow(Child, Expected));
      return Result;
    }
    if (const auto *If = dyn_cast<IfStmt>(Statement)) {
      Flow Condition = flow(If->getCond(), Expected);
      Flow Then = flow(If->getThen(), Expected);
      Flow Else = flow(If->getElse(), Expected);
      Flow Branches{Then.Outcomes | Else.Outcomes,
                    Then.Invalid || Else.Invalid};
      return sequence(Condition, Branches);
    }
    if (const auto *Label = dyn_cast<LabelStmt>(Statement))
      return flow(Label->getSubStmt(), Expected);
    if (isa<ContinueStmt>(Statement))
      return {BackWithoutProgress, false};
    if (isa<BreakStmt>(Statement) || isa<ReturnStmt>(Statement))
      return {ExitsLoop, false};
    if (isa<ForStmt>(Statement) || isa<WhileStmt>(Statement) ||
        isa<DoStmt>(Statement)) {
      /* Nested loops receive their own independent totality obligation.
       * For the enclosing rank they are an ordinary falling-through
       * statement when they do not mutate that rank; rejecting them
       * outright made a proved inner scan poison an otherwise elementary
       * outer index loop. */
      return mutation(Statement, Expected) == Mutation::None
                 ? Flow{FallWithoutProgress, false}
                 : Flow{0, true};
    }
    if (isa<GotoStmt>(Statement) || isa<SwitchStmt>(Statement))
      return {0, true};
    switch (mutation(Statement, Expected)) {
    case Mutation::None:
      return {FallWithoutProgress, false};
    case Mutation::Good:
      return {FallWithProgress, false};
    case Mutation::Bad:
      return {0, true};
    }
    llvm_unreachable("all mutations handled");
  }

  static bool bodyGuaranteesProgress(const Stmt *Body,
                                     const Progress &Expected) {
    Flow Result = flow(Body, Expected);
    return !Result.Invalid && Result.Outcomes != 0 &&
           !(Result.Outcomes & (FallWithoutProgress | BackWithoutProgress));
  }

  static void collectProgress(const Stmt *Statement,
                              std::vector<Progress> &Result) {
    if (!Statement)
      return;
    if (const auto *Expression = dyn_cast<Expr>(Statement))
      if (std::optional<Progress> Change = progress(Expression)) {
        bool Seen = false;
        for (const Progress &Existing : Result)
          Seen |= sameRank(Existing, *Change) &&
                  Existing.Kind == Change->Kind;
        if (!Seen)
          Result.push_back(*Change);
        return;
      }
    for (const Stmt *Child : Statement->children())
      collectProgress(Child, Result);
  }

  static bool writesVariable(const Stmt *Statement, const ValueDecl *Variable) {
    if (!Statement)
      return false;
    if (const auto *Expression = dyn_cast<Expr>(Statement)) {
      const Expr *Plain = ignore(Expression);
      if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Plain))
        if ((Unary->isIncrementDecrementOp() ||
             Unary->getOpcode() == UO_AddrOf) &&
            value(Unary->getSubExpr()) == Variable)
          return true;
      if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Plain))
        if (Binary->isAssignmentOp() && value(Binary->getLHS()) == Variable)
          return true;
    }
    for (const Stmt *Child : Statement->children())
      if (writesVariable(Child, Variable))
        return true;
    return false;
  }

  static bool containsCall(const Stmt *Statement) {
    if (!Statement)
      return false;
    if (isa<CallExpr>(Statement))
      return true;
    for (const Stmt *Child : Statement->children())
      if (containsCall(Child))
        return true;
    return false;
  }

  enum CallFlowOutcome : unsigned {
    FallWithoutCall = 1,
    FallWithCall = 2,
    BackWithoutCall = 4,
    BackWithCall = 8,
    ExitWithoutCall = 16,
    ExitWithCall = 32,
  };

  struct CallFlow {
    unsigned Outcomes;
    bool Invalid;
  };

  static unsigned afterCall(unsigned Outcomes) {
    unsigned Result = 0;
    if (Outcomes & (FallWithoutCall | FallWithCall))
      Result |= FallWithCall;
    if (Outcomes & (BackWithoutCall | BackWithCall))
      Result |= BackWithCall;
    if (Outcomes & (ExitWithoutCall | ExitWithCall))
      Result |= ExitWithCall;
    return Result;
  }

  static CallFlow callSequence(CallFlow First, CallFlow Second) {
    if (First.Invalid || Second.Invalid)
      return {0, true};
    unsigned Result =
        First.Outcomes &
        (BackWithoutCall | BackWithCall | ExitWithoutCall | ExitWithCall);
    if (First.Outcomes & FallWithoutCall)
      Result |= Second.Outcomes;
    if (First.Outcomes & FallWithCall)
      Result |= afterCall(Second.Outcomes);
    return {Result, false};
  }

  static CallFlow callFlow(const Stmt *Statement) {
    if (!Statement)
      return {FallWithoutCall, false};
    if (const auto *Compound = dyn_cast<CompoundStmt>(Statement)) {
      CallFlow Result{FallWithoutCall, false};
      for (const Stmt *Child : Compound->body())
        Result = callSequence(Result, callFlow(Child));
      return Result;
    }
    if (const auto *If = dyn_cast<IfStmt>(Statement)) {
      CallFlow Prefix = callSequence(callFlow(If->getInit()),
                                     callFlow(If->getConditionVariableDeclStmt()));
      Prefix = callSequence(Prefix, callFlow(If->getCond()));
      CallFlow Then = callFlow(If->getThen());
      CallFlow Else = callFlow(If->getElse());
      CallFlow Branches{Then.Outcomes | Else.Outcomes,
                        Then.Invalid || Else.Invalid};
      return callSequence(Prefix, Branches);
    }
    if (const auto *Label = dyn_cast<LabelStmt>(Statement))
      return callFlow(Label->getSubStmt());
    if (isa<ContinueStmt>(Statement))
      return {BackWithoutCall, false};
    if (isa<BreakStmt>(Statement))
      return {ExitWithoutCall, false};
    if (const auto *Return = dyn_cast<ReturnStmt>(Statement))
      return callSequence(callFlow(Return->getRetValue()),
                          {ExitWithoutCall, false});
    if (isa<ForStmt>(Statement) || isa<WhileStmt>(Statement) ||
        isa<DoStmt>(Statement)) {
      /* A nested loop has its own control targets.  If it contains a call,
       * conservatively assume that call can return and the nested loop can
       * then fall through to this loop's backedge. */
      return containsCall(Statement)
                 ? CallFlow{FallWithoutCall | FallWithCall, false}
                 : CallFlow{FallWithoutCall, false};
    }
    if (isa<GotoStmt>(Statement) || isa<SwitchStmt>(Statement))
      return {0, true};
    return containsCall(Statement)
               ? CallFlow{FallWithoutCall | FallWithCall, false}
               : CallFlow{FallWithoutCall, false};
  }

  static bool callCanReachBackedge(const Stmt *Body) {
    CallFlow Result = callFlow(Body);
    return Result.Invalid ||
           (Result.Outcomes & (FallWithCall | BackWithCall));
  }

  static bool addressTaken(const Stmt *Statement,
                           const ValueDecl *Variable) {
    if (!Statement)
      return false;
    if (const auto *Unary = dyn_cast<UnaryOperator>(Statement))
      if (Unary->getOpcode() == UO_AddrOf &&
          value(Unary->getSubExpr()) == Variable)
        return true;
    for (const Stmt *Child : Statement->children())
      if (addressTaken(Child, Variable))
        return true;
    return false;
  }

  static bool memberOf(const Expr *Expression, const ValueDecl *Field) {
    const auto *Member = dyn_cast_or_null<MemberExpr>(ignore(Expression));
    return Member && Member->getMemberDecl() == Field;
  }

  // Mirrors writesVariable() exactly, but matches a struct/union FIELD
  // (any `Base->Field` or `Base.Field`, for ANY base expression) instead of
  // a single ValueDecl.  Matching on field identity rather than chasing the
  // specific base expression is deliberately coarser than the alias
  // tracking below: it cannot tell two same-named fields on two unrelated
  // objects apart, so a write to an unconnected object's same-named field
  // makes this return a conservative true where a sharper analysis would
  // not have to.  That coarseness only ever costs precision (a real-but-
  // undetected stable bound stays unproved), never soundness -- it can
  // never miss an actual write to the field this checker is about to rely
  // on as unchanging.
  static bool writesMember(const Stmt *Statement, const ValueDecl *Field) {
    if (!Statement)
      return false;
    if (const auto *Expression = dyn_cast<Expr>(Statement)) {
      const Expr *Plain = ignore(Expression);
      if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Plain))
        if ((Unary->isIncrementDecrementOp() || Unary->getOpcode() == UO_AddrOf) &&
            memberOf(Unary->getSubExpr(), Field))
          return true;
      if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Plain))
        if (Binary->isAssignmentOp() && memberOf(Binary->getLHS(), Field))
          return true;
    }
    for (const Stmt *Child : Statement->children())
      if (writesMember(Child, Field))
        return true;
    return false;
  }

  // A loop bound of the shape `base->field` or `base.field`, where `base`
  // is a plain parameter or local variable, is stable across the loop when
  // three things are all true: the base itself is never reseated or
  // handed to something that could reseat or overwrite it out from under
  // this read (the same writesVariable/aliasedWrite tests the plain-
  // variable case above already requires of `base`); nothing in the
  // tested region writes `*base` wholesale or passes `base` on to a call
  // that could reach back through it (writesThroughAlias, applied to
  // `base` directly -- it already recognizes exactly that shape for a
  // pointer-typed alias, which is exactly what an arrow base is; a dot
  // base is a struct, not a pointer, so this test is vacuous for it,
  // which is correct, not a gap: aliasedWrite already covers a struct
  // local whose OWN address escaped); and no expression anywhere in the
  // tested region assigns through a member with the same field identity
  // (writesMember, coarse but sound as documented on it above).
  //
  // A member reached through anything other than a single plain base
  // variable (another member expression, a call result, a subscript) is
  // deliberately left unrecognized here and falls through to this
  // function's existing "false" -- there is no local var to run the
  // escape checks against, so nothing about it can be shown stable this
  // way, and this lemma does not try.
  bool memberStable(const MemberExpr *Member, const Stmt *Body,
                    const Expr *Increment) const {
    const ValueDecl *Field = Member->getMemberDecl();
    const ValueDecl *BaseDecl = value(Member->getBase());
    if (!Field || !BaseDecl || Member->getType().isVolatileQualified())
      return false;
    const auto *BaseVar = dyn_cast<VarDecl>(BaseDecl);
    if (!BaseVar || BaseVar->getType().isVolatileQualified() || !Current)
      return false;
    /* A call need not receive BaseVar to mutate the same object: a parameter
     * may alias globally reachable storage, and a local object's address may
     * have escaped before the loop.  Without an interprocedural no-write
     * summary, any call invalidates a member bound. */
    if (containsCall(Body) || containsCall(Increment))
      return false;
    if (writesVariable(Body, BaseVar) || writesVariable(Increment, BaseVar) ||
        aliasedWrite(BaseVar, Body))
      return false;
    if (Member->isArrow() &&
        (writesThroughAlias(Body, BaseVar) || writesThroughAlias(Increment, BaseVar)))
      return false;
    return !writesMember(Body, Field) && !writesMember(Increment, Field);
  }

  static bool addressOf(const Expr *Expression, const ValueDecl *Variable) {
    const auto *Unary = dyn_cast_or_null<UnaryOperator>(ignore(Expression));
    return Unary && Unary->getOpcode() == UO_AddrOf &&
           value(Unary->getSubExpr()) == Variable;
  }

  static void collectAliases(const Stmt *Statement, const ValueDecl *Variable,
                             std::vector<const ValueDecl *> &Aliases) {
    if (!Statement)
      return;
    if (const auto *Declaration = dyn_cast<DeclStmt>(Statement))
      for (const Decl *Item : Declaration->decls())
        if (const auto *Alias = dyn_cast<VarDecl>(Item))
          if (addressOf(Alias->getInit(), Variable))
            Aliases.push_back(Alias);
    if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(
            ignore(dyn_cast_or_null<Expr>(Statement))))
      if (Binary->isAssignmentOp() && addressOf(Binary->getRHS(), Variable))
        if (const ValueDecl *Alias = value(Binary->getLHS()))
          Aliases.push_back(Alias);
    for (const Stmt *Child : Statement->children())
      collectAliases(Child, Variable, Aliases);
  }

  static bool writesThroughAlias(const Stmt *Statement,
                                 const ValueDecl *Alias,
                                 bool CallsAreWrites = true) {
    if (!Statement)
      return false;
    if (const auto *Expression = dyn_cast<Expr>(Statement)) {
      const Expr *Plain = ignore(Expression);
      if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Plain)) {
        const auto *Target =
            dyn_cast_or_null<UnaryOperator>(ignore(Unary->getSubExpr()));
        if (Unary->isIncrementDecrementOp() && Target &&
            Target->getOpcode() == UO_Deref &&
            value(Target->getSubExpr()) == Alias)
          return true;
      }
      if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Plain)) {
        const auto *Target =
            dyn_cast_or_null<UnaryOperator>(ignore(Binary->getLHS()));
        if (Binary->isAssignmentOp() && Target &&
            Target->getOpcode() == UO_Deref &&
            value(Target->getSubExpr()) == Alias)
          return true;
      }
      if (CallsAreWrites) {
        if (const auto *Call = dyn_cast_or_null<CallExpr>(Plain))
          for (const Expr *Argument : Call->arguments())
            if (value(Argument) == Alias)
              return true;
      }
    }
    for (const Stmt *Child : Statement->children())
      if (writesThroughAlias(Child, Alias, CallsAreWrites))
        return true;
    return false;
  }

  bool aliasedWrite(const ValueDecl *Variable, const Stmt *Body) const {
    std::vector<const ValueDecl *> Aliases;
    collectAliases(Current->getBody(), Variable, Aliases);
    for (const ValueDecl *Alias : Aliases)
      if (writesThroughAlias(Body, Alias))
        return true;
    return false;
  }

  bool validRankVariable(const Progress &Change, const Stmt *Body,
                         const Expr *Increment = nullptr) const {
    if (const auto *Field = dyn_cast<FieldDecl>(Change.Variable)) {
      const auto *Base = dyn_cast_or_null<VarDecl>(Change.Base);
      return !Change.VolatileAccess && !Field->getType().isVolatileQualified() &&
             Base && !Base->getType().isVolatileQualified() && Current &&
             !callCanReachBackedge(Body) &&
             !containsCall(Increment) &&
             !writesVariable(Body, Base) && !aliasedWrite(Base, Body) &&
             (!Base->getType()->isPointerType() ||
              !writesThroughAlias(Body, Base, false));
    }
    const auto *Variable = dyn_cast<VarDecl>(Change.Variable);
    if (!Variable || Variable->getType().isVolatileQualified() || !Current)
      return false;
    if (!(isa<ParmVarDecl>(Variable) || Variable->hasLocalStorage())) {
      if (Variable->getFormalLinkage() != Linkage::Internal ||
          containsCall(Body) || containsCall(Increment))
        return false;
    } else if ((containsCall(Body) || containsCall(Increment)) &&
               addressTaken(Current->getBody(), Variable)) {
      return false;
    }
    return !aliasedWrite(Variable, Body);
  }

  bool stableBound(const Expr *Expression, const Stmt *Body,
                   const Expr *Increment) const {
    if (!Expression)
      return false;
    Expr::EvalResult Constant;
    if (Expression->EvaluateAsInt(Constant, Context))
      return true;
    const Expr *Plain = ignore(Expression);
    if (const auto *Cast = dyn_cast_or_null<CastExpr>(Plain))
      return stableBound(Cast->getSubExpr(), Body, Increment);
    if (const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Plain)) {
      const auto *Variable = dyn_cast<VarDecl>(Reference->getDecl());
      if (!Variable || Variable->getType().isVolatileQualified() || !Current)
        return false;
      if (!(isa<ParmVarDecl>(Variable) || Variable->hasLocalStorage())) {
        if (Variable->getFormalLinkage() != Linkage::Internal ||
            containsCall(Body) || containsCall(Increment))
          return false;
      } else if ((containsCall(Body) || containsCall(Increment)) &&
                 addressTaken(Current->getBody(), Variable)) {
        return false;
      }
      return !aliasedWrite(Variable, Body) &&
             !writesVariable(Body, Variable) &&
             !writesVariable(Increment, Variable);
    }
    if (const auto *Member = dyn_cast_or_null<MemberExpr>(Plain))
      return memberStable(Member, Body, Increment);
    if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Plain)) {
      /* A second pointer can alias the pointee without being derived from
       * this spelling.  Local syntactic escape tracking is insufficient to
       * prove `*p` stable, and volatile pointees make the issue explicit. */
      if (Unary->getOpcode() == UO_Deref)
        return false;
      if (Unary->isIncrementDecrementOp() || Unary->getOpcode() == UO_AddrOf)
        return false;
      return stableBound(Unary->getSubExpr(), Body, Increment);
    }
    if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Plain)) {
      if (Binary->isAssignmentOp() || Binary->isCommaOp() ||
          Binary->isLogicalOp())
        return false;
      return stableBound(Binary->getLHS(), Body, Increment) &&
             stableBound(Binary->getRHS(), Body, Increment);
    }
    return false;
  }

  bool maximumFitsRank(const llvm::APSInt &BoundMaximum,
                       const ValueDecl *Variable,
                       bool AllowEqual) const {
    QualType VariableType = Variable->getType();
    if (!VariableType->isIntegerType())
      return false;
    llvm::APSInt Maximum = llvm::APSInt::getMaxValue(
        Context.getIntWidth(VariableType),
        VariableType->isUnsignedIntegerOrEnumerationType());
    int Comparison = llvm::APSInt::compareValues(BoundMaximum, Maximum);
    return AllowEqual ? Comparison <= 0 : Comparison < 0;
  }

  bool boundFitsRank(const Expr *Bound, const ValueDecl *Variable,
                     bool AllowEqual) const {
    Expr::EvalResult Constant;
    if (Bound->EvaluateAsInt(Constant, Context))
      return maximumFitsRank(Constant.Val.getInt(), Variable, AllowEqual);
    const Expr *Plain = ignore(Bound);
    if (!Plain || !Plain->getType()->isIntegerType())
      return false;
    QualType BoundType = Plain->getType();
    llvm::APSInt BoundMaximum = llvm::APSInt::getMaxValue(
        Context.getIntWidth(BoundType),
        BoundType->isUnsignedIntegerOrEnumerationType());
    return maximumFitsRank(BoundMaximum, Variable, AllowEqual);
  }

  bool belowTypeMaximum(const Expr *Bound, const ValueDecl *Variable) const {
    return boundFitsRank(Bound, Variable, false);
  }

  bool atMostTypeMaximum(const Expr *Bound,
                         const ValueDecl *Variable) const {
    return boundFitsRank(Bound, Variable, true);
  }

  bool aboveTypeMinimum(const Expr *Bound, const ValueDecl *Variable) const {
    QualType VariableType = Variable->getType();
    if (!VariableType->isIntegerType())
      return false;
    llvm::APSInt Minimum = llvm::APSInt::getMinValue(
        Context.getIntWidth(VariableType),
        VariableType->isUnsignedIntegerOrEnumerationType());
    Expr::EvalResult Constant;
    if (Bound->EvaluateAsInt(Constant, Context))
      return llvm::APSInt::compareValues(Constant.Val.getInt(), Minimum) > 0;
    const Expr *Plain = ignore(Bound);
    if (!Plain || !Plain->getType()->isIntegerType())
      return false;
    QualType BoundType = Plain->getType();
    llvm::APSInt BoundMinimum = llvm::APSInt::getMinValue(
        Context.getIntWidth(BoundType),
        BoundType->isUnsignedIntegerOrEnumerationType());
    return llvm::APSInt::compareValues(BoundMinimum, Minimum) > 0;
  }

  bool affineOn(const Expr *Expression, const Progress &Rank,
                const Stmt *Body, const Expr *Increment) const {
    Expression = ignore(Expression);
    if (rankAccess(Expression, Rank))
      return true;
    const auto *Binary = dyn_cast_or_null<BinaryOperator>(Expression);
    if (!Binary || Binary->getOpcode() != BO_Add)
      return false;
    if (rankAccess(Binary->getLHS(), Rank))
      return stableBound(Binary->getRHS(), Body, Increment);
    return rankAccess(Binary->getRHS(), Rank) &&
           stableBound(Binary->getLHS(), Body, Increment);
  }

  bool guardsUnsignedStep(BinaryOperatorKind Opcode, const Expr *Bound,
                          const Progress &Change) const {
    if (!Change.GuardedStep)
      return true;
    /* For `n -= K`, an unsigned backedge is safe exactly when the taken
     * condition establishes n >= K.  Keep this deliberately narrow: both
     * K and the lower bound must be integer constant expressions, and use
     * only the direct inclusive spelling.  This covers chunked countdowns
     * such as `while (n >= sizeof word) n -= sizeof word` without accepting
     * the wrapping `while (n) n -= 2`. */
    if (Opcode != BO_GE)
      return false;
    Expr::EvalResult BoundValue;
    Expr::EvalResult StepValue;
    if (!Bound->EvaluateAsInt(BoundValue, Context) ||
        !Change.GuardedStep->EvaluateAsInt(StepValue, Context))
      return false;
    return llvm::APSInt::compareValues(BoundValue.Val.getInt(),
                                       StepValue.Val.getInt()) >= 0;
  }

  bool strictComparison(const Expr *Condition, const Progress &Change,
                        const Stmt *Body, const Expr *Increment) const {
    if (Change.RequiresNonzeroCondition)
      return false;
    Condition = ignore(Condition);
    if (const auto *Logical = dyn_cast_or_null<BinaryOperator>(Condition)) {
      if (Logical->getOpcode() == BO_LAnd)
        return strictComparison(Logical->getLHS(), Change, Body, Increment) ||
               strictComparison(Logical->getRHS(), Change, Body, Increment);
      /* For A || B, either arm can keep the loop running.  Both therefore
       * need the same rank; accepting one arm makes
       * `i < n || keep_running` a false proof. */
      if (Logical->getOpcode() == BO_LOr)
        return strictComparison(Logical->getLHS(), Change, Body, Increment) &&
               strictComparison(Logical->getRHS(), Change, Body, Increment);
      bool Left = affineOn(Logical->getLHS(), Change, Body, Increment);
      bool Right = affineOn(Logical->getRHS(), Change, Body, Increment);
      if (Change.Kind == ProgressKind::Up)
        return (Left &&
                stableBound(Logical->getRHS(), Body, Increment) &&
                ((Logical->getOpcode() == BO_LT &&
                  (!Change.Variable->getType()->isUnsignedIntegerType() ||
                   atMostTypeMaximum(Logical->getRHS(), Change.Variable))) ||
                 (Logical->getOpcode() == BO_LE &&
                  (!Change.Variable->getType()->isUnsignedIntegerType() ||
                   belowTypeMaximum(Logical->getRHS(), Change.Variable))))) ||
               (Right &&
                stableBound(Logical->getLHS(), Body, Increment) &&
                ((Logical->getOpcode() == BO_GT &&
                  (!Change.Variable->getType()->isUnsignedIntegerType() ||
                   atMostTypeMaximum(Logical->getLHS(), Change.Variable))) ||
                 (Logical->getOpcode() == BO_GE &&
                  (!Change.Variable->getType()->isUnsignedIntegerType() ||
                   belowTypeMaximum(Logical->getLHS(), Change.Variable)))));
      if (Change.Kind == ProgressKind::Down)
        return (Left &&
                stableBound(Logical->getRHS(), Body, Increment) &&
                guardsUnsignedStep(Logical->getOpcode(), Logical->getRHS(),
                                   Change) &&
                (Logical->getOpcode() == BO_GT ||
                 (Logical->getOpcode() == BO_GE &&
                  (Change.Variable->getType()->isSignedIntegerType() ||
                   aboveTypeMinimum(Logical->getRHS(), Change.Variable))))) ||
               (Right &&
                stableBound(Logical->getLHS(), Body, Increment) &&
                guardsUnsignedStep(
                    BinaryOperator::reverseComparisonOp(
                        Logical->getOpcode()),
                    Logical->getLHS(), Change) &&
                (Logical->getOpcode() == BO_LT ||
                 (Logical->getOpcode() == BO_LE &&
                  (Change.Variable->getType()->isSignedIntegerType() ||
                   aboveTypeMinimum(Logical->getLHS(), Change.Variable)))));
    }
    return false;
  }

  static bool sentinelRead(const Stmt *Statement, const Progress &Rank) {
    if (!Statement)
      return false;
    if (const auto *Unary = dyn_cast<UnaryOperator>(Statement)) {
      if (Unary->getOpcode() == UO_Deref &&
          basedOn(Unary->getSubExpr(), Rank))
        return true;
    }
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Statement)) {
      if (basedOn(Subscript->getBase(), Rank) ||
          basedOn(Subscript->getIdx(), Rank))
        return true;
    }
    if (const auto *Member = dyn_cast<MemberExpr>(Statement)) {
      if (Member->isArrow() && basedOn(Member->getBase(), Rank))
        return true;
    }
    for (const Stmt *Child : Statement->children())
      if (sentinelRead(Child, Rank))
        return true;
    return false;
  }

  bool sentinelDomainCannotCycle(const Progress &Rank) const {
    QualType Type = Rank.Variable->getType();
    if (Type->isPointerType() || Type->isSignedIntegerType())
      return true;
    if (!Type->isUnsignedIntegerType())
      return false;
    /* A dereference does not itself keep an unsigned index from wrapping.
     * Finite-object reasoning supplies a rank only when the index domain is
     * at least as wide as size_t: then every representable object runs out
     * of valid element offsets before the induction value can cycle.  A
     * narrower index needs an independently proved strict scalar bound;
     * strictComparison() is tried before this sentinel fallback. */
    llvm::APSInt ObjectExtentMaximum = llvm::APSInt::getMaxValue(
        Context.getIntWidth(Context.getSizeType()), true);
    return maximumFitsRank(ObjectExtentMaximum, Rank.Variable, true);
  }

  static bool rankNonzeroWhen(const Expr *Condition, const Progress &Rank,
                              bool Truth) {
    Condition = ignore(Condition);
    if (!Condition)
      return false;
    if (rankAccess(Condition, Rank))
      return Truth;
    if (const auto *Unary = dyn_cast<UnaryOperator>(Condition)) {
      if (Unary->getOpcode() == UO_LNot)
        return rankNonzeroWhen(Unary->getSubExpr(), Rank, !Truth);
    }
    const auto *Binary = dyn_cast<BinaryOperator>(Condition);
    if (!Binary)
      return false;
    if (Binary->getOpcode() == BO_LAnd) {
      if (Truth)
        return rankNonzeroWhen(Binary->getLHS(), Rank, true) ||
               rankNonzeroWhen(Binary->getRHS(), Rank, true);
      return rankNonzeroWhen(Binary->getLHS(), Rank, false) &&
             rankNonzeroWhen(Binary->getRHS(), Rank, false);
    }
    if (Binary->getOpcode() == BO_LOr) {
      if (Truth)
        return rankNonzeroWhen(Binary->getLHS(), Rank, true) &&
               rankNonzeroWhen(Binary->getRHS(), Rank, true);
      return rankNonzeroWhen(Binary->getLHS(), Rank, false) ||
             rankNonzeroWhen(Binary->getRHS(), Rank, false);
    }
    bool RankLeft =
        rankAccess(Binary->getLHS(), Rank) && zeroInteger(Binary->getRHS());
    bool RankRight =
        rankAccess(Binary->getRHS(), Rank) && zeroInteger(Binary->getLHS());
    if (!RankLeft && !RankRight)
      return false;
    switch (Binary->getOpcode()) {
    case BO_NE:
      return Truth;
    case BO_EQ:
      return !Truth;
    case BO_GT:
      return RankLeft && Truth;
    case BO_LT:
      return RankRight && Truth;
    case BO_LE:
      return RankLeft && !Truth;
    case BO_GE:
      return RankRight && !Truth;
    default:
      return false;
    }
  }

  static bool exitsBeforeBackedge(const Stmt *Statement) {
    CallFlow Result = callFlow(Statement);
    return !Result.Invalid && Result.Outcomes != 0 &&
           !(Result.Outcomes &
             (FallWithoutCall | FallWithCall |
              BackWithoutCall | BackWithCall));
  }

  static bool bodyHasDominatingNonzeroGuard(const Stmt *Body,
                                            const Progress &Rank) {
    const auto *Compound = dyn_cast_or_null<CompoundStmt>(Body);
    if (!Compound)
      return false;
    for (const Stmt *Child : Compound->body()) {
      if (const auto *Guard = dyn_cast<IfStmt>(Child)) {
        if (Guard->getInit() || Guard->getConditionVariableDeclStmt() ||
            containsCall(Guard->getCond()) ||
            mutation(Guard->getCond(), Rank) != Mutation::None)
          return false;
        if (exitsBeforeBackedge(Guard->getThen()) &&
            rankNonzeroWhen(Guard->getCond(), Rank, false))
          return true;
        return Guard->getElse() && exitsBeforeBackedge(Guard->getElse()) &&
               rankNonzeroWhen(Guard->getCond(), Rank, true);
      }
      /* Only straight-line, call-free, rank-preserving declarations or
       * expressions may precede the guard.  In particular a decrement,
       * conditional bypass, continue, or goto prevents domination. */
      Flow Prefix = flow(Child, Rank);
      if (containsCall(Child) || Prefix.Invalid ||
          Prefix.Outcomes != FallWithoutProgress)
        return false;
    }
    return false;
  }

  bool sentinelCondition(const Expr *Condition, const Progress &Change) const {
    Condition = ignore(Condition);
    if (Change.RequiresNonzeroCondition)
      return rankNonzeroWhen(Condition, Change, true);
    if (const auto *Logical = dyn_cast_or_null<BinaryOperator>(Condition)) {
      if (Logical->getOpcode() == BO_LAnd)
        return sentinelCondition(Logical->getLHS(), Change) ||
               sentinelCondition(Logical->getRHS(), Change);
      if (Logical->getOpcode() == BO_LOr)
        return sentinelCondition(Logical->getLHS(), Change) &&
               sentinelCondition(Logical->getRHS(), Change);
    }
    /* A nonzero integer condition paired with a strict integer descent is
     * a scalar rank for both unsigned and signed values.  For signed
     * subtraction from a negative value, the only alternative to reaching
     * zero is signed overflow, so no infinite *defined* C execution is
     * admitted.  nonzeroWhen() also handles `n != 0` and conjunctions while
     * deliberately requiring both arms of a disjunction to imply nonzero. */
    if (!Change.GuardedStep && Change.Kind == ProgressKind::Down &&
        Change.Variable->getType()->isIntegerType() &&
        rankNonzeroWhen(Condition, Change, true))
      return true;
    /* A load through the induction variable supplies an object-distance
     * rank only when that induction domain cannot cycle before exhausting
     * the object.  Pointer and signed overflow leave defined C execution;
     * sufficiently wide unsigned indices exhaust every possible object.
     * Narrow unsigned indices require the explicit scalar bound handled
     * above, because their modular arithmetic can revisit the same bytes. */
    return sentinelDomainCannotCycle(Change) &&
           sentinelRead(Condition, Change);
  }

  static bool constantFalse(const Expr *Condition, ASTContext &Context) {
    if (!Condition)
      return false;
    Expr::EvalResult Result;
    return Condition->EvaluateAsInt(Result, Context) &&
           Result.Val.getInt().isZero();
  }

  static std::optional<Progress> conditionCountdown(const Expr *Condition) {
    Condition = ignore(Condition);
    auto Decremented = [](const Expr *Expression)
        -> std::optional<Progress> {
      const auto *Unary = dyn_cast_or_null<UnaryOperator>(ignore(Expression));
      if (!Unary || !Unary->isDecrementOp())
        return std::nullopt;
      const ValueDecl *Variable = value(Unary->getSubExpr());
      if (!Variable)
        return std::nullopt;
      const auto *Member = dyn_cast_or_null<MemberExpr>(
          ignore(Unary->getSubExpr()));
      const ValueDecl *Base = Member ? value(Member->getBase()) : nullptr;
      return makeProgress(Variable, ProgressKind::Down, Base,
                          Unary->getSubExpr(), nullptr, false, true);
    };
    if (std::optional<Progress> Change = Decremented(Condition))
      return Change;
    const auto *Comparison = dyn_cast_or_null<BinaryOperator>(Condition);
    if (!Comparison)
      return std::nullopt;
    std::optional<Progress> Left = Decremented(Comparison->getLHS());
    std::optional<Progress> Right = Decremented(Comparison->getRHS());
    if (Left && zeroInteger(Comparison->getRHS()) &&
        (Comparison->getOpcode() == BO_GT ||
         Comparison->getOpcode() == BO_NE))
      return Left;
    if (Right && zeroInteger(Comparison->getLHS()) &&
        (Comparison->getOpcode() == BO_LT ||
         Comparison->getOpcode() == BO_NE))
      return Right;
    return std::nullopt;
  }

  std::string loopProof(const Expr *Condition, const Expr *Increment,
                        const Stmt *Body) const {
    if (constantFalse(Condition, Context))
      return "constant-false";
    /* Without an explicit total/pure call summary, a call made while
     * deciding whether to take the backedge can both fail to return and
     * mutate globally reachable rank or bound state. */
    if (containsCall(Condition))
      return "unproved";
    /* `while (n--)` and `for (...; n-- > 0; ...)` perform their strict
     * descent in the condition, before every taken iteration.  The final
     * false test may itself wrap an unsigned n, but there is no following
     * backedge, so that cannot create a cycle. */
    if (std::optional<Progress> Change = conditionCountdown(Condition)) {
      if (Change->Variable->getType()->isIntegerType() &&
          validRankVariable(*Change, Body, Increment) &&
          mutation(Body, *Change) == Mutation::None)
        return "strict-scalar-rank";
    }
    if (std::optional<Progress> Change = progress(Increment)) {
      /* The for increment is on every backedge, but an additional body
       * mutation could cancel it or turn the effective step into a
       * sentinel-skipping/wrapping step. */
      Mutation BodyMutation = mutation(Body, *Change);
      /* An additional same-direction unit step cannot invalidate a signed
       * induction rank: either the comparison is reached after finitely
       * many steps, or the addition overflows and the execution was already
       * outside C's defined domain.  Keep rejecting it for unsigned ranks,
       * where wrapping is defined and can make the loop genuinely cycle. */
      if (!validRankVariable(*Change, Body, Increment) ||
          (BodyMutation != Mutation::None &&
           !(BodyMutation == Mutation::Good &&
             (Change->Variable->getType()->isSignedIntegerType() ||
              Change->Variable->getType()->isPointerType()))))
        return "unproved";
      if (strictComparison(Condition, *Change, Body, Increment))
        return "strict-scalar-rank";
      if (sentinelCondition(Condition, *Change))
        return "sentinel-distance-rank";
      return "unproved";
    }
    /* A comma expression is the normal spelling of a multi-variable for
     * increment (`i++, j--`).  progress() intentionally describes one
     * scalar, so collect each candidate and prove the one used by the loop
     * condition.  mutation() still rejects cancellation of that candidate.
     */
    std::vector<Progress> IncrementCandidates;
    collectProgress(Increment, IncrementCandidates);
    for (const Progress &Change : IncrementCandidates) {
      Mutation BodyMutation = mutation(Body, Change);
      if (!validRankVariable(Change, Body, Increment) ||
          mutation(Increment, Change) != Mutation::Good ||
          (BodyMutation != Mutation::None &&
           !(BodyMutation == Mutation::Good &&
             (Change.Variable->getType()->isSignedIntegerType() ||
              Change.Variable->getType()->isPointerType()))))
        continue;
      if (strictComparison(Condition, Change, Body, Increment))
        return "strict-scalar-rank";
      if (sentinelCondition(Condition, Change))
        return "sentinel-distance-rank";
    }
    std::vector<Progress> Candidates;
    collectProgress(Body, Candidates);
    for (const Progress &Change : Candidates) {
      if (!validRankVariable(Change, Body, Increment) ||
          !bodyGuaranteesProgress(Body, Change))
        continue;
      if (strictComparison(Condition, Change, Body, Increment))
        return "strict-scalar-rank";
      if (!Condition && Change.UnitStep &&
          Change.Kind == ProgressKind::Down &&
          Change.Variable->getType()->isIntegerType()) {
        Progress UnitChange = Change;
        UnitChange.UnitOnly = true;
        if (bodyGuaranteesProgress(Body, UnitChange) &&
            bodyHasDominatingNonzeroGuard(Body, UnitChange))
          return "strict-scalar-rank";
      }
      if (sentinelCondition(Condition, Change))
        return "sentinel-distance-rank";
    }
    return "unproved";
  }

  void loop(StringRef Kind, const Stmt *Statement, const Expr *Condition,
            const Expr *Increment, const Stmt *Body) const {
    if (!Current)
      return;
    llvm::outs() << "L\t" << key(Current) << '\t'
                 << file(Statement->getBeginLoc()) << '\t'
                 << line(Statement->getBeginLoc()) << '\t' << Kind << '\t'
                 << loopProof(Condition, Increment, Body) << '\t'
                 << text(Statement) << '\n';
  }

public:
  explicit TotalityVisitor(ASTContext &Context)
      : Context(Context), SM(Context.getSourceManager()) {}

  bool TraverseFunctionDecl(FunctionDecl *Function) {
    if (!Function->isThisDeclarationADefinition() ||
        !SM.isWrittenInMainFile(SM.getExpansionLoc(Function->getLocation())))
      return true;
    const FunctionDecl *Saved = Current;
    Current = Function;
    llvm::outs() << "F\t" << key(Function) << '\t'
                 << file(Function->getLocation()) << '\t'
                 << line(Function->getLocation()) << '\t'
                 << (Function->isNoReturn() ? "noreturn" : "returns") << '\t'
                 << Function->getNumParams() << '\n';
    RecursiveASTVisitor<TotalityVisitor>::TraverseStmt(Function->getBody());
    Current = Saved;
    return true;
  }

  bool VisitCallExpr(CallExpr *Call) {
    if (!Current)
      return true;
    const FunctionDecl *Callee = Call->getDirectCallee();
    llvm::outs() << (Callee ? "C\t" : "I\t") << key(Current) << '\t';
    if (Callee)
      llvm::outs() << key(Callee);
    else
      llvm::outs() << text(Call->getCallee());
    llvm::outs() << '\t' << file(Call->getExprLoc()) << '\t'
                 << line(Call->getExprLoc()) << '\t'
                 << (Callee ? callRelations(Call, Callee) : "-") << '\n';
    return true;
  }

  bool VisitForStmt(ForStmt *Loop) {
    loop("for", Loop, Loop->getCond(), Loop->getInc(), Loop->getBody());
    return true;
  }

  bool VisitWhileStmt(WhileStmt *Loop) {
    loop("while", Loop, Loop->getCond(), nullptr, Loop->getBody());
    return true;
  }

  bool VisitDoStmt(DoStmt *Loop) {
    loop("do", Loop, Loop->getCond(), nullptr, Loop->getBody());
    return true;
  }
};

class TotalityConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &Context) override {
    TotalityVisitor(Context).TraverseDecl(Context.getTranslationUnitDecl());
  }
};

class TotalityAction : public PluginASTAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                 StringRef) override {
    return std::make_unique<TotalityConsumer>();
  }

  bool ParseArgs(const CompilerInstance &,
                 const std::vector<std::string> &) override {
    return true;
  }

  ActionType getActionType() override { return AddAfterMainAction; }
};

} // namespace

static FrontendPluginRegistry::Add<TotalityAction>
    X("ntlibc-totality", "emit ntlibc totality proof inputs");
