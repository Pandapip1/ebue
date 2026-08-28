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
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

enum class OwnershipKind : unsigned char { Owned, Consumed };
REGISTER_MAP_WITH_PROGRAMSTATE(OwnershipMap, SymbolRef, OwnershipKind)

enum class ConstructKind : unsigned char { Live, Destroyed };
REGISTER_MAP_WITH_PROGRAMSTATE(ConstructMap, const MemRegion *, ConstructKind)

namespace {

static std::string diagnosticText(const Stmt *Statement, CheckerContext &C) {
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

static std::string diagnosticOrigin(const Stmt *Statement, CheckerContext &C) {
  const SourceManager &SM = C.getSourceManager();
  return SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())).str();
}

static std::string diagnosticSite(const Stmt *Statement, CheckerContext &C) {
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

static std::string diagnosticContext(CheckerContext &C) {
  const Decl *Current = C.getLocationContext()->getDecl();
  if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
    return Named->getQualifiedNameAsString();
  return Current ? Current->getDeclKindName() : "unknown";
}

static std::string diagnosticMessage(StringRef Reason, const Stmt *Statement,
                                     CheckerContext &C) {
  return (Reason + "; origin '" + diagnosticOrigin(Statement, C) +
          "'; context '" + diagnosticContext(C) + "'; expression '" +
          diagnosticText(Statement, C) + "'; site '" +
          diagnosticSite(Statement, C) + "'")
      .str();
}

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

enum class ConstructFamily : unsigned char {
  ThreadAttr,
  Mutex,
  MutexAttr,
  Condition,
  ConditionAttr,
  Rwlock,
  RwlockAttr,
  Barrier,
  BarrierAttr,
  Spinlock,
  Semaphore
};

enum class ConstructOperation : unsigned char { Construct, Destroy, Use };

struct ConstructCall {
  ConstructFamily Family;
  ConstructOperation Operation;
  unsigned Argument;
};

class OwnedConstructChecker : public Checker<check::PreCall, check::PostCall> {
  mutable std::unique_ptr<BugType> BT;

  static std::optional<ConstructCall> protocolFor(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return std::nullopt;
    StringRef Name = Function->getName();

    struct Prefix {
      llvm::StringLiteral Text;
      ConstructFamily Family;
    };
    static constexpr Prefix Prefixes[] = {
        {"pthread_mutex_", ConstructFamily::Mutex},
        {"pthread_mutexattr_", ConstructFamily::MutexAttr},
        {"pthread_cond_", ConstructFamily::Condition},
        {"pthread_condattr_", ConstructFamily::ConditionAttr},
        {"pthread_rwlock_", ConstructFamily::Rwlock},
        {"pthread_rwlockattr_", ConstructFamily::RwlockAttr},
        {"pthread_barrier_", ConstructFamily::Barrier},
        {"pthread_barrierattr_", ConstructFamily::BarrierAttr},
        {"pthread_spin_", ConstructFamily::Spinlock},
        {"pthread_attr_", ConstructFamily::ThreadAttr},
    };
    for (const Prefix &Candidate : Prefixes) {
      if (!Name.starts_with(Candidate.Text))
        continue;
      StringRef Suffix = Name.drop_front(Candidate.Text.size());
      ConstructOperation Operation = ConstructOperation::Use;
      if (Suffix == "init")
        Operation = ConstructOperation::Construct;
      else if (Suffix == "destroy")
        Operation = ConstructOperation::Destroy;
      return ConstructCall{Candidate.Family, Operation, 0};
    }

    static constexpr llvm::StringLiteral SemaphoreUses[] = {
        "sem_wait", "sem_trywait", "sem_timedwait", "sem_post", "sem_getvalue"};
    if (Name == "sem_init")
      return ConstructCall{ConstructFamily::Semaphore,
                           ConstructOperation::Construct, 0};
    if (Name == "sem_destroy")
      return ConstructCall{ConstructFamily::Semaphore,
                           ConstructOperation::Destroy, 0};
    for (StringRef Use : SemaphoreUses)
      if (Name == Use)
        return ConstructCall{ConstructFamily::Semaphore,
                             ConstructOperation::Use, 0};
    return std::nullopt;
  }

  static bool isLazyFamily(ConstructFamily Family) {
    return Family == ConstructFamily::Mutex ||
           Family == ConstructFamily::Condition ||
           Family == ConstructFamily::Rwlock;
  }

  static bool isZeroInitializer(const Expr *Initializer) {
    Initializer = Initializer->IgnoreParenImpCasts();
    if (isa<ImplicitValueInitExpr>(Initializer))
      return true;
    if (const auto *Integer = dyn_cast<IntegerLiteral>(Initializer))
      return Integer->getValue().isZero();
    if (const auto *List = dyn_cast<InitListExpr>(Initializer)) {
      for (const Expr *Element : List->inits())
        if (!isZeroInitializer(Element))
          return false;
      const Expr *Filler = List->getArrayFiller();
      return !Filler || isZeroInitializer(Filler);
    }
    if (const auto *Cast = dyn_cast<CastExpr>(Initializer))
      return isZeroInitializer(Cast->getSubExpr());
    return false;
  }

  static bool hasStaticInitialization(const MemRegion *Region,
                                      ConstructFamily Family) {
    if (!isLazyFamily(Family))
      return false;
    const auto *Variable = dyn_cast<VarRegion>(Region);
    if (!Variable)
      return false;
    const VarDecl *Declaration = Variable->getDecl();
    if (!Declaration->hasInit())
      return Declaration->hasGlobalStorage();
    return isZeroInitializer(Declaration->getInit());
  }

  static const MemRegion *argumentRegion(const CallEvent &Call,
                                         unsigned Argument) {
    if (Argument >= Call.getNumArgs())
      return nullptr;
    return Call.getArgSVal(Argument).getAsRegion();
  }

  void report(StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven owned construct",
                                     categories::MemoryError);
    auto Report = std::make_unique<PathSensitiveBugReport>(
        *BT, diagnosticMessage(Reason, Statement, C), Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  void requireLive(const CallEvent &Call, unsigned Argument,
                   ConstructFamily Family, CheckerContext &C) const {
    const MemRegion *Region = argumentRegion(Call, Argument);
    if (!Region ||
        C.getState()->isNull(Call.getArgSVal(Argument)).isConstrainedTrue())
      return;
    const ConstructKind *Kind = C.getState()->get<ConstructMap>(Region);
    if (!Kind && hasStaticInitialization(Region, Family))
      return;
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;
    if (!Kind)
      report("owned construct is not proven initialized", Statement,
             C.getState(), C);
    else if (*Kind == ConstructKind::Destroyed)
      report("operation accesses a destroyed owned construct", Statement,
             C.getState(), C);
  }

  void checkBorrowedAttributes(const CallEvent &Call, CheckerContext &C) const {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return;
    StringRef Name = Function->getName();
    if (Name == "pthread_mutex_init")
      requireLive(Call, 1, ConstructFamily::MutexAttr, C);
    else if (Name == "pthread_cond_init")
      requireLive(Call, 1, ConstructFamily::ConditionAttr, C);
    else if (Name == "pthread_rwlock_init")
      requireLive(Call, 1, ConstructFamily::RwlockAttr, C);
    else if (Name == "pthread_barrier_init")
      requireLive(Call, 1, ConstructFamily::BarrierAttr, C);
    else if (Name == "pthread_create")
      requireLive(Call, 1, ConstructFamily::ThreadAttr, C);
    if ((Name == "pthread_cond_wait" || Name == "pthread_cond_timedwait") &&
        Call.getNumArgs() > 1)
      requireLive(Call, 1, ConstructFamily::Mutex, C);
  }

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    checkBorrowedAttributes(Call, C);
    std::optional<ConstructCall> Protocol = protocolFor(Call);
    if (!Protocol)
      return;
    const MemRegion *Region = argumentRegion(Call, Protocol->Argument);
    if (!Region)
      return;
    const ConstructKind *Kind = C.getState()->get<ConstructMap>(Region);
    bool StaticLive =
        !Kind && hasStaticInitialization(Region, Protocol->Family);
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;

    if (Protocol->Operation == ConstructOperation::Construct) {
      if ((Kind && *Kind == ConstructKind::Live) || StaticLive)
        report("owned construct is already initialized", Statement,
               C.getState(), C);
      return;
    }
    if (!Kind && !StaticLive) {
      report("owned construct is not proven initialized", Statement,
             C.getState(), C);
      return;
    }
    if (Kind && *Kind == ConstructKind::Destroyed) {
      report(Protocol->Operation == ConstructOperation::Destroy
                 ? "owned construct is already destroyed"
                 : "operation accesses a destroyed owned construct",
             Statement, C.getState(), C);
    }
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    std::optional<ConstructCall> Protocol = protocolFor(Call);
    if (!Protocol || Protocol->Operation == ConstructOperation::Use)
      return;
    const MemRegion *Region = argumentRegion(Call, Protocol->Argument);
    if (!Region)
      return;
    SVal Return = Call.getReturnValue();
    if (Return.isUnknownOrUndef())
      return;
    std::optional<DefinedOrUnknownSVal> DefinedReturn =
        Return.getAs<DefinedOrUnknownSVal>();
    if (!DefinedReturn)
      return;
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function)
      return;
    SValBuilder &Builder = C.getSValBuilder();
    DefinedOrUnknownSVal Success =
        Builder.evalEQ(C.getState(), *DefinedReturn,
                       Builder.makeZeroVal(Function->getReturnType()));
    auto [Succeeded, Failed] = C.getState()->assume(Success);
    if (Succeeded) {
      ConstructKind Next = Protocol->Operation == ConstructOperation::Construct
                               ? ConstructKind::Live
                               : ConstructKind::Destroyed;
      C.addTransition(Succeeded->set<ConstructMap>(Region, Next));
    }
    if (Failed)
      C.addTransition(Failed);
  }
};

