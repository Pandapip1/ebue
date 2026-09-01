// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

REGISTER_MAP_WITH_PROGRAMSTATE(AllocationOrigin, SymbolRef, const Stmt *)
REGISTER_MAP_WITH_PROGRAMSTATE(AllocationFrame, SymbolRef,
                               const StackFrameContext *)
REGISTER_MAP_WITH_PROGRAMSTATE(AllocationFamily, SymbolRef,
                               const IdentifierInfo *)
REGISTER_MAP_WITH_PROGRAMSTATE(FreerObligation, SymbolRef, bool)
REGISTER_MAP_WITH_PROGRAMSTATE(ReplacedBy, SymbolRef, SymbolRef)

namespace {

struct TokenContract {
  const IdentifierInfo *Family;
  const Attr *Attribute;
};

static const FunctionDecl *functionOf(const CallEvent &Call) {
  return dyn_cast_or_null<FunctionDecl>(Call.getDecl());
}

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

static bool isDynamicStorageToken(ASTContext &Context, StringRef Name) {
  const TypedefNameDecl *Token = dialectToken(Context, Name);
  if (!Token)
    return false;
  for (const AnnotateAttr *Attribute :
       Token->specific_attrs<AnnotateAttr>())
    if (Attribute->getAnnotation() == "qual:dynamic_storage")
      return true;
  return false;
}

static const IdentifierInfo *annotationFamily(const Decl *Declaration,
                                              StringRef Prefix) {
  if (!Declaration)
    return nullptr;
  for (const AnnotateAttr *Attribute :
       Declaration->specific_attrs<AnnotateAttr>()) {
    StringRef Text = Attribute->getAnnotation();
    if (Text.consume_front(Prefix) && !Text.empty() && !Text.contains(':') &&
        isDynamicStorageToken(Declaration->getASTContext(), Text))
      return &Declaration->getASTContext().Idents.get(Text);
  }
  return nullptr;
}

static std::optional<TokenContract>
returnsOwnership(const FunctionDecl *Function) {
  if (!Function)
    return std::nullopt;
  for (const AnnotateAttr *Attribute :
       Function->specific_attrs<AnnotateAttr>()) {
    StringRef Text = Attribute->getAnnotation();
    if (Text.consume_front("withtok:") && !Text.empty() &&
        !Text.contains(':') &&
        isDynamicStorageToken(Function->getASTContext(), Text))
      return TokenContract{&Function->getASTContext().Idents.get(Text),
                           Attribute};
  }
  return std::nullopt;
}

static bool takesAnyArgument(const FunctionDecl *Function, unsigned Argument) {
  if (!Function)
    return false;
  if (Argument < Function->getNumParams() &&
      annotationFamily(Function->getParamDecl(Argument), "consume:"))
    return true;
  return false;
}

/* A reallocation input is consumed only when the returned pointer is nonnull.
 * This remains declaration-driven: the checker contains no allocator names. */
static std::optional<unsigned>
reallocatedArgument(const FunctionDecl *Function) {
  if (!Function)
    return std::nullopt;
  for (unsigned Argument = 0; Argument < Function->getNumParams(); ++Argument)
    if (annotationFamily(Function->getParamDecl(Argument),
                         "consume_if_nonnull_return:"))
      return Argument;
  return std::nullopt;
}

/* Some POSIX interfaces return the caller's buffer when it is nonnull and
 * allocate one only when that argument is null.  The ordinary returns
 * attribute would incorrectly make both results owned. */
static std::optional<unsigned>
returnedArgument(const FunctionDecl *Function, const IdentifierInfo *Family) {
  if (!Function)
    return std::nullopt;
  if (!Family)
    return std::nullopt;
  unsigned Argument = 0;
  for (const ParmVarDecl *Parameter : Function->parameters()) {
    for (const AnnotateAttr *Attribute :
         Parameter->specific_attrs<AnnotateAttr>()) {
      StringRef Text = Attribute->getAnnotation();
      if (Text.consume_front("withtok:") && Text == Family->getName())
        return Argument;
    }
    ++Argument;
  }
  return std::nullopt;
}

static bool takesArgument(const FunctionDecl *Function,
                          const IdentifierInfo *Family, unsigned Argument) {
  if (!Function || !Family)
    return false;
  if (Argument < Function->getNumParams() &&
      annotationFamily(Function->getParamDecl(Argument), "consume:") == Family)
    return true;
  return false;
}

static std::string sourceText(const Stmt *Statement, CheckerContext &C) {
  if (!Statement)
    return "function exit";
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

static std::string contextName(CheckerContext &C) {
  const Decl *Current = C.getLocationContext()->getDecl();
  if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
    return Named->getQualifiedNameAsString();
  return Current ? Current->getDeclKindName() : "unknown";
}

class AllocationLifetimeChecker
    : public Checker<check::ASTDecl<FunctionDecl>, check::BeginFunction,
                     check::PreCall, check::PostCall, check::EndFunction> {
  mutable std::unique_ptr<BugType> BT;

  static ProgramStateRef forget(ProgramStateRef State, SymbolRef Symbol) {
    if (!Symbol)
      return State;
    return State->remove<AllocationOrigin>(Symbol)
        ->remove<AllocationFrame>(Symbol)
        ->remove<AllocationFamily>(Symbol)
        ->remove<FreerObligation>(Symbol)
        ->remove<ReplacedBy>(Symbol);
  }

  static ProgramStateRef track(ProgramStateRef State, SymbolRef Symbol,
                               const Stmt *Origin,
                               const StackFrameContext *Frame,
                               const IdentifierInfo *Family,
                               bool IsFreerObligation) {
    if (!Symbol || !Frame || !Family)
      return State;
    State = State->set<AllocationOrigin>(Symbol, Origin);
    State = State->set<AllocationFrame>(Symbol, Frame);
    State = State->set<AllocationFamily>(Symbol, Family);
    return State->set<FreerObligation>(Symbol, IsFreerObligation);
  }

  static bool belongsToFrame(ProgramStateRef State, SymbolRef Symbol,
                             const StackFrameContext *Frame) {
    const StackFrameContext *const *Owner =
        State->get<AllocationFrame>(Symbol);
    return Owner && *Owner == Frame;
  }

  static bool canBeNonNull(ProgramStateRef State, SymbolRef Symbol,
                           CheckerContext &C) {
    DefinedSVal Value = C.getSValBuilder().makeSymbolVal(Symbol);
    return State->assume(Value, true) != nullptr;
  }

  static bool replacedOnThisPath(ProgramStateRef State, SymbolRef Symbol,
                                 CheckerContext &C) {
    const SymbolRef *Replacement = State->get<ReplacedBy>(Symbol);
    if (!Replacement)
      return false;
    DefinedSVal Value = C.getSValBuilder().makeSymbolVal(*Replacement);
    return State->assume(Value, false) == nullptr;
  }

  /* Retire both sides of a realloc transition once control flow has proved
   * which side owns storage.  Doing this at the actual release call is
   * important: dead-symbol cleanup may discard the null/non-null constraint
   * before checkEndFunction, but `if (replacement) free(replacement); else
   * free(old);` has the decisive fact available at each of these two calls. */
  static ProgramStateRef retireReallocationPeer(ProgramStateRef State,
                                                 SymbolRef Consumed) {
    SymbolRef Current = Consumed;
    for (;;) {
      SymbolRef Predecessor = nullptr;
      for (const auto &Entry : State->get<ReplacedBy>()) {
        if (Entry.second == Current) {
          Predecessor = Entry.first;
          break;
        }
      }
      if (!Predecessor)
        break;
      State = forget(State, Predecessor);
      Current = Predecessor;
    }
    return State;
  }

  void report(StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unreleased dynamic allocation",
                                     categories::MemoryError);
    std::string Message =
        (Reason + "; context '" + contextName(C) + "'; allocation '" +
         sourceText(Statement, C) + "'")
            .str();
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    if (Statement)
      Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

public:
  void checkASTDecl(const FunctionDecl *Function, AnalysisManager &,
                    BugReporter &) const {
    if (!Function->getIdentifier())
      return;
    const SourceManager &SM = Function->getASTContext().getSourceManager();
    SourceLocation Location = SM.getExpansionLoc(Function->getLocation());
    std::string Path = SM.getFilename(Location).str();
    unsigned Line = SM.getSpellingLineNumber(Location);
    if (Function->doesThisDeclarationHaveABody())
      llvm::errs() << "ntlibc-allocation-contract: definition\t-\t"
                   << Function->getName() << '\t' << Path << '\t' << Line
                   << '\n';
    for (const AnnotateAttr *Attribute :
         Function->specific_attrs<AnnotateAttr>()) {
      StringRef Family = Attribute->getAnnotation();
      if (!Family.consume_front("withtok:") || Family.empty() ||
          Family.contains(':') ||
          !isDynamicStorageToken(Function->getASTContext(), Family))
        continue;
      StringRef Kind = !Function->doesThisDeclarationHaveABody()
                           ? "returns-declaration"
                           : Attribute->isInherited()
                                 ? "returns-definition-inherited"
                                 : "returns-definition-explicit";
      llvm::errs() << "ntlibc-allocation-contract: " << Kind << '\t'
                   << Family << '\t' << Function->getName() << '\t' << Path
                   << '\t' << Line << '\n';
    }
    unsigned Argument = 1;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      for (const AnnotateAttr *Attribute :
           Parameter->specific_attrs<AnnotateAttr>()) {
        StringRef Family = Attribute->getAnnotation();
        if (!Family.consume_front("consume:") || Family.empty() ||
            Family.contains(':') ||
            !isDynamicStorageToken(Function->getASTContext(), Family))
          continue;
        StringRef Kind = !Function->doesThisDeclarationHaveABody()
                             ? "takes-declaration"
                             : Attribute->isInherited()
                                   ? "takes-definition-inherited"
                                   : "takes-definition-explicit";
        llvm::errs() << "ntlibc-allocation-contract: " << Kind << '\t'
                     << Family << '\t' << Function->getName() << '\t'
                     << Argument << '\t' << Path << '\t' << Line << '\n';
      }
      ++Argument;
    }
  }

  void checkBeginFunction(CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function)
      return;
    llvm::SmallVector<std::pair<const IdentifierInfo *, const ParmVarDecl *>, 4>
        Inputs;
    for (const ParmVarDecl *Parameter : Function->parameters())
      if (const IdentifierInfo *Family =
              annotationFamily(Parameter, "consume:"))
        Inputs.push_back({Family, Parameter});
    llvm::SmallVector<ProgramStateRef, 4> States{C.getState()};
    bool Changed = false;
    for (const auto &[Family, Parameter] : Inputs) {
      llvm::SmallVector<ProgramStateRef, 4> NextStates;
      for (ProgramStateRef State : States) {
        SVal Value = State->getSVal(
            State->getLValue(Parameter, C.getLocationContext()));
        SymbolRef Symbol = Value.getAsLocSymbol(true);
        std::optional<DefinedOrUnknownSVal> Defined =
            Value.getAs<DefinedOrUnknownSVal>();
        if (!Symbol || !Defined) {
          NextStates.push_back(State);
          continue;
        }
        auto [NonNullState, NullState] = State->assume(*Defined);
        if (NullState)
          NextStates.push_back(NullState);
        if (NonNullState)
          NextStates.push_back(track(NonNullState, Symbol, nullptr,
                                     C.getStackFrame(), Family, true));
        Changed = true;
      }
      States = std::move(NextStates);
    }
    if (Changed)
      for (ProgramStateRef State : States)
        C.addTransition(State);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    bool Changed = false;
    const FunctionDecl *Function = functionOf(Call);
    for (unsigned Argument = 0; Argument < Call.getNumArgs(); ++Argument) {
      SymbolRef Symbol = Call.getArgSVal(Argument).getAsLocSymbol(true);
      if (!Symbol)
        continue;
      const IdentifierInfo *const *Family =
          State->get<AllocationFamily>(Symbol);
      if (!Family)
        continue;
      const bool *Freer = State->get<FreerObligation>(Symbol);
      bool Consumed = takesArgument(Function, *Family, Argument) ||
                      (Freer && *Freer &&
                       belongsToFrame(State, Symbol, C.getStackFrame()) &&
                       takesAnyArgument(Function, Argument));
      if (!Consumed)
        continue;
      State = retireReallocationPeer(State, Symbol);
      State = forget(State, Symbol);
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    const FunctionDecl *Function = functionOf(Call);
    std::optional<TokenContract> Returns = returnsOwnership(Function);
    if (!Returns)
      return;
    ProgramStateRef State = C.getState();
    if (std::optional<unsigned> Argument =
            returnedArgument(Function, Returns->Family)) {
      if (*Argument >= Call.getNumArgs())
        return;
      std::optional<DefinedOrUnknownSVal> ArgumentValue =
          Call.getArgSVal(*Argument).getAs<DefinedOrUnknownSVal>();
      if (!ArgumentValue)
        return;
      auto [ArgumentNonNullState, ArgumentNullState] =
          State->assume(*ArgumentValue);
      if (ArgumentNonNullState)
        C.addTransition(ArgumentNonNullState);
      if (!ArgumentNullState)
        return;
      State = ArgumentNullState;
    }
    SVal ReturnValue = Call.getReturnValue();
    SymbolRef Result = ReturnValue.getAsLocSymbol(true);
    if (!Result)
      return;
    std::optional<DefinedOrUnknownSVal> Defined =
        ReturnValue.getAs<DefinedOrUnknownSVal>();
    if (!Defined)
      return;
    auto [NonNullState, NullState] = State->assume(*Defined);
    if (NullState)
      C.addTransition(NullState);
    if (!NonNullState)
      return;
    const IdentifierInfo *Family = Returns->Family;
    NonNullState = track(NonNullState, Result, Call.getOriginExpr(),
                         C.getStackFrame(), Family, false);
    if (std::optional<unsigned> Argument =
            reallocatedArgument(functionOf(Call));
        Argument && *Argument < Call.getNumArgs()) {
      SymbolRef Old = Call.getArgSVal(*Argument).getAsLocSymbol(true);
      if (Old && NonNullState->get<AllocationFamily>(Old))
        NonNullState = NonNullState->set<ReplacedBy>(Old, Result);
    }
    C.addTransition(NonNullState);
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    std::optional<TokenContract> Returns = returnsOwnership(Function);
    SymbolRef Returned =
        Return && Return->getRetValue()
            ? C.getSVal(Return->getRetValue()).getAsLocSymbol(true)
            : nullptr;

    if (Returned && belongsToFrame(State, Returned, C.getStackFrame())) {
      const Stmt *const *Origin = State->get<AllocationOrigin>(Returned);
      if (!Returns) {
        report("returned allocation has no dynamic-storage token contract",
               Origin ? *Origin : Return, State, C);
        return;
      }
      State = forget(State, Returned);
    }

    for (const auto &Entry : State->get<AllocationFamily>()) {
      SymbolRef Symbol = Entry.first;
      if (!belongsToFrame(State, Symbol, C.getStackFrame()) ||
          replacedOnThisPath(State, Symbol, C) ||
          !canBeNonNull(State, Symbol, C))
        continue;
      const Stmt *const *Origin = State->get<AllocationOrigin>(Symbol);
      const bool *Freer = State->get<FreerObligation>(Symbol);
      report(Freer && *Freer
                 ? "consume function exits without releasing its argument"
                 : "dynamic allocation is not freed before function exit",
             Origin ? *Origin : (Return ? static_cast<const Stmt *>(Return)
                                        : Function ? Function->getBody() : nullptr),
             State, C);
      return;
    }
  }
};

} // namespace

void registerAllocationLifetimeChecker(CheckerRegistry &Registry) {
  Registry.addChecker<AllocationLifetimeChecker>(
      "ntlibc.AllocationLifetime",
      "Proves allocations are freed or transferred through a paired "
      "dynamic-storage token contract",
      "");
}
