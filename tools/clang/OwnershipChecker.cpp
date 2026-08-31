// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Attr.h"
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
#include "llvm/ADT/DenseMap.h"

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

  // Clang's own dynamic-extent tracking for an allocator's return value
  // only fires for a handful of literally-named standard functions
  // (confirmed empirically against clang 18: `malloc(n)` gets a real,
  // usable dynamic extent; `__malloc(n)` -- the name every allocation
  // inside this tree's OWN code actually goes through, since `malloc`
  // itself is just this codebase's own public wrapper around it -- does
  // not, leaving ValidPointerChecker with nothing but an unconstrained
  // SymbolExtent placeholder for every buffer this codebase allocates
  // through its own internal entry point). isAllocator() above already
  // recognizes this whole family for ownership-tracking purposes and
  // already has the real call in hand, so it can set the region's real
  // dynamic extent itself, straight from the real size argument(s) --
  // exactly the fact clang's own builtin modeling would have recorded had
  // the function been literally named "malloc"/"calloc"/etc. This is not
  // a new assumption layered on top of what the program does: it is the
  // exact byte count the allocator itself is about to hand back, read
  // directly off the arguments of the call that produced it.
  //
  // strdup/strndup are deliberately left alone: their real size depends
  // on the *content* of a string argument (strlen, or a strnlen capped by
  // a second argument), not a value already sitting in a register at the
  // call site the way every other allocator's size is, so there is no
  // argument SVal here that IS the answer the way there is for the rest
  // of this family.
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
    if (std::optional<SVal> SizeInBytes = allocationSizeInBytes(Call, C)) {
      if (std::optional<DefinedOrUnknownSVal> DefinedSize =
              SizeInBytes->getAs<DefinedOrUnknownSVal>()) {
        if (const MemRegion *Region = ReturnValue.getAsRegion())
          State = setDynamicExtent(State, Region->getBaseRegion(),
                                   *DefinedSize, C.getSValBuilder());
      }
    }
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

  // True when Region's identity itself crosses a call boundary this
  // per-function analysis cannot see through -- the exact same "was this
  // value ever visible to my own tracking" gap already fixed for
  // OwnershipChecker::checkPreCall's deallocator argument and
  // ResourceLifecycleChecker::checkResource's liveness proof (see their
  // own comments), just never applied to construct lifecycles. A
  // SymbolicRegion base means this object's address came in as an
  // opaque, borrowed pointer -- overwhelmingly a plain parameter, e.g.
  // pthread_cond_wait's own `mutex`, or sem_timedwait's `sem`, both of
  // which POSIX requires the CALLER to have already initialized, in code
  // this per-function analysis never sees at all (a different TU
  // entirely, in the general case). ConstructMap can only ever gain an
  // entry for a construct by watching THIS analysis's own
  // pthread_*_init()/sem_init() call it directly, so an absent entry for
  // a borrowed object is not evidence it was never initialized, it is
  // simply the ordinary, expected shape of "someone else's problem to
  // have set up". A concrete VarRegion/local or global, by contrast, is
  // an object this analysis DOES see the entire lifetime of within the
  // current function, so an absent entry there remains real, checkable
  // evidence of a genuinely never-initialized on-stack synchronization
  // object -- that case is unchanged and still reported.
  static bool isOpaqueBorrow(const MemRegion *Region) {
    return Region && Region->getSymbolicBase() != nullptr;
  }

  void requireLive(const CallEvent &Call, unsigned Argument,
                   ConstructFamily Family, CheckerContext &C) const {
    const MemRegion *Region = argumentRegion(Call, Argument);
    if (!Region ||
        C.getState()->isNull(Call.getArgSVal(Argument)).isConstrainedTrue())
      return;
    const ConstructKind *Kind = C.getState()->get<ConstructMap>(Region);
    if (!Kind &&
        (hasStaticInitialization(Region, Family) || isOpaqueBorrow(Region)))
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
      // Deliberately NOT extended with isOpaqueBorrow here: unlike the
      // "not proven initialized" check below, "no information" must
      // stay "no information" for a double-construct proof specifically
      // -- trusting an opaque borrow as evidence of "definitely already
      // live" would risk hiding a real double pthread_mutex_init() on a
      // borrowed pointer, which is exactly backwards. This path already
      // does not misreport an opaque borrow as "already initialized"
      // today (StaticLive is false for a SymbolicRegion, since
      // hasStaticInitialization only matches a VarRegion), so there is
      // nothing to fix on this branch.
      if ((Kind && *Kind == ConstructKind::Live) || StaticLive)
        report("owned construct is already initialized", Statement,
               C.getState(), C);
      return;
    }
    if (!Kind && !StaticLive && !isOpaqueBorrow(Region)) {
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
                     check::PostCall, check::BeginFunction> {
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
  //
  // __teb() (src/internal/libc.h: `PTEB __teb(void);`) is the same shape
  // for NT: it reads the current thread's Thread Environment Block via
  // the GS/FS segment (x86_64/i386) or TPIDR register (aarch64), which
  // the OS itself guarantees exists for any running thread -- there is
  // no code path, on any arch this tree supports, where a live thread
  // observes its own TEB as absent. crt/crt1.c's __libc_start_main uses
  // exactly this fact to bootstrap __peb itself (see
  // isAlwaysNonNullGlobal below) before anything else in the program has
  // run: `__peb = __teb()->ProcessEnvironmentBlock;`.
  //
  // localeconv() (src/misc/locale.c) is the same shape again, one file
  // over: `return &__posix_lconv;`, unconditional, the address of a
  // fixed static object, with no other return statement anywhere in
  // its one real definition. src/misc/langinfo.c's own
  // RADIXCHAR/THOUSEP cases (`localeconv()->decimal_point`,
  // `localeconv()->thousands_sep`) are a different translation unit,
  // so this checker's own per-TU analysis has no way to see that body
  // and prove it from first principles the way it could within
  // locale.c itself -- exactly the cross-file gap __errno_location and
  // __teb already needed this same mechanism for.
  static bool isAlwaysNonNull(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return false;
    StringRef Name = Function->getName();
    return Name == "__errno_location" || Name == "__teb" ||
           Name == "localeconv";
  }

  // __peb (src/internal/libc.h: `extern PPEB __peb;`) is a plain global
  // pointer, not a call result, so isAlwaysNonNull's checkPostCall-based
  // mechanism cannot cover it -- it is set exactly once, unconditionally,
  // in crt/crt1.c's __libc_start_main, from __teb()->ProcessEnvironmentBlock
  // (itself always-valid, see isAlwaysNonNull above) before any other
  // code in the program runs, and nothing anywhere in this tree ever
  // reassigns or clears it afterward. That makes every later dereference
  // of __peb (dlfcn.c's __peb->ImageBaseAddress, plat_malloc.c's
  // __peb->ProcessHeap used by every malloc/free/realloc on NT, ...) the
  // exact same "always valid, but not something this per-function
  // analysis can derive from its own tracking" shape as __errno_location,
  // just expressed as a global's identity instead of a call's return
  // value. This is checked structurally (a DeclRefExpr naming this one
  // specific, by-name-identified global) rather than through SVal/region
  // state, because unlike a call result there is no "after this call"
  // point to assume the fact at -- the value already exists in the
  // global's storage by the time any TU's code runs.
  static bool isAlwaysNonNullGlobal(const Expr *PointerExpr) {
    const auto *Ref = dyn_cast<DeclRefExpr>(PointerExpr->IgnoreParenCasts());
    if (!Ref)
      return false;
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    return Variable && Variable->getIdentifier() &&
           Variable->hasGlobalStorage() && Variable->getName() == "__peb";
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

  // For a[i] into a fixed-size, compile-time-known array `a`, prove the
  // access in-bounds by comparing the index directly against the array's
  // own element count, instead of through the generic byte-extent
  // machinery below. That machinery computes "bytes remaining" as
  // extent_of_a_in_bytes MINUS i*sizeof(element) -- an entirely correct
  // but *compound*, derived symbolic expression -- and then asks the
  // constraint solver whether that compound value can be proven >=
  // sizeof(element). clang's default range-based solver reasons well
  // about a single symbol's own range (exactly what a guard like
  // src/exit/exit.c's atexit() -- `if (nhandlers >= ATEXIT_CAP_) return
  // -1; handlers[nhandlers++] = f;` -- establishes directly: nhandlers
  // < ATEXIT_CAP_) but does not generally re-derive that same fact once
  // it has been folded into a multiplication/subtraction over a fresh
  // symbol -- so a genuinely bounds-checked write into a real, fixed-
  // size array was reported as if the check had never happened. Asking
  // the exact question the guard itself answered (is the raw index
  // symbol below the array's own element count?) is precisely what the
  // solver handles well, so this only helps the shape that is provable
  // by construction, and returns false (falling through to the existing
  // machinery, unchanged) for anything it cannot establish outright --
  // including every heap-allocated "array" (a struct field's calloc'd
  // buffer, whose real capacity was fixed by an argument to a *different*
  // call this per-function analysis cannot see) reached only through a
  // pointer, which has no compile-time array type to compare against at
  // all.
  static bool arrayIndexProvenInBounds(const ElementRegion *Element,
                                       ProgramStateRef State,
                                       CheckerContext &C) {
    const auto *Super =
        dyn_cast<TypedValueRegion>(Element->getSuperRegion());
    if (!Super)
      return false;
    const ConstantArrayType *ArrayType =
        C.getASTContext().getAsConstantArrayType(Super->getValueType());
    if (!ArrayType)
      return false;
    SVal Index = Element->getIndex();
    std::optional<DefinedOrUnknownSVal> DefinedIndex =
        Index.getAs<DefinedOrUnknownSVal>();
    if (!DefinedIndex)
      return false;
    QualType IndexType = Index.getType(C.getASTContext());
    if (IndexType.isNull() || !IndexType->isIntegralOrEnumerationType())
      return false;
    SValBuilder &Builder = C.getSValBuilder();
    SVal Count = Builder.makeIntVal(ArrayType->getSize().getZExtValue(),
                                    IndexType);
    SVal Below =
        Builder.evalBinOp(State, BO_LT, *DefinedIndex, Count,
                          Builder.getConditionType());
    std::optional<DefinedOrUnknownSVal> BelowCondition =
        Below.getAs<DefinedOrUnknownSVal>();
    if (!BelowCondition)
      return false;
    // If assuming "index is at or past the count" is itself feasible,
    // the bound is not proven -- fall through to the existing machinery
    // rather than claim a fact that is not actually established.
    if (State->assume(*BelowCondition, false))
      return false;
    if (IndexType->isSignedIntegerOrEnumerationType()) {
      SVal NonNegative =
          Builder.evalBinOp(State, BO_GE, *DefinedIndex,
                            Builder.makeIntVal(0, IndexType),
                            Builder.getConditionType());
      std::optional<DefinedOrUnknownSVal> NonNegativeCondition =
          NonNegative.getAs<DefinedOrUnknownSVal>();
      if (!NonNegativeCondition)
        return false;
      if (State->assume(*NonNegativeCondition, false))
        return false;
    }
    return true;
  }

  // For `buf[i]` into a HEAP-allocated buffer (a SymbolicRegion, so no
  // compile-time ConstantArrayType exists for arrayIndexProvenInBounds
  // above to use) whose real dynamic extent was set -- by this checker
  // itself, see OwnershipChecker::allocationSizeInBytes -- directly from
  // an allocation call's own size ARGUMENT expression (e.g. `malloc(n +
  // 1)`), prove `buf[i]` in-bounds when `i` is EXACTLY that same
  // argument expression's own root symbol: `buf[n]`, the single most
  // common "allocate len+1, write the terminator at len" idiom
  // throughout this tree (strndup.c's `d = malloc(l+1); ...; d[l] = 0;`
  // is the concrete case this was developed against, and clears
  // completely with this fix). The generic byte-extent machinery below
  // computes this exact same relationship -- extent_of_buf (itself
  // `n + 1`, already a compound expression) MINUS the access offset
  // (`n`) -- but clang's range-based constraint solver does not fold
  // "(S + K) - S" down to the literal K for two separately-built
  // compound expressions that merely happen to share a root symbol; it
  // proves a single symbol's own affine range well (arrayIndexProvenInBounds
  // above already exploits exactly that), but not this kind of
  // cross-expression cancellation. Confirmed empirically while
  // developing this fix: SValBuilder::evalBinOp leaves the subtraction
  // unsimplified (still a compound SymSymExpr), and even an explicit
  // follow-up assume() on the resulting comparison cannot refute the
  // "too small" case -- the solver is not merely missing an
  // optimization here, it structurally cannot correlate two affine
  // expressions built from the same symbol without recognizing the
  // syntactic identity itself, which is exactly what this function does
  // instead, with plain integer arithmetic that needs no solver help at
  // all once the two expressions are known to share a root symbol.
  // Deliberately narrow in the byte-stride dimension: only a byte-stride
  // (`char`) element type is handled, since that is the only case where
  // the index and the allocation's own size argument are expressed in
  // the same units without a further multiplication this function does
  // not attempt to peel through (a `wchar_t` buffer sized as
  // `(n+1)*sizeof(WCHAR)` falls through to the existing machinery,
  // unchanged). It ORIGINALLY also required the exact same bare symbol
  // on both sides (only a `+` between that one symbol's root and a
  // literal, nothing else) -- collectLinearTerms()/linearExtentProvenInBounds()
  // below generalize that part to any number of summed/subtracted
  // symbols on either side, folded via ordinary linear-term
  // cancellation instead of a single pointer-identity comparison; see
  // that function's own comment for why and for the concrete callers
  // that need it. A provably-equal-but-differently-DERIVED symbol (two
  // separate calls that happen to compute the same value) is still not
  // recognized by either version -- src/internal/linux/handle_path.c's
  // `r = __malloc((size_t)n + 1); if (n) memcpy(...); r[n] = 0;` still
  // reports on its `n == 0` branch (where the index is concretized to
  // the literal 0 rather than staying the symbol `n`), a real remaining
  // gap neither version closes; see the ownership-lemma commit message
  // for why that narrower residual was left rather than chased further.
  // A plain, unchecked width/signedness conversion (`(size_t)n` at the
  // allocation call vs. the raw `long n` used again as the index, also
  // from handle_path.c) wraps the same underlying symbol in a
  // SymbolCast, which is a genuinely different SymExpr object from the
  // bare symbol -- so a pointer-identity comparison between the two
  // sides needs to see through any such casts on either side to
  // recognize they still name the same value (this part of that file's
  // shape IS handled, by stripCasts below, called from within
  // collectLinearTerms() too).
  static SymbolRef stripCasts(SymbolRef Symbol) {
    while (const auto *Cast = dyn_cast_or_null<SymbolCast>(Symbol))
      Symbol = Cast->getOperand();
    return Symbol;
  }

  // Generalizes the single-symbol cancellation above (the original
  // shape this was built for was strictly "extent = S + K, index = S")
  // to the far more common real shape in this tree's own path-handling
  // code: an allocation sized from the SUM of two or more independent
  // length symbols, indexed by an expression that reuses only SOME of
  // them. src/env/setenv.c's `s = malloc(l1 + l2 + 2); ...; s[l1] =
  // '=';` (a name, a '=', a value and a NUL) and
  // src/internal/rpath.c's join() -- `p = __malloc(dl + 1 + tl + 1);
  // ...; p[dl] = '\\'; ...; p[dl + 1 + tl] = 0;` (a directory, a
  // separator, a tail and a NUL) are both exactly this: the extent is
  // "index's own symbols, PLUS at least one more nonnegative term",
  // which is provably sufficient by plain arithmetic once the shared
  // symbols are identified and cancelled -- no different in kind from
  // the S+K case, just with more terms on one or both sides. Recognizing
  // this syntactically (as the S+K lemma above already does for its own
  // narrower shape) needs no solver help either.
  //
  // collectLinearTerms() walks a SymExpr built purely from BO_Add/BO_Sub
  // over other SymExprs and integer literals -- which is exactly what
  // every size/offset expression in this idiom is built from, since
  // nothing here multiplies two symbolic lengths together -- and reduces
  // it to a normalized "symbol -> net signed coefficient" map plus a net
  // integer constant. A node this cannot decompose (a multiplication, a
  // call result, ...) is folded in as one opaque atomic term instead of
  // being silently dropped, so it can still cancel by pointer identity
  // against the identical opaque subexpression on the other side, but
  // can never be treated as a free pass the way a genuine summed symbol
  // is; ElemWidth stays restricted to a byte stride for the same reason
  // as before (a `wchar_t` buffer's `(n+1) * sizeof(WCHAR)` extent has a
  // BO_Mul node neither this nor the old lemma peels through).
  static void collectLinearTerms(SymbolRef Sym, bool Negate,
                                 llvm::DenseMap<SymbolRef, int> &Terms,
                                 int64_t &Constant) {
    Sym = stripCasts(Sym);
    if (const auto *IntExpr = dyn_cast<SymIntExpr>(Sym)) {
      BinaryOperator::Opcode Op = IntExpr->getOpcode();
      if (Op == BO_Add || Op == BO_Sub) {
        collectLinearTerms(IntExpr->getLHS(), Negate, Terms, Constant);
        int64_t Rhs = IntExpr->getRHS().getExtValue();
        if (Op == BO_Sub)
          Rhs = -Rhs;
        Constant += Negate ? -Rhs : Rhs;
        return;
      }
    } else if (const auto *SymExprB = dyn_cast<SymSymExpr>(Sym)) {
      BinaryOperator::Opcode Op = SymExprB->getOpcode();
      if (Op == BO_Add || Op == BO_Sub) {
        collectLinearTerms(SymExprB->getLHS(), Negate, Terms, Constant);
        collectLinearTerms(SymExprB->getRHS(),
                           Op == BO_Sub ? !Negate : Negate, Terms, Constant);
        return;
      }
    }
    Terms[Sym] += Negate ? -1 : 1;
  }

  // Strictly more general than the old sameSymbolExtentProvenInBounds
  // (folded into this function): "extent = S + K, index = S" is just
  // the case where every term cancels to zero except the constant, which
  // this reaches the same way, with no special-casing needed -- perfect
  // cancellation never depends on any symbol's sign, only a leftover
  // term does. A leftover term is only trusted when it is a symbol whose
  // own type is unsigned (so it cannot be negative by construction --
  // every length/offset symbol this idiom ever sums is a size_t) and its
  // net coefficient is positive (subtracted more than it was added is
  // never trusted, since that could shrink the real remaining space by
  // an amount this function has no way to bound).
  // getDynamicExtent() always answers in BYTES, but a NON-byte element
  // array's own index (`ne[n]`) is naturally expressed in ELEMENTS, not
  // bytes -- so the two are not directly comparable the way the
  // byte-stride case above compares them. This codebase's other
  // extremely common allocation idiom is exactly this mismatch:
  // `realloc(p, sizeof(*p) * (n + K))` growing a POINTER (or struct)
  // array rather than a byte buffer -- src/env/setenv.c's `ne =
  // realloc(__environ, sizeof(char *) * (n + 2)); ne[n] = s; ne[n + 1]
  // = 0;` and putenv()'s `putenv_strings = realloc(..., sizeof(char *)
  // * (nputenv + 1)); putenv_strings[nputenv++] = s;` are both this
  // shape. Peeling a top-level `ElemWidth * (...)` factor off the
  // extent expression converts it back to the same element-count units
  // the index is already naturally in, after which the exact same
  // linear-term cancellation below applies unchanged -- the required
  // remaining amount is then simply "at least 1 more element", not "at
  // least Width more bytes". SValBuilder always normalizes a
  // symbol-times-constant product into a SymIntExpr (RHS the literal),
  // regardless of the multiplication's spelling order in the source, so
  // checking only that shape is not an extra restriction here.
  static SymbolRef peelElementWidthFactor(SymbolRef Sym, CharUnits ElemWidth) {
    Sym = stripCasts(Sym);
    const auto *IntExpr = dyn_cast<SymIntExpr>(Sym);
    if (!IntExpr || IntExpr->getOpcode() != BO_Mul)
      return nullptr;
    if (IntExpr->getRHS().getExtValue() != ElemWidth.getQuantity())
      return nullptr;
    return IntExpr->getLHS();
  }

  static bool linearExtentProvenInBounds(const ElementRegion *Element,
                                         SVal BaseExtent, CharUnits Width,
                                         CheckerContext &C) {
    SymbolRef ExtentSym = BaseExtent.getAsSymbol();
    if (!ExtentSym)
      return false;
    CharUnits ElemWidth =
        C.getASTContext().getTypeSizeInChars(Element->getElementType());
    // In bytes for the ElemWidth == 1 case (Width IS the byte count
    // needed); in elements (always exactly 1: "the accessed element
    // itself") once ElemWidth has been peeled off below.
    int64_t Required = Width.getQuantity();
    if (ElemWidth.getQuantity() != 1) {
      SymbolRef Peeled = peelElementWidthFactor(ExtentSym, ElemWidth);
      if (!Peeled)
        return false;
      ExtentSym = Peeled;
      Required = 1;
    }
    SVal Index = Element->getIndex();
    SymbolRef IndexSym = Index.getAsSymbol();
    if (!IndexSym)
      return false;

    llvm::DenseMap<SymbolRef, int> Terms;
    int64_t Constant = 0;
    collectLinearTerms(ExtentSym, false, Terms, Constant);
    collectLinearTerms(IndexSym, true, Terms, Constant);

    for (const auto &Entry : Terms) {
      if (Entry.second == 0)
        continue;
      if (Entry.second < 0)
        return false;
      QualType SymType = Entry.first->getType();
      if (SymType.isNull() || !SymType->isUnsignedIntegerOrEnumerationType())
        return false;
    }
    return Constant >= 0 && static_cast<uint64_t>(Constant) >=
                                static_cast<uint64_t>(Required);
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
    if (isAlwaysNonNullGlobal(Pointer))
      return;
    // Reinterpreting an already-nonnull pointer through a pointer-to-
    // pointer cast never turns it into a null one, but evaluating the
    // CAST expression's own SVal loses that fact: src/stdio/printf.c's
    // and scanf.c's shared gf() macro reads one format character as
    // `*(q)` for a byte format or `*(const wchar_t *)(const void *)(q)`
    // for a wide one (the cast lets one parser serve both fprintf() and
    // fwprintf()), where `q` is a cursor walked across an
    // already-nonnull `fmt`/`fp` by `q += st` each iteration. Only the
    // CAST side was ever flagged "not proven nonnull" -- the identical
    // `q`, dereferenced without the cast a few lines away in the very
    // same loop, was not -- which isolated the cast, not the cursor
    // arithmetic, as what breaks the proof: evaluating the SVal of a
    // BitCast/NoOp pointer-to-pointer CastExpr does not, in general,
    // preserve the symbolic region identity (and so the nonnull fact
    // already established for it) that evaluating its sub-expression
    // directly does. Confirmed empirically against four minimal repros
    // before touching this file: a bare `*q` after `q += st` (symbolic
    // stride) proves fine; the identical cursor dereferenced through
    // `*(wchar_t_fake *)q` does not; and looking through the cast fixes
    // it without hiding a real bug -- `*(T *)p` for a `p` that is
    // actually unconstrained, or explicitly null (`char *p = 0; *(T
    // *)(void *)p`), is still caught, both by this checker (the fix
    // only changes what EvalExpr designates, not whether isNonNull is
    // asked about it) and independently by clang's own core.NullDereference.
    // Deliberately narrow: only CK_BitCast/CK_NoOp are looked through
    // (never CK_LValueToRValue -- an earlier version of this fix walked
    // into that too and started treating every unconstrained raw
    // parameter as nonnull, because it ended up evaluating the SVal of
    // the pointer VARIABLE's own storage location instead of the
    // pointer VALUE stored there, which is trivially "nonnull" as any
    // local's address always is; the fixture suite below (the same
    // pointer-safe.c/pointer-unsafe.c fixtures every other lemma here is
    // checked against) is what caught that), and only when the
    // sub-expression is itself of pointer type, so a cast that changes
    // value category or turns an integer into a pointer is left alone.
    const Expr *EvalExpr = Pointer;
    for (;;) {
      const auto *Cast = dyn_cast<CastExpr>(EvalExpr->IgnoreParens());
      if (!Cast)
        break;
      CastKind Kind = Cast->getCastKind();
      if (Kind != CK_BitCast && Kind != CK_NoOp)
        break;
      if (!Cast->getSubExpr()->getType()->isPointerType())
        break;
      EvalExpr = Cast->getSubExpr();
    }
    SVal Value = C.getSVal(EvalExpr);
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

  // GCC/Clang's own `nonnull` attribute (`__attribute__((nonnull(N,...)))`,
  // or no argument list at all, meaning every pointer parameter) is the
  // C ecosystem's standard, general-purpose way to say exactly the fact
  // this whole checker otherwise has no way to learn about an ordinary
  // parameter: that it is a REQUIRED, non-optional pointer by the
  // function's own real, published contract, not a value the callee is
  // ever expected to validate. Real compilers already understand it (GCC
  // and Clang both diagnose a provably-NULL argument at a call site under
  // -Wnonnull), so recognizing it here piggybacks on a fact this project
  // is expected to state truthfully in its own headers anyway, rather
  // than inventing a checker-only heuristic. This assumes each nonnull
  // parameter is proven at function entry, the same way an explicit
  // `if (!p) return;` guard would establish it -- the difference is that
  // the guard the analyzer would otherwise need is the caller's job, not
  // this function's, per the attribute's own meaning.
  void checkBeginFunction(CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function)
      return;
    const auto *NonNull = Function->getAttr<NonNullAttr>();
    if (!NonNull)
      return;
    ProgramStateRef State = C.getState();
    const LocationContext *LC = C.getLocationContext();
    bool Changed = false;
    unsigned Index = 0;
    for (const ParmVarDecl *Param : Function->parameters()) {
      unsigned ThisIndex = Index++;
      if (!Param->getType()->isPointerType() || !NonNull->isNonNull(ThisIndex))
        continue;
      SVal ParamValue = State->getSVal(State->getLValue(Param, LC));
      std::optional<DefinedOrUnknownSVal> Defined =
          ParamValue.getAs<DefinedOrUnknownSVal>();
      if (!Defined)
        continue;
      if (ProgramStateRef NonNullState = State->assume(*Defined, true)) {
        State = NonNullState;
        Changed = true;
      }
    }
    if (Changed)
      C.addTransition(State);
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
    if (const auto *Element = dyn_cast<ElementRegion>(Region)) {
      if (arrayIndexProvenInBounds(Element, State, C)) {
        if (!alignmentProven(Region, Type, C.getASTContext()))
          report("dereference alignment is not proven valid", Statement,
                 State, C);
        return;
      }
    }
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
    if (!NoRealExtentInfo) {
      if (const auto *Element = dyn_cast<ElementRegion>(Region)) {
        if (linearExtentProvenInBounds(Element, BaseExtent, Width, C)) {
          if (!alignmentProven(Region, Type, C.getASTContext()))
            report("dereference alignment is not proven valid", Statement,
                   State, C);
          return;
        }
      }
    }
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
      // A *fixed*, compile-time-known offset (a plain single dereference,
      // or a struct field reached through one) gets the same "trust the
      // type" leniency here as the NoRealExtentInfo branch above, once
      // OwnershipChecker::allocationSizeInBytes started giving this
      // checker's own allocator family (__malloc, calloc, realloc, ...)
      // real tracked extents rather than leaving them as placeholders:
      // a real extent is very often *itself* a compound, data-dependent
      // expression (`sizeof(struct foo) + extra`, `n * width`, ...), and
      // the fixed-offset access's "Remaining >= Width" comparison
      // inherits that same compound-subtraction shape
      // sameSymbolExtentProvenInBounds exists to work around for the
      // matching-symbol case above -- but a plain fixed field offset
      // essentially never matches that narrow pattern, so before this
      // adjustment, giving __malloc-family allocations real extents
      // regressed every fixed-offset access into one from "trusted by
      // type" (no real extent existed to contradict it) to "unprovable,
      // so reported" (a real, compound extent now exists, but the
      // solver can't relate it to the fixed offset) -- confirmed
      // empirically while developing this fix: src/internal/nt/path.c's
      // `*p`/`b[0..6]`-style fixed-offset accesses into `__malloc`'d
      // buffers newly regressed from proven to reported the moment
      // real extent tracking was added, with no code change of their
      // own. The fix is asymmetric on purpose, matching 0402bed's own
      // reasoning for the placeholder case: only report a fixed-offset
      // access when the real tracked extent makes sufficiency PROVABLY
      // IMPOSSIBLE (`assume(Enough, true)` itself refuted) -- not merely
      // when sufficiency isn't provable -- so a genuinely too-small
      // allocation reached through a fixed field offset (0402bed's own
      // "malloc(4) accessed through an 8-byte field" shape, where the
      // extent's real value is concrete or otherwise fully resolvable)
      // is still caught, exactly as before.
      RegionOffset Offset = Region->getAsOffset();
      bool FixedOffset = Offset.isValid() && !Offset.hasSymbolicOffset();
      if (!Condition) {
        if (!FixedOffset) {
          report("dereference extent is not proven sufficient", Statement,
                 State, C);
          return;
        }
      } else if (FixedOffset) {
        if (!State->assume(*Condition, true)) {
          report("dereference extent is not proven sufficient", Statement,
                 State, C);
          return;
        }
      } else {
        ProgramStateRef TooSmall = State->assume(*Condition, false);
        if (TooSmall) {
          report("dereference extent is not proven sufficient", Statement,
                 TooSmall, C);
          return;
        }
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

  // A hard-coded integer literal at the call site (resource-unsafe.c's
  // bogus_literal: `write(99, "x", 1)`) is real, checkable evidence that
  // this descriptor was authored out of thin air -- it is not, and could
  // never be, the result of any open()/socket()/... this analysis could
  // have tracked. A *computed* argument that merely evaluates concrete
  // on some explored path is a different claim entirely: the single most
  // common shape is a bounded `for` loop's own induction variable, e.g.
  // src/internal/fd.c's __fd_close_all_cloexec():
  //   for (i = 0; i < FD_MAX; i++)
  //     if (__fds[i].h && (__fds[i].flags & O_CLOEXEC)) close(i);
  // clang's analyzer explores only a handful of concrete values of `i`
  // before giving up and widening it to a fresh, unconstrained symbol
  // (FD_MAX == 1024) -- on those first few concrete iterations, `i` is
  // indistinguishable from a hand-written literal by SVal alone, even
  // though the source never wrote any such number down, and the loop's
  // own `__fds[i].h` guard is real, checkable evidence (of exactly the
  // same "someone else's acquire, invisible to this per-function
  // analysis" shape as a borrowed parameter) that whatever integer `i`
  // is on this path names a live descriptor this process's own table
  // says is open. The two are only distinguishable at the AST level --
  // by whether the argument expression is itself the literal, or merely
  // a variable/expression the analyzer's own limited exploration reduced
  // to a concrete value -- so that is what this checks, instead of the
  // SVal's concreteness.
  //
  // Deliberately scoped to Descriptor only: unlike Semaphore/Stream (see
  // the Stream/Semaphore-use carve-out in checkResource below), the file
  // descriptor namespace has exactly one acquire surface (open/socket/
  // accept/dup/...) and exactly one release function (close()), so there
  // is no "used the wrong release API for this concretely-addressed
  // object" hazard (sem_close() on an unnamed, sem_init()'d semaphore,
  // say) that a broader-than-literal trust could hide.
  static bool isLiteralArgument(const CallEvent &Call, unsigned Argument) {
    const Expr *ArgExpr = Call.getArgExpr(Argument);
    if (!ArgExpr)
      return false;
    ArgExpr = ArgExpr->IgnoreParenCasts();
    if (const auto *Unary = dyn_cast<UnaryOperator>(ArgExpr))
      if (Unary->getOpcode() == UO_Minus || Unary->getOpcode() == UO_Plus)
        ArgExpr = Unary->getSubExpr()->IgnoreParenCasts();
    return isa<IntegerLiteral>(ArgExpr);
  }

  // A resource read back through a data-dependent (symbolic) array index
  // -- src/sh/execute.c's __sh_exec_pipeline(), closing `pipes[i][1]`
  // inside a `for (i = 0; i < n; i++)` loop over each pipeline stage's
  // own pipe, is the concrete case that motivated this -- is a claim
  // this checker's per-symbol ResourceMap structurally cannot evaluate,
  // for a reason one level deeper than every other "no information"
  // case above: once such a loop's index is widened past its first few
  // concrete iterations, clang's RegionStore models a symbolic-index
  // ElementRegion read with one shared "default value" representative
  // for the *entire* array, not one distinct symbol per logical element
  // -- so `pipes[i][1]` at one loop iteration and `pipes[i][1]` at a
  // later, logically different iteration (a different pipeline stage
  // entirely) can resolve to the exact same SymbolRef purely as an
  // artifact of the memory model, not because they are really the same
  // resource. __sh_exec_pipeline() closes each pipeline index's ends in
  // exactly one of its two passes -- pass 1 for a real (SH_CMD_SIMPLE)
  // stage, pass 2 for a deferred compound-command stage -- gated by a
  // `deferred[]` array set once, before either pass runs, and never
  // revisited; genuinely correct, but a correlation between "which index
  // this iteration is" and "which pass already closed it" that this
  // per-symbol tracking has no way to see either way, on top of no
  // longer even being able to name the two array elements distinctly.
  // Trusted the same way any other "no information" shape is -- neither
  // direction (acquired-but-not-seen, or released-and-then-reused) is
  // provable when the underlying representation itself cannot tell two
  // different elements apart, so this returns before the state lookup,
  // for every resource operation on such an argument, symmetrically.
  static bool hasSymbolicArrayIndex(const Expr *ArgExpr, CheckerContext &C) {
    ArgExpr = ArgExpr->IgnoreParenCasts();
    const auto *Subscript = dyn_cast<ArraySubscriptExpr>(ArgExpr);
    if (!Subscript)
      return false;
    SVal Index = C.getSVal(Subscript->getIdx());
    if (!Index.getAs<nonloc::ConcreteInt>())
      return true;
    return hasSymbolicArrayIndex(Subscript->getBase(), C);
  }

  void checkResource(const CallEvent &Call, Family Expected, unsigned Argument,
                     bool Consume, CheckerContext &C) const {
    if (Argument >= Call.getNumArgs())
      return;
    if (Expected == Descriptor && isStandardDescriptor(Call, Argument, C))
      return;
    if (Expected == Stream && !Consume && isFflushAll(Call, Argument, C))
      return;
    if (const Expr *ArgExpr = Call.getArgExpr(Argument))
      if (hasSymbolicArrayIndex(ArgExpr, C))
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
        // OwnedConstructChecker proves separately, not this checker; or
        // src/stdio/printf.c's vdprintf(), which builds a throwaway
        // stack `FILE f;` never passed through fopen/fdopen/tmpfile/
        // popen and calls `fflush(&f)` directly on it, exactly the same
        // "unnamed, caller-managed object" shape as the semaphore case,
        // just for Stream instead), a literal constant, or similar.
        // That is real, checkable evidence for every family except a
        // *use* (not release) of Semaphore or Stream, so it is kept
        // reported everywhere else: sem_wait/post and fflush's own
        // unnamed/ad-hoc-object cases are the two legitimate uses of
        // this shape (fclose(&f) on that same ad-hoc FILE, or
        // sem_close(&s) on that same unnamed semaphore, would still be
        // real bugs -- neither Semaphore nor Stream's carve-out here
        // extends to Consume, on purpose). A genuinely Unknown/Undef
        // value is a different case from either: the analyzer itself
        // lost track of what this is (most commonly a loop variable
        // widened away after clang's default max-loop iteration cap),
        // which is "no information" just like an untracked symbol
        // below, not positive evidence.
        if (Call.getArgSVal(Argument).isUnknownOrUndef())
          return;
        if ((Expected == Semaphore || Expected == Stream) && !Consume)
          return;
        // Descriptor is a separate carve-out, and applies regardless of
        // Consume (see isLiteralArgument's own comment for why that is
        // safe specifically for this one family): a concrete descriptor
        // this analysis merely could not trace back to an open()/
        // socket()/... call is only real evidence of a fabricated
        // resource when the source itself wrote the number down as a
        // literal, not when it is a loop induction variable or other
        // computed expression the analyzer's own limited exploration
        // happened to concretize.
        if (Expected == Descriptor && !isLiteralArgument(Call, Argument))
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
