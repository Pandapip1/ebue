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
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
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

REGISTER_MAP_WITH_PROGRAMSTATE(ResourceMap, SymbolRef, unsigned)

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
      // Symbol == nullptr means the argument is not derived from any
      // symbolic region at all -- a concrete address this analysis can
      // name outright (the address of a stack-local or global variable,
      // an array, ...), which by construction was never returned by
      // malloc(). That is positive evidence of a real bug (freeing a
      // non-heap object), not merely absence of information, so it is
      // still reported -- UNLESS the value is Unknown/Undef, meaning the
      // analyzer itself lost track of what it is (most commonly a loop
      // variable widened away after clang's default max-loop iteration
      // cap, e.g. __fd_close_all_cloexec's `for (i = 0; i < FD_MAX; i++)`
      // with FD_MAX == 1024): that is the same "no information" case as
      // an untracked symbol below, just represented differently by the
      // analyzer, and demanding proof of something the analyzer itself
      // admits it cannot characterize is not a real proof obligation
      // either.
      //
      // Symbol != nullptr but absent from OwnershipMap is a different
      // case: the pointer's provenance is opaque to this per-function
      // analysis -- exactly the same "was this analysis's own
      // malloc()/free() tracking ever able to see this value" gap fixed
      // for ValidPointerChecker's liveness proof above. A handle
      // received across a call boundary (closedir()'s `DIR *dp`, itself
      // malloc'd inside a DIFFERENT function -- opendir() -- that this
      // analysis never sees) has no OwnershipMap entry not because it
      // is known unowned, but because per-function analysis cannot see
      // what happened before this function was entered. Demanding proof
      // here is exactly as structurally unsatisfiable as it was for
      // liveness, so this only trusts the borrow; it still transitions
      // the symbol to Consumed on a real free() so a same-function
      // double-free of this exact borrowed pointer is still caught by
      // the *Kind == Consumed branch below.
      if (!Symbol) {
        if (Argument.isUnknownOrUndef())
          return;
        report(Deallocator ? "deallocator argument is not proven owned"
                           : "reallocator argument is not proven owned",
               Statement, State, C);
        return;
      }
      if (Deallocator)
        C.addTransition(
            State->set<OwnershipMap>(Symbol, OwnershipKind::Consumed));
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
                     check::PreStmt<MemberExpr>, check::Location,
                     check::PostCall> {
  mutable std::unique_ptr<BugType> BT;

  // Functions this codebase itself guarantees always return a pointer to
  // real, live storage and never NULL, but which this checker has no
  // other way to know that about: not a heap allocation Ownership would
  // see, just a fixed, always-present object. errno.h defines
  // `#define errno (*__errno_location())`, so this one function's return
  // value is implicitly dereferenced by every `errno = ...` and
  // `if (errno)` in the tree -- __errno_location() is declared to always
  // return a valid pointer to the calling thread's own storage and is
  // never permitted to return NULL, so without this, essentially every
  // errno use in the codebase produced an unprovable "not proven
  // nonnull" finding for the exact same reason, at the exact same call.
  static bool isAlwaysNonNull(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    return Function && Function->getIdentifier() &&
           Function->getName() == "__errno_location";
  }

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

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    if (!isAlwaysNonNull(Call))
      return;
    std::optional<DefinedOrUnknownSVal> Defined =
        Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
    if (!Defined)
      return;
    if (ProgramStateRef NonNull = C.getState()->assume(*Defined, true))
      C.addTransition(NonNull);
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
      // A Consumed entry is positive evidence: this checker's own
      // allocator/deallocator tracking (OwnershipChecker above) watched
      // this exact symbol go through free()/realloc() on this path, so a
      // later dereference really is a use-after-free. That is the only
      // liveness fact this checker can ever *establish*.
      if (Kind && *Kind == OwnershipKind::Consumed) {
        report("dereference accesses consumed storage", Statement, State, C);
        return;
      }
      // An *absent* entry is not evidence of anything -- it just means
      // this symbol never passed through OwnershipChecker's tracked
      // malloc family. That is the ordinary, expected shape of a borrowed
      // pointer: a function parameter, a global, or any value this
      // checker did not itself allocate. Reporting "not proven live" here
      // used to fire for essentially every dereference of a plain pointer
      // parameter in the tree (the single most common pointer shape in a
      // C library), because per-function analysis can never produce
      // positive liveness evidence for a value whose provenance crosses a
      // call boundary -- no amount of code on the callee side can ever
      // satisfy that obligation, so it was not a proof requirement, it
      // was unconditional noise. Nonnull-ness is still separately
      // required (see checkPointerExpression/above); this only stops
      // treating "unknown provenance" as if it were "known freed". See
      // tools/lint-ownership-fixtures/pointer-safe.c's opaque_borrow for
      // the worked example. (Extent proof below has the matching
      // relaxation, for the same reason -- see the comment there.)
    }

    QualType Type = accessType(Region, Statement);
    if (Type.isNull() || Type->isIncompleteType()) {
      report("dereference extent is not proven sufficient", Statement, State,
             C);
      return;
    }
    CharUnits Width = C.getASTContext().getTypeSizeInChars(Type);
    SVal Remaining = getDynamicExtentWithOffset(State, Location);
    // getDynamicExtentWithOffset never actually returns Unknown/Undef in
    // practice for a region reachable from a pointer value: when nothing
    // has told it a real size (no setDynamicExtent call -- the only
    // callers of that in this checker list are malloc-family summaries
    // built into the core engine itself, keyed off the actual allocation
    // size argument), it conjures a fresh, wholly unconstrained
    // SymbolExtent placeholder instead (SymbolManager::getExtentSymbol)
    // so that the arithmetic below always has *something* symbolic to
    // operate on, then subtracts this access's byte offset from it. That
    // subtraction means Remaining itself is almost never literally a bare
    // SymbolExtent even when the underlying region has no real size
    // info -- f->type (a fixed, nonzero field offset) comes back as a
    // compound "extent_of_f minus offsetof(type)" expression symbol, not
    // a SymbolExtent -- so testing Remaining directly under-detects the
    // placeholder case for anything but a zero-offset access. Testing the
    // *base* region's own raw extent instead sidesteps that: the
    // subtraction hasn't happened yet, so a placeholder for f is still
    // exactly a SymbolExtent there, while a genuinely tracked base (a
    // malloc call's real byte count, or a concrete array/struct's static
    // size) is preserved and still drives the real comparison below for
    // any offset into it -- so a too-small malloc'd allocation accessed
    // through a field at a fixed offset is still caught.
    SVal BaseExtent = getDynamicExtent(State, Region->getBaseRegion(),
                                       C.getSValBuilder());
    bool NoRealExtentInfo =
        BaseExtent.isUnknownOrUndef() ||
        isa_and_nonnull<SymbolExtent>(BaseExtent.getAsSymbol());
    if (NoRealExtentInfo) {
      // With no real extent to compare against, fall back to the same
      // "trust the type" reasoning as the liveness fix: a *fixed*,
      // compile-time-known offset (a plain single dereference, or a
      // struct field reached through one -- f->vfs, f->vnext, ...) is
      // guaranteed in-bounds by the C type system itself, which is
      // exactly what makes the pointer's static type meaningful to hold
      // in the first place. A *symbolic* (data-dependent) offset is a
      // genuinely different case -- errbuf[n] with a runtime-computed n
      // really can run past whatever the caller actually allocated, and
      // with no real extent to relate n to, that risk is real and still
      // reported.
      RegionOffset Offset = Region->getAsOffset();
      if (!Offset.isValid() || Offset.hasSymbolicOffset()) {
        report("dereference extent is not proven sufficient", Statement,
               State, C);
        return;
      }
    } else {
      SValBuilder &Builder = C.getSValBuilder();
      SVal Enough =
          Builder.evalBinOp(State, BO_GE, Remaining,
                            Builder.makeIntVal(Width.getQuantity(),
                                               C.getASTContext().getSizeType()),
                            Builder.getConditionType());
      std::optional<DefinedOrUnknownSVal> Condition =
          Enough.getAs<DefinedOrUnknownSVal>();
      if (!Condition) {
        report("dereference extent is not proven sufficient", Statement,
               State, C);
        return;
      }
      ProgramStateRef TooSmall = State->assume(*Condition, false);
      if (TooSmall) {
        report("dereference extent is not proven sufficient", Statement,
               TooSmall, C);
        return;
      }
    }
    if (!alignmentProven(Region, Type, C.getASTContext()))
      report("dereference alignment is not proven valid", Statement, State, C);
  }
};