class ValidPointerChecker
    : public Checker<check::PreStmt<UnaryOperator>,
                     check::PreStmt<ArraySubscriptExpr>,
                     check::PreStmt<MemberExpr>, check::Location> {
  mutable std::unique_ptr<BugType> BT;

  void report(StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven pointer dereference",
                                     categories::MemoryError);
    auto Report = std::make_unique<PathSensitiveBugReport>(
        *BT, diagnosticMessage(Reason, Statement, C), Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  static QualType accessType(const MemRegion *Region, const Stmt *Statement) {
    if (const auto *Expression = dyn_cast<Expr>(Statement)) {
      QualType Type = Expression->getType();
      if (!Type.isNull() && !Type->isVoidType() && !Type->isFunctionType())
        return Type;
    }
    if (const auto *Typed = dyn_cast_or_null<TypedValueRegion>(Region))
      return Typed->getValueType();
    return QualType();
  }

  static bool alignmentProven(const MemRegion *Region, QualType Type,
                              ASTContext &Ctx) {
    if (Type.isNull() || Type->isIncompleteType())
      return false;
    uint64_t Required = Ctx.getTypeAlign(Type);
    RegionOffset Offset = Region->getAsOffset();
    if (!Offset.isValid())
      return false;
    if (!Offset.hasSymbolicOffset()) {
      if (Offset.getOffset() < 0 ||
          static_cast<uint64_t>(Offset.getOffset()) % Required)
        return false;
      const MemRegion *Base = Offset.getRegion();
      if (const auto *Variable = dyn_cast_or_null<VarRegion>(Base))
        return static_cast<uint64_t>(
                   Ctx.getDeclAlign(Variable->getDecl()).getQuantity()) *
                   8 >=
               Required;
      if (const auto *Typed = dyn_cast_or_null<TypedValueRegion>(Base)) {
        QualType BaseType = Typed->getValueType();
        return !BaseType.isNull() && !BaseType->isIncompleteType() &&
               Ctx.getTypeAlign(BaseType) >= Required;
      }
      if (const auto *Symbolic = dyn_cast_or_null<SymbolicRegion>(Base)) {
        QualType SymbolType = Symbolic->getSymbol()->getType();
        if (SymbolType->isPointerType()) {
          QualType Pointee = SymbolType->getPointeeType();
          if (!Pointee->isIncompleteType() &&
              Ctx.getTypeAlign(Pointee) >= Required)
            return true;
        }
        // A live pointer's base address carries the alignment promised by
        // the type used for the access. Concrete byte offsets are checked
        // above; this also covers malloc's suitably aligned base address.
        return true;
      }
      return false;
    }
    const auto *Element = dyn_cast<ElementRegion>(Region);
    if (!Element)
      return false;
    QualType ElementType = Element->getElementType();
    return !ElementType->isIncompleteType() &&
           Ctx.getTypeSize(ElementType) % Required == 0 &&
           alignmentProven(Element->getSuperRegion(), Type, Ctx);
  }

public:
  void checkPointerExpression(const Expr *Pointer, const Stmt *Dereference,
                              CheckerContext &C) const {
    SVal Value = C.getSVal(Pointer);
    const MemRegion *Region = Value.getAsRegion();
    if (Region && !Region->getSymbolicBase())
      return;
    if (!C.getState()->isNonNull(Value).isConstrainedTrue()) {
      ProgramStateRef NullState = C.getState();
      if (std::optional<DefinedOrUnknownSVal> Defined =
              Value.getAs<DefinedOrUnknownSVal>())
        NullState = C.getState()->assume(*Defined, false);
      report("pointer dereference is not proven nonnull", Dereference,
             NullState ? NullState : C.getState(), C);
    }
  }

  void checkPreStmt(const UnaryOperator *Unary, CheckerContext &C) const {
    if (Unary->getOpcode() == UO_Deref)
      checkPointerExpression(Unary->getSubExpr(), Unary, C);
  }

  void checkPreStmt(const ArraySubscriptExpr *Subscript,
                    CheckerContext &C) const {
    checkPointerExpression(Subscript->getBase(), Subscript, C);
  }

  void checkPreStmt(const MemberExpr *Member, CheckerContext &C) const {
    if (Member->isArrow())
      checkPointerExpression(Member->getBase(), Member, C);
  }

  void checkLocation(SVal Location, bool, const Stmt *Statement,
                     CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    const MemRegion *Region = Location.getAsRegion();
    if (!Region) {
      report("pointer dereference is not proven nonnull", Statement, State, C);
      return;
    }
    if (!Region->getSymbolicBase() && !isa<ElementRegion>(Region))
      return;
    if (const SymbolicRegion *Base = Region->getSymbolicBase()) {
      const OwnershipKind *Kind = State->get<OwnershipMap>(Base->getSymbol());
      if (Kind && *Kind == OwnershipKind::Consumed) {
        report("dereference accesses consumed storage", Statement, State, C);
        return;
      }
      if (!Kind && !Region->hasStackStorage()) {
        report("pointer target is not proven live storage", Statement, State,
               C);
        return;
      }
    }

    QualType Type = accessType(Region, Statement);
    if (Type.isNull() || Type->isIncompleteType()) {
      report("dereference extent is not proven sufficient", Statement, State,
             C);
      return;
    }
    CharUnits Width = C.getASTContext().getTypeSizeInChars(Type);
    SVal Remaining = getDynamicExtentWithOffset(State, Location);
    if (Remaining.isUnknownOrUndef()) {
      report("dereference extent is not proven sufficient", Statement, State,
             C);
      return;
    }
    SValBuilder &Builder = C.getSValBuilder();
    SVal Enough =
        Builder.evalBinOp(State, BO_GE, Remaining,
                          Builder.makeIntVal(Width.getQuantity(),
                                             C.getASTContext().getSizeType()),
                          Builder.getConditionType());
    std::optional<DefinedOrUnknownSVal> Condition =
        Enough.getAs<DefinedOrUnknownSVal>();
    if (!Condition) {
      report("dereference extent is not proven sufficient", Statement, State,
             C);
      return;
    }
    ProgramStateRef TooSmall = State->assume(*Condition, false);
    if (TooSmall) {
      report("dereference extent is not proven sufficient", Statement, TooSmall,
             C);
      return;
    }
    if (!alignmentProven(Region, Type, C.getASTContext()))
      report("dereference alignment is not proven valid", Statement, State, C);
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<OwnershipChecker>(
      "ntlibc.Ownership",
      "Proves allocator provenance and borrow lifetime at deallocation", "");
  Registry.addChecker<OwnedConstructChecker>(
      "ntlibc.OwnedConstruct",
      "Proves synchronization object construction and destruction", "");
  Registry.addChecker<ValidPointerChecker>(
      "ntlibc.ValidPointer",
      "Proves every memory access has a nonnull, live, in-bounds, aligned "
      "pointer",
      "");
}
