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
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(ignore(Expression));
    return Reference ? dyn_cast<ValueDecl>(Reference->getDecl()) : nullptr;
  }

  static bool unitInteger(const Expr *Expression) {
    const auto *Literal = dyn_cast_or_null<IntegerLiteral>(ignore(Expression));
    return Literal && Literal->getValue() == 1;
  }

  static bool zeroInteger(const Expr *Expression) {
    const auto *Literal = dyn_cast_or_null<IntegerLiteral>(ignore(Expression));
    return Literal && Literal->getValue().isZero();
  }

  static bool nonzeroWhen(const Expr *Condition, const ParmVarDecl *Parameter,
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

  static bool basedOn(const Expr *Expression, const ValueDecl *Variable) {
    Expression = ignore(Expression);
    if (value(Expression) == Variable)
      return true;
    if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Expression))
      return basedOn(Unary->getSubExpr(), Variable);
    if (const auto *Member = dyn_cast_or_null<MemberExpr>(Expression))
      return basedOn(Member->getBase(), Variable);
    if (const auto *Subscript =
            dyn_cast_or_null<ArraySubscriptExpr>(Expression))
      return basedOn(Subscript->getBase(), Variable) ||
             basedOn(Subscript->getIdx(), Variable);
    return false;
  }

  static std::optional<Progress> progress(const Expr *Expression) {
    Expression = ignore(Expression);
    if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Expression)) {
      const ValueDecl *Variable = value(Unary->getSubExpr());
      if (!Variable)
        return std::nullopt;
      if (Unary->isIncrementOp())
        return Progress{Variable, ProgressKind::Up};
      if (Unary->isDecrementOp())
        return Progress{Variable, ProgressKind::Down};
    }
    const auto *Binary = dyn_cast_or_null<BinaryOperator>(Expression);
    if (!Binary)
      return std::nullopt;
    const ValueDecl *Variable = value(Binary->getLHS());
    if (!Variable)
      return std::nullopt;
    /* A step larger than one can jump over the bound and wrap.  In
     * particular, `for (unsigned i = 0; i < UINT_MAX; i += 2)` does not
     * terminate.  Unit steps are the only context-free scalar proof. */
    if (Binary->getOpcode() == BO_AddAssign && unitInteger(Binary->getRHS()))
      return Progress{Variable, ProgressKind::Up};
    if (Binary->getOpcode() == BO_SubAssign && unitInteger(Binary->getRHS()))
      return Progress{Variable, ProgressKind::Down};
    if (!Binary->isAssignmentOp())
      return std::nullopt;
    const Expr *Right = ignore(Binary->getRHS());
    if (const auto *Operation = dyn_cast<BinaryOperator>(Right)) {
      if (value(Operation->getLHS()) == Variable &&
          unitInteger(Operation->getRHS())) {
        if (Operation->getOpcode() == BO_Add)
          return Progress{Variable, ProgressKind::Up};
        if (Operation->getOpcode() == BO_Sub)
          return Progress{Variable, ProgressKind::Down};
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
        if (Change->Variable == Expected.Variable)
          return Change->Kind == Expected.Kind ? Mutation::Good : Mutation::Bad;
      }
      const Expr *Plain = ignore(Expression);
      if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Plain)) {
        if ((Unary->isIncrementDecrementOp() ||
             Unary->getOpcode() == UO_AddrOf) &&
            value(Unary->getSubExpr()) == Expected.Variable)
          return Mutation::Bad;
      }
      if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Plain))
        if (Binary->isAssignmentOp() &&
            value(Binary->getLHS()) == Expected.Variable)
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
    if (isa<GotoStmt>(Statement) || isa<SwitchStmt>(Statement) ||
        isa<ForStmt>(Statement) || isa<WhileStmt>(Statement) ||
        isa<DoStmt>(Statement))
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
          Seen |= Existing.Variable == Change->Variable &&
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
    if (!Field || !BaseDecl)
      return false;
    const auto *BaseVar = dyn_cast<VarDecl>(BaseDecl);
    if (!BaseVar || !(isa<ParmVarDecl>(BaseVar) || BaseVar->hasLocalStorage()) ||
        BaseVar->getType().isVolatileQualified() || !Current)
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
                                 const ValueDecl *Alias) {
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
      if (const auto *Call = dyn_cast_or_null<CallExpr>(Plain))
        for (const Expr *Argument : Call->arguments())
          if (value(Argument) == Alias)
            return true;
    }
    for (const Stmt *Child : Statement->children())
      if (writesThroughAlias(Child, Alias))
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

  bool validRankVariable(const Progress &Change, const Stmt *Body) const {
    const auto *Variable = dyn_cast<VarDecl>(Change.Variable);
    return Variable &&
           (isa<ParmVarDecl>(Variable) || Variable->hasLocalStorage()) &&
           !Variable->getType().isVolatileQualified() && Current &&
           !aliasedWrite(Variable, Body);
  }

  bool stableBound(const Expr *Expression, const Stmt *Body,
                   const Expr *Increment) const {
    if (!Expression)
      return false;
    Expr::EvalResult Constant;
    if (Expression->EvaluateAsInt(Constant, Context))
      return true;
    const Expr *Plain = ignore(Expression);
    if (const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Plain)) {
      const auto *Variable = dyn_cast<VarDecl>(Reference->getDecl());
      return Variable &&
             (isa<ParmVarDecl>(Variable) || Variable->hasLocalStorage()) &&
             !Variable->getType().isVolatileQualified() && Current &&
             !aliasedWrite(Variable, Body) && !writesVariable(Body, Variable) &&
             !writesVariable(Increment, Variable);
    }
    if (const auto *Member = dyn_cast_or_null<MemberExpr>(Plain))
      return memberStable(Member, Body, Increment);
    if (const auto *Unary = dyn_cast_or_null<UnaryOperator>(Plain)) {
      if (Unary->isIncrementDecrementOp() || Unary->getOpcode() == UO_AddrOf ||
          Unary->getOpcode() == UO_Deref)
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

  bool belowTypeMaximum(const Expr *Bound, const ValueDecl *Variable) const {
    QualType VariableType = Variable->getType();
    if (!VariableType->isIntegerType())
      return false;
    llvm::APSInt Maximum = llvm::APSInt::getMaxValue(
        Context.getIntWidth(VariableType),
        VariableType->isUnsignedIntegerOrEnumerationType());
    Expr::EvalResult Constant;
    if (Bound->EvaluateAsInt(Constant, Context))
      return llvm::APSInt::compareValues(Constant.Val.getInt(), Maximum) < 0;
    const Expr *Plain = ignore(Bound);
    if (!Plain || !Plain->getType()->isIntegerType())
      return false;
    QualType BoundType = Plain->getType();
    llvm::APSInt BoundMaximum = llvm::APSInt::getMaxValue(
        Context.getIntWidth(BoundType),
        BoundType->isUnsignedIntegerOrEnumerationType());
    return llvm::APSInt::compareValues(BoundMaximum, Maximum) < 0;
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

  bool strictComparison(const Expr *Condition, const Progress &Change,
                        const Stmt *Body, const Expr *Increment) const {
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
      const ValueDecl *Left = value(Logical->getLHS());
      const ValueDecl *Right = value(Logical->getRHS());
      if (Change.Kind == ProgressKind::Up)
        return (Left == Change.Variable &&
                stableBound(Logical->getRHS(), Body, Increment) &&
                (Logical->getOpcode() == BO_LT ||
                 (Logical->getOpcode() == BO_LE &&
                  belowTypeMaximum(Logical->getRHS(), Change.Variable)))) ||
               (Right == Change.Variable &&
                stableBound(Logical->getLHS(), Body, Increment) &&
                (Logical->getOpcode() == BO_GT ||
                 (Logical->getOpcode() == BO_GE &&
                  belowTypeMaximum(Logical->getLHS(), Change.Variable))));
      if (Change.Kind == ProgressKind::Down)
        return (Left == Change.Variable &&
                stableBound(Logical->getRHS(), Body, Increment) &&
                (Logical->getOpcode() == BO_GT ||
                 (Logical->getOpcode() == BO_GE &&
                  aboveTypeMinimum(Logical->getRHS(), Change.Variable)))) ||
               (Right == Change.Variable &&
                stableBound(Logical->getLHS(), Body, Increment) &&
                (Logical->getOpcode() == BO_LT ||
                 (Logical->getOpcode() == BO_LE &&
                  aboveTypeMinimum(Logical->getLHS(), Change.Variable))));
    }
    return false;
  }

  static bool sentinelRead(const Stmt *Statement, const ValueDecl *Variable) {
    if (!Statement)
      return false;
    if (const auto *Unary = dyn_cast<UnaryOperator>(Statement)) {
      if (Unary->getOpcode() == UO_Deref &&
          basedOn(Unary->getSubExpr(), Variable))
        return true;
    }
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Statement)) {
      if (basedOn(Subscript->getBase(), Variable) ||
          basedOn(Subscript->getIdx(), Variable))
        return true;
    }
    if (const auto *Member = dyn_cast<MemberExpr>(Statement)) {
      if (Member->isArrow() && basedOn(Member->getBase(), Variable))
        return true;
    }
    for (const Stmt *Child : Statement->children())
      if (sentinelRead(Child, Variable))
        return true;
    return false;
  }

  static bool sentinelCondition(const Expr *Condition, const Progress &Change) {
    Condition = ignore(Condition);
    if (const auto *Logical = dyn_cast_or_null<BinaryOperator>(Condition)) {
      if (Logical->getOpcode() == BO_LAnd)
        return sentinelCondition(Logical->getLHS(), Change) ||
               sentinelCondition(Logical->getRHS(), Change);
      if (Logical->getOpcode() == BO_LOr)
        return sentinelCondition(Logical->getLHS(), Change) &&
               sentinelCondition(Logical->getRHS(), Change);
    }
    if (Change.Kind == ProgressKind::Down &&
        Change.Variable->getType()->isUnsignedIntegerType() &&
        value(Condition) == Change.Variable)
      return true;
    /* A load through the induction variable supplies an explicit sentinel
     * rank.  On every defined execution a unit pointer/index step can visit
     * only the finite remainder of its C object before either observing the
     * sentinel or leaving the domain of defined execution. */
    return sentinelRead(Condition, Change.Variable);
  }

  static bool constantFalse(const Expr *Condition, ASTContext &Context) {
    if (!Condition)
      return false;
    Expr::EvalResult Result;
    return Condition->EvaluateAsInt(Result, Context) &&
           Result.Val.getInt().isZero();
  }

  std::string loopProof(const Expr *Condition, const Expr *Increment,
                        const Stmt *Body) const {
    if (constantFalse(Condition, Context))
      return "constant-false";
    if (std::optional<Progress> Change = progress(Increment)) {
      /* The for increment is on every backedge, but an additional body
       * mutation could cancel it or turn the effective step into a
       * sentinel-skipping/wrapping step. */
      if (!validRankVariable(*Change, Body) ||
          mutation(Body, *Change) != Mutation::None)
        return "unproved";
      if (strictComparison(Condition, *Change, Body, Increment))
        return "strict-scalar-rank";
      if (sentinelCondition(Condition, *Change))
        return "sentinel-distance-rank";
      return "unproved";
    }
    std::vector<Progress> Candidates;
    collectProgress(Body, Candidates);
    for (const Progress &Change : Candidates) {
      if (!validRankVariable(Change, Body) ||
          !bodyGuaranteesProgress(Body, Change))
        continue;
      if (strictComparison(Condition, Change, Body, Increment))
        return "strict-scalar-rank";
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
