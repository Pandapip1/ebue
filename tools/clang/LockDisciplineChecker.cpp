// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMap.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

enum class HeldKind : unsigned char { Unlocked, Read, Write };
REGISTER_MAP_WITH_PROGRAMSTATE(HeldLocks, const MemRegion *, HeldKind)
REGISTER_MAP_WITH_PROGRAMSTATE(LockAcquirers, const MemRegion *,
                               const StackFrameContext *)
// A region tagged here, for the stack frame that tagged it, is exempt from
// checkEndFunction's "function exits while a lock is held" report --
// because for that specific frame, ending while holding this specific
// lock is not a leak but the whole point: a hand-off to whatever runs
// next (the function's caller, or, for a pthread_cleanup_push() handler,
// the cancellation machinery that invoked it). See RequiresHeldOnEntry
// and AcquiresLockForCaller below for the two ways a region gets tagged,
// and their own comments for why ntlibc has exactly two functions that
// need this and no general-purpose annotation mechanism for it.
REGISTER_MAP_WITH_PROGRAMSTATE(HandoffExempt, const MemRegion *,
                               const StackFrameContext *)

namespace {

enum class LockOperation : unsigned char {
  Initialize,
  AcquireRead,
  AcquireWrite,
  Release,
  RequireHeld,
  Destroy
};

struct LockCall {
  LockOperation Operation;
  unsigned Argument;
};

// A (function name, argument index) pair whose designated lock argument
// is, by contract, already held by the caller when the function is
// entered -- and is handed back held on every return path, success,
// failure, or cancellation alike.
//
// cond_wait() is pthread_cond_wait()/pthread_cond_timedwait()'s shared
// implementation (src/thread/pthread_cond.c): POSIX requires the mutex
// argument to those two public functions to already be locked by the
// calling thread on entry, and requires it to be locked again on every
// return.  protocolFor()'s RequireHeld entry for the two public names
// already enforces the first half of that at THEIR call sites -- but
// cond_wait is a separate C function implementing the same contract, and
// the by-name protocol table below has no way to reach into a function
// it never sees a call to. Without the entry below, cond_wait's own
// first pthread_mutex_unlock(mutex) call (releasing the lock its own
// caller handed it) is indistinguishable, to a checker with no memory of
// an un-observed precondition, from releasing a lock nobody ever
// acquired -- a real false positive this project's own fixtures already
// demonstrate is otherwise correctly reported (see unsafe.c's
// unlocked_release).
//
// checkBeginFunction seeds the designated region as held, and also tags
// it exempt in HandoffExempt (see that map's comment). The exemption
// tag, once set, is never cleared for the rest of that stack frame's
// lifetime -- it survives the region's own later release/reacquire
// cycles (cond_wait unlocks this exact mutex once, then relocks it
// before returning), so every return path is covered uniformly: both
// the early "the caller handed me an already-dead condvar" returns that
// never touch the mutex at all, and the ordinary return after a full
// wait-and-reacquire cycle.
struct HandoffParameter {
  const char *Function;
  unsigned Argument;
};
constexpr HandoffParameter RequiresHeldOnEntry[] = {
    {"cond_wait", 1},
};

// Functions that acquire a lock purely to hand it off held to whatever
// runs next, not to release it themselves -- checkPostCall tags the
// region such a function acquires as HandoffExempt the moment the
// acquisition succeeds, rather than seeding it in advance the way
// RequiresHeldOnEntry does.
//
// cond_wait_cleanup is the pthread_cleanup_push() handler cond_wait
// registers for the duration of its wait: if the thread is cancelled out
// of the wait, the cancellation machinery invokes this handler, whose
// entire job is to restore the same "mutex locked" postcondition on the
// cancellation path that cond_wait itself guarantees on every ordinary
// path (see RequiresHeldOnEntry's comment) -- the lock it takes here is
// for the code that resumes after cancellation, not for itself to
// release. It cannot use RequiresHeldOnEntry's parameter-index seeding:
// its mutex is not a plain parameter but cleanup->mutex, a struct field
// reached through its lone void* argument, so there is no fixed region
// to compute until the real pthread_mutex_lock(cleanup->mutex) call
// inside its body actually resolves that indirection -- which is exactly
// the moment checkPostCall tags it.
constexpr const char *AcquiresLockForCaller[] = {
    "cond_wait_cleanup",
};

class LockDisciplineChecker
    : public Checker<check::PreCall, check::PostCall, check::EndFunction,
                     check::BeginFunction> {
  mutable std::unique_ptr<BugType> BT;

  static std::optional<LockCall> protocolFor(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return std::nullopt;
    StringRef Name = Function->getName();
    if (Name == "pthread_mutex_init" || Name == "pthread_rwlock_init" ||
        Name == "pthread_spin_init")
      return LockCall{LockOperation::Initialize, 0};
    if (Name == "pthread_mutex_lock" || Name == "pthread_mutex_trylock" ||
        Name == "pthread_mutex_timedlock" || Name == "pthread_spin_lock" ||
        Name == "pthread_spin_trylock")
      return LockCall{LockOperation::AcquireWrite, 0};
    if (Name == "pthread_rwlock_rdlock" || Name == "pthread_rwlock_tryrdlock" ||
        Name == "pthread_rwlock_timedrdlock")
      return LockCall{LockOperation::AcquireRead, 0};
    if (Name == "pthread_rwlock_wrlock" || Name == "pthread_rwlock_trywrlock" ||
        Name == "pthread_rwlock_timedwrlock")
      return LockCall{LockOperation::AcquireWrite, 0};
    if (Name == "pthread_mutex_unlock" || Name == "pthread_rwlock_unlock" ||
        Name == "pthread_spin_unlock")
      return LockCall{LockOperation::Release, 0};
    if (Name == "pthread_cond_wait" || Name == "pthread_cond_timedwait")
      return LockCall{LockOperation::RequireHeld, 1};
    if (Name == "pthread_mutex_destroy" || Name == "pthread_rwlock_destroy" ||
        Name == "pthread_spin_destroy")
      return LockCall{LockOperation::Destroy, 0};
    return std::nullopt;
  }

  static const MemRegion *regionFor(const CallEvent &Call,
                                    const LockCall &Protocol) {
    if (Protocol.Argument >= Call.getNumArgs())
      return nullptr;
    return Call.getArgSVal(Protocol.Argument).getAsRegion();
  }

  // True if the function currently being analyzed is one of
  // AcquiresLockForCaller's names -- see that table's comment.
  static bool acquiresForCaller(CheckerContext &C) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function || !Function->getIdentifier())
      return false;
    StringRef Name = Function->getName();
    for (const char *Entry : AcquiresLockForCaller)
      if (Name == Entry)
        return true;
    return false;
  }

  // True if Call's own CallExpr is (modulo enclosing parentheses and
  // implicit casts) the return statement's own operand -- i.e. the
  // surrounding function reads exactly like `return
  // pthread_mutex_unlock(mutex);`, with nothing between the call and the
  // return to swallow the value. See checkPostCall's Failed-branch
  // handling for why this matters: it is the difference between a
  // function silently discarding a release's ambiguous outcome (a real
  // bug the existing unheld-release check catches independently, before
  // this ever runs) and one explicitly propagating that exact outcome to
  // its own caller, which is not a leak this function is responsible
  // for -- the caller receives the same nonzero status a direct call to
  // pthread_mutex_unlock() would have given it, and can react to it
  // exactly as it would have.
  static bool isDirectReturnOperand(const CallEvent &Call, CheckerContext &C) {
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return false;
    ParentMap &PM = C.getLocationContext()->getAnalysisDeclContext()->getParentMap();
    const Stmt *Parent = PM.getParentIgnoreParenCasts(Statement);
    return Parent && isa<ReturnStmt>(Parent);
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

  static std::string context(CheckerContext &C) {
    const Decl *Current = C.getLocationContext()->getDecl();
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      return Named->getQualifiedNameAsString();
    return Current ? Current->getDeclKindName() : "unknown";
  }

  void report(StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    if (!Statement)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven lock discipline",
                                     categories::LogicError);
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        (Reason + "; origin '" +
         SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
         "'; context '" + context(C) + "'; expression '" + text(Statement, C) +
         "'")
            .str();
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    std::optional<LockCall> Protocol = protocolFor(Call);
    if (!Protocol)
      return;
    const MemRegion *Region = regionFor(Call, *Protocol);
    const Stmt *Statement = Call.getOriginExpr();
    if (!Region || !Statement)
      return;
    const HeldKind *Kind = C.getState()->get<HeldLocks>(Region);
    bool Held = Kind && *Kind != HeldKind::Unlocked;
    switch (Protocol->Operation) {
    case LockOperation::AcquireRead:
    case LockOperation::AcquireWrite:
      if (Held)
        report("lock acquisition is attempted while already held", Statement,
               C.getState(), C);
      break;
    case LockOperation::Release:
      if (!Held)
        report("lock release is not proven to hold the lock", Statement,
               C.getState(), C);
      break;
    case LockOperation::RequireHeld:
      if (!Held)
        report("condition wait is not proven to hold its mutex", Statement,
               C.getState(), C);
      break;
    case LockOperation::Destroy:
      if (Held)
        report("lock is destroyed while held", Statement, C.getState(), C);
      break;
    case LockOperation::Initialize:
      break;
    }
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    std::optional<LockCall> Protocol = protocolFor(Call);
    if (!Protocol || Protocol->Operation == LockOperation::RequireHeld ||
        Protocol->Operation == LockOperation::Destroy)
      return;
    const MemRegion *Region = regionFor(Call, *Protocol);
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Region || !Function)
      return;
    std::optional<DefinedOrUnknownSVal> Return =
        Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
    if (!Return)
      return;
    DefinedOrUnknownSVal Success = C.getSValBuilder().evalEQ(
        C.getState(), *Return,
        C.getSValBuilder().makeZeroVal(Function->getReturnType()));
    auto [Succeeded, Failed] = C.getState()->assume(Success);
    if (Succeeded) {
      HeldKind Next = HeldKind::Unlocked;
      if (Protocol->Operation == LockOperation::AcquireRead)
        Next = HeldKind::Read;
      else if (Protocol->Operation == LockOperation::AcquireWrite)
        Next = HeldKind::Write;
      Succeeded = Succeeded->set<HeldLocks>(Region, Next);
      if (Next == HeldKind::Unlocked) {
        Succeeded = Succeeded->remove<LockAcquirers>(Region);
      } else {
        Succeeded = Succeeded->set<LockAcquirers>(Region, C.getStackFrame());
        // See AcquiresLockForCaller's comment: cond_wait_cleanup's
        // pthread_mutex_lock(cleanup->mutex) resolves here to whatever
        // region cleanup->mutex actually names, which is exactly the
        // region this acquisition is tagging -- there is no way to know
        // that region in advance of this call succeeding.
        if (acquiresForCaller(C))
          Succeeded = Succeeded->set<HandoffExempt>(Region, C.getStackFrame());
      }
      C.addTransition(Succeeded);
    }
    if (Failed) {
      // A release whose own return value is (modulo casts) the
      // surrounding function's return value -- `return
      // pthread_mutex_unlock(mutex);` -- propagates whatever ambiguity a
      // nonzero return leaves about the lock's true state straight to
      // its own caller, unchanged, rather than swallowing it. Ordinarily
      // a Release's Failed branch retains the prior held state, because
      // in general a failed unlock's effect on the lock is unknown; here
      // that unknown outcome becomes the caller's problem to interpret,
      // exactly as if the caller had called pthread_mutex_unlock()
      // itself, so this function has nothing left to leak. (A release of
      // a lock this function never actually held in the first place --
      // unsafe.c's unlocked_release fixture -- is unaffected: checkPreCall
      // already reports that at the call site itself, before this
      // success/failure split is even reached.)
      if (Protocol->Operation == LockOperation::Release &&
          isDirectReturnOperand(Call, C)) {
        Failed = Failed->set<HeldLocks>(Region, HeldKind::Unlocked);
        Failed = Failed->remove<LockAcquirers>(Region);
      }
      C.addTransition(Failed);
    }
  }

  void checkBeginFunction(CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function || !Function->getIdentifier())
      return;
    StringRef Name = Function->getName();
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (const HandoffParameter &Entry : RequiresHeldOnEntry) {
      if (Name != Entry.Function || Entry.Argument >= Function->getNumParams())
        continue;
      const ParmVarDecl *Param = Function->getParamDecl(Entry.Argument);
      Loc ParamLoc = State->getLValue(Param, C.getLocationContext());
      const MemRegion *Region = State->getSVal(ParamLoc).getAsRegion();
      if (!Region)
        continue;
      // Seed the precondition (checkPreCall's Release check needs this
      // to not misread the caller's already-held lock as unheld -- see
      // RequiresHeldOnEntry's comment) and tag the region exempt from
      // the end-of-function leak check in the same step: both halves of
      // this function's contract share one region, discovered once,
      // here.
      State = State->set<HeldLocks>(Region, HeldKind::Write);
      State = State->set<HandoffExempt>(Region, C.getStackFrame());
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    for (const auto &Entry : C.getState()->get<HeldLocks>()) {
      if (Entry.second == HeldKind::Unlocked)
        continue;
      const StackFrameContext *const *Exempt =
          C.getState()->get<HandoffExempt>(Entry.first);
      if (Exempt && *Exempt == C.getStackFrame())
        continue;
      const StackFrameContext *const *Acquirer =
          C.getState()->get<LockAcquirers>(Entry.first);
      if (!Acquirer || *Acquirer != C.getStackFrame())
        continue;
      const auto *Function =
          dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
      const Stmt *Statement = Return;
      if (!Statement && Function && Function->hasBody())
        Statement = Function->getBody();
      report("function exits while a lock is held", Statement, C.getState(), C);
      return;
    }
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<LockDisciplineChecker>(
      "ntlibc.LockDiscipline", "Proves mutex, rwlock, and spinlock discipline",
      "");
}