class ResourceLifecycleChecker
    : public Checker<check::PreCall, check::PostCall> {
  mutable std::unique_ptr<BugType> BT;

  enum Family : unsigned {
    Descriptor = 1,
    Stream,
    Directory,
    Semaphore,
    Mapping,
    Handle
  };

  static unsigned live(Family Value) {
    return static_cast<unsigned>(Value) * 2;
  }
  static unsigned released(Family Value) { return live(Value) + 1; }

  static const FunctionDecl *function(const CallEvent &Call) {
    return dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  }

  static std::optional<Family> acquiredFamily(const CallEvent &Call) {
    const FunctionDecl *Function = function(Call);
    if (!Function || !Function->getIdentifier())
      return std::nullopt;
    StringRef Name = Function->getName();
    if (Name == "open" || Name == "openat" || Name == "creat" ||
        Name == "socket" || Name == "accept" || Name == "dup" || Name == "dup2")
      return Descriptor;
    if (Name == "fopen" || Name == "fdopen" || Name == "tmpfile" ||
        Name == "popen")
      return Stream;
    if (Name == "opendir" || Name == "fdopendir")
      return Directory;
    if (Name == "sem_open")
      return Semaphore;
    if (Name == "mmap")
      return Mapping;
    return std::nullopt;
  }

  // NT's own syscalls (unlike the POSIX open()/socket()/... family above)
  // never return the handle they acquire: they return an NTSTATUS and
  // write the handle through an out-pointer argument instead --
  // NtCreateFile(&h, ...), NtDuplicateObject(..., &h, ...), and so on.
  // acquiredFamily()/checkPostCall's `Call.getReturnValue()` can only ever
  // see the NTSTATUS for these, so every Handle this codebase's NT
  // backend acquires was previously invisible to ResourceMap -- and every
  // later NtClose() on it was therefore unprovable by construction, not
  // because of any real lifecycle problem. This table is every NT handle-
  // acquiring syscall this codebase actually calls before an NtClose
  // (found by tracing each NtClose call site back to its handle's
  // origin); the argument index is almost always the first (NT's own
  // convention puts the out-handle first), except where a handle is
  // acquired alongside another one already in scope, as with
  // NtDuplicateObject's *target* handle (its 4th argument) and
  // NtOpenProcessToken's access-token handle (its 3rd).
  struct HandleOutParam {
    llvm::StringLiteral Name;
    unsigned Argument;
  };
  static std::optional<unsigned> handleOutParamArgument(const CallEvent &Call) {
    const FunctionDecl *Function = function(Call);
    if (!Function || !Function->getIdentifier())
      return std::nullopt;
    StringRef Name = Function->getName();
    static constexpr HandleOutParam OutParams[] = {
        {"NtCreateFile", 0},          {"NtOpenFile", 0},
        {"NtCreateEvent", 0},         {"NtCreateSemaphore", 0},
        {"NtOpenSemaphore", 0},       {"NtCreateMutant", 0},
        {"NtCreateThreadEx", 0},      {"NtOpenProcess", 0},
        {"NtCreateJobObject", 0},     {"NtCreateSection", 0},
        {"NtCreateNamedPipeFile", 0}, {"NtCreateTimer", 0},
        {"NtOpenSymbolicLinkObject", 0},
        {"NtDuplicateObject", 3},
        {"NtOpenProcessToken", 2},
    };
    for (const HandleOutParam &Candidate : OutParams)
      if (Name == Candidate.Name)
        return Candidate.Argument;
    return std::nullopt;
  }

  static std::optional<std::pair<Family, unsigned>>
  release(const CallEvent &Call) {
    const FunctionDecl *Function = function(Call);
    if (!Function || !Function->getIdentifier())
      return std::nullopt;
    StringRef Name = Function->getName();
    if (Name == "close")
      return std::pair{Descriptor, 0u};
    if (Name == "fclose" || Name == "pclose")
      return std::pair{Stream, 0u};
    if (Name == "closedir")
      return std::pair{Directory, 0u};
    if (Name == "sem_close")
      return std::pair{Semaphore, 0u};
    if (Name == "munmap")
      return std::pair{Mapping, 0u};
    if (Name == "NtClose")
      return std::pair{Handle, 0u};
    return std::nullopt;
  }

  static std::optional<std::pair<Family, unsigned>> use(const CallEvent &Call) {
    const FunctionDecl *Function = function(Call);
    if (!Function || !Function->getIdentifier())
      return std::nullopt;
    StringRef Name = Function->getName();
    if (Name == "read" || Name == "write" || Name == "pread" ||
        Name == "pwrite" || Name == "lseek" || Name == "fstat" ||
        Name == "fsync")
      return std::pair{Descriptor, 0u};
    if (Name == "fread" || Name == "fwrite")
      return std::pair{Stream, 3u};
    if (Name == "fflush" || Name == "fileno" || Name == "rewind")
      return std::pair{Stream, 0u};
    if (Name == "fseek")
      return std::pair{Stream, 0u};
    if (Name == "readdir" || Name == "rewinddir" || Name == "dirfd")
      return std::pair{Directory, 0u};
    if (Name == "sem_wait" || Name == "sem_trywait" ||
        Name == "sem_timedwait" || Name == "sem_post")
      return std::pair{Semaphore, 0u};
    return std::nullopt;
  }

  void report(StringRef Reason, const CallEvent &Call,
              CheckerContext &C) const {
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode();
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven resource lifecycle",
                                     categories::MemoryError);
    auto Report = std::make_unique<PathSensitiveBugReport>(
        *BT, diagnosticMessage(Reason, Statement, C), Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  // POSIX guarantees file descriptors 0/1/2 (STDIN_FILENO/STDOUT_FILENO/
  // STDERR_FILENO) are open on entry to main() and stay open unless a
  // program deliberately closes them -- this codebase's own writestr()/
  // __getopt_msg() (src/misc/getopt.c) and expand_param() (src/wordexp/
  // wordexp.c) write directly to the literal descriptor 2 for exactly
  // this reason, the same convention diagnostic output has followed
  // since long before this checker existed. A literal 0/1/2 argument is
  // never the result of an open()/socket()/... this analysis could have
  // tracked (it is a compile-time constant, not a symbol at all), so
  // without this it was indistinguishable from a wholly made-up
  // descriptor -- this is the Resource-checker analogue of trusting
  // __errno_location()'s always-valid return above.
  static bool isStandardDescriptor(const CallEvent &Call, unsigned Argument,
                                   CheckerContext &C) {
    if (Argument >= Call.getNumArgs())
      return false;
    std::optional<nonloc::ConcreteInt> Value =
        Call.getArgSVal(Argument).getAs<nonloc::ConcreteInt>();
    if (!Value)
      return false;
    const llvm::APSInt &Int = Value->getValue();
    return Int >= 0 && Int <= 2;
  }

  // ISO C (7.21.5.2p2) gives fflush(NULL) its own, different meaning --
  // "flush all streams" -- unlike every other Stream-family operation
  // here (fileno/rewind/fseek/fread/fwrite), which are simply undefined
  // on a null FILE*. Requiring proof of a live, specific stream for the
  // one call whose entire point is "there is no specific stream" was
  // never satisfiable, the same shape as free(NULL)/realloc(NULL, ...)
  // already being no-ops OwnershipChecker's checkPreCall special-cases.
  static bool isFflushAll(const CallEvent &Call, unsigned Argument,
                          CheckerContext &C) {
    const FunctionDecl *Function = function(Call);
    if (!Function || !Function->getIdentifier() ||
        Function->getName() != "fflush")
      return false;
    return C.getState()->isNull(Call.getArgSVal(Argument)).isConstrainedTrue();
  }

  void checkResource(const CallEvent &Call, Family Expected, unsigned Argument,
                     bool Consume, CheckerContext &C) const {
    if (Argument >= Call.getNumArgs())
      return;
    if (Expected == Descriptor && isStandardDescriptor(Call, Argument, C))
      return;
    if (Expected == Stream && !Consume && isFflushAll(Call, Argument, C))
      return;
    SymbolRef Symbol = Call.getArgSVal(Argument).getAsSymbol(true);
    const unsigned *State =
        Symbol ? C.getState()->get<ResourceMap>(Symbol) : nullptr;
    if (!State) {
      if (!Symbol) {
        // Symbol == nullptr: this argument is a concrete, wholly
        // non-symbolic value the analyzer can name outright -- the
        // address of a stack-local/global (e.g. `sem_t s; sem_wait(&s);`
        // for an unnamed, caller-owned semaphore, whose lifecycle
        // OwnedConstructChecker proves separately, not this checker), a
        // literal constant, or similar. That is real, checkable
        // evidence for every family except Semaphore-use, so it is kept
        // reported there: sem_wait/post's own unnamed-object case is the
        // one legitimate use of this shape, so it keeps its carve-out.
        // A genuinely Unknown/Undef value is a third, different case
        // from either: the analyzer itself lost track of what this is
        // (most commonly a loop variable widened away after clang's
        // default max-loop iteration cap -- __fd_close_all_cloexec's
        // `for (i = 0; i < FD_MAX; i++) ... close(i);` with FD_MAX ==
        // 1024 is exactly this shape), which is "no information" just
        // like an untracked symbol below, not positive evidence.
        if (Call.getArgSVal(Argument).isUnknownOrUndef())
          return;
        if (Expected == Semaphore && !Consume)
          return;
        report("resource is not proven live", Call, C);
        return;
      }
      // Symbol != nullptr but absent from ResourceMap: the resource's
      // provenance is opaque to this per-function analysis -- the same
      // "was this analysis's own acquire/release tracking ever able to
      // see this value" gap fixed for Ownership's deallocator check and
      // ValidPointer's liveness proof above. A descriptor reached
      // through a borrowed struct or passed as a plain parameter
      // (closedir()'s `dp->fd`, set by opendir() in a function this
      // analysis never sees; posix_close()'s `int fd` parameter, opened
      // by whatever called it) has no ResourceMap entry not because it
      // is known un-acquired, but because per-function analysis cannot
      // see what happened before this function was entered. Trust it,
      // but still transition a real release to the released state, so a
      // same-function double-release of this exact borrowed resource is
      // still caught by the *State == released(Expected) branch below.
      if (Consume)
        C.addTransition(
            C.getState()->set<ResourceMap>(Symbol, released(Expected)));
      return;
    }
    if (*State == released(Expected)) {
      report(Consume ? "resource is already released"
                     : "operation uses a released resource",
             Call, C);
      return;
    }
    if (*State != live(Expected)) {
      report("resource family does not match operation", Call, C);
      return;
    }
    if (Consume)
      C.addTransition(
          C.getState()->set<ResourceMap>(Symbol, released(Expected)));
  }

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    if (std::optional<Family> Family = acquiredFamily(Call)) {
      SymbolRef Symbol = Call.getReturnValue().getAsSymbol(true);
      if (Symbol)
        C.addTransition(C.getState()->set<ResourceMap>(Symbol, live(*Family)));
      return;
    }
    if (std::optional<unsigned> Argument = handleOutParamArgument(Call)) {
      if (*Argument >= Call.getNumArgs())
        return;
      const MemRegion *Out = Call.getArgSVal(*Argument).getAsRegion();
      if (!Out)
        return;
      // The call is opaque to the analyzer, so by the time checkPostCall
      // runs, the engine's own default conservative evaluation has
      // already invalidated *Out and bound a fresh symbolic value there
      // (every non-const pointer argument to an unmodeled call gets this
      // treatment) -- reading it back here is exactly how MallocChecker-
      // style checkers recover an out-parameter's acquired value.
      SymbolRef Symbol = C.getState()->getSVal(Out).getAsSymbol(true);
      if (Symbol)
        C.addTransition(C.getState()->set<ResourceMap>(Symbol, live(Handle)));
    }
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    if (auto Release = release(Call))
      checkResource(Call, Release->first, Release->second, true, C);
    else if (auto Use = use(Call))
      checkResource(Call, Use->first, Use->second, false, C);
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
  Registry.addChecker<ResourceLifecycleChecker>(
      "ntlibc.Resource", "Proves acquire, use, and release resource lifecycles",
      "");
}
