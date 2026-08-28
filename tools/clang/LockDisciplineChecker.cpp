// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
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

class LockDisciplineChecker
    : public Checker<check::PreCall, check::PostCall, check::EndFunction> {
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
      if (Next == HeldKind::Unlocked)
        Succeeded = Succeeded->remove<LockAcquirers>(Region);
      else
        Succeeded = Succeeded->set<LockAcquirers>(Region, C.getStackFrame());
      C.addTransition(Succeeded);
    }
    if (Failed)
      C.addTransition(Failed);
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    for (const auto &Entry : C.getState()->get<HeldLocks>()) {
      if (Entry.second == HeldKind::Unlocked)
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
