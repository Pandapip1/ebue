// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include <cctype>
#include <memory>
#include <string>

using namespace clang;
using namespace ento;

namespace {

class FallibleResultChecker : public Checker<check::PreStmt<CallExpr>> {
  mutable std::unique_ptr<BugType> BT;

  static bool isFallible(const CallExpr *Call) {
    const FunctionDecl *Function = Call->getDirectCallee();
    if (!Function || !Function->getIdentifier())
      return false;
    static constexpr llvm::StringLiteral Names[] = {"close",
                                                    "posix_close",
                                                    "closedir",
                                                    "fclose",
                                                    "fflush",
                                                    "fseek",
                                                    "fsetpos",
                                                    "remove",
                                                    "rename",
                                                    "renameat",
                                                    "unlink",
                                                    "unlinkat",
                                                    "rmdir",
                                                    "link",
                                                    "linkat",
                                                    "symlink",
                                                    "symlinkat",
                                                    "truncate",
                                                    "ftruncate",
                                                    "fsync",
                                                    "fdatasync",
                                                    "pipe",
                                                    "pipe2",
                                                    "dup2",
                                                    "dup3",
                                                    "read",
                                                    "write",
                                                    "pread",
                                                    "pwrite",
                                                    "chmod",
                                                    "fchmod",
                                                    "fchmodat",
                                                    "chown",
                                                    "fchown",
                                                    "lchown",
                                                    "fchownat",
                                                    "mkdir",
                                                    "mkdirat",
                                                    "munmap",
                                                    "mprotect",
                                                    "msync",
                                                    "sem_init",
                                                    "sem_destroy",
                                                    "sem_close",
                                                    "sem_unlink",
                                                    "sem_wait",
                                                    "sem_trywait",
                                                    "sem_timedwait",
                                                    "sem_post",
                                                    "pthread_join",
                                                    "pthread_detach",
                                                    "pthread_cancel",
                                                    "pthread_mutex_init",
                                                    "pthread_mutex_destroy",
                                                    "pthread_mutex_lock",
                                                    "pthread_mutex_trylock",
                                                    "pthread_mutex_timedlock",
                                                    "pthread_mutex_unlock",
                                                    "pthread_rwlock_init",
                                                    "pthread_rwlock_destroy",
                                                    "pthread_rwlock_rdlock",
                                                    "pthread_rwlock_wrlock",
                                                    "pthread_rwlock_tryrdlock",
                                                    "pthread_rwlock_trywrlock",
                                                    "pthread_rwlock_unlock",
                                                    "pthread_cond_init",
                                                    "pthread_cond_destroy",
                                                    "pthread_cond_wait",
                                                    "pthread_cond_timedwait",
                                                    "pthread_cond_signal",
                                                    "pthread_cond_broadcast",
                                                    "pthread_barrier_init",
                                                    "pthread_barrier_destroy",
                                                    "pthread_barrier_wait",
                                                    "pthread_spin_init",
                                                    "pthread_spin_destroy",
                                                    "pthread_spin_lock",
                                                    "pthread_spin_trylock",
                                                    "pthread_spin_unlock"};
    StringRef Name = Function->getName();
    for (StringRef Candidate : Names)
      if (Name == Candidate)
        return true;
    return false;
  }

  static bool isDiscarded(const CallExpr *Call, ASTContext &Context) {
    DynTypedNode Current = DynTypedNode::create(*Call);
    for (;;) {
      auto Parents = Context.getParents(Current);
      if (Parents.size() != 1)
        return false;
      const Stmt *Parent = Parents[0].get<Stmt>();
      if (!Parent)
        return false;
      if (const auto *Cast = dyn_cast<CastExpr>(Parent)) {
        /* An explicit conversion to void is the C spelling for a deliberate,
         * best-effort operation.  Other casts only transform the value and do
         * not count as handling it. */
        if (Cast->getType()->isVoidType() &&
            isa<CStyleCastExpr>(Cast))
          return false;
        Current = DynTypedNode::create(*Parent);
        continue;
      }
      if (isa<ParenExpr, ExprWithCleanups>(Parent)) {
        Current = DynTypedNode::create(*Parent);
        continue;
      }
      if (const auto *Operator = dyn_cast<BinaryOperator>(Parent))
        return Operator->getOpcode() == BO_Comma &&
               Operator->getLHS() == Current.get<Stmt>();
      const Stmt *Slot = Current.get<Stmt>();
      if (const auto *If = dyn_cast<IfStmt>(Parent))
        return If->getThen() == Slot || If->getElse() == Slot;
      if (const auto *While = dyn_cast<WhileStmt>(Parent))
        return While->getBody() == Slot;
      if (const auto *Do = dyn_cast<DoStmt>(Parent))
        return Do->getBody() == Slot;
      if (const auto *For = dyn_cast<ForStmt>(Parent))
        /* Only the body and increment slots discard their value the way a
         * bare statement does.  The condition slot's value is tested to
         * control the loop, so a fallible call sitting there is used, not
         * discarded -- deliberately not matched here. */
        return For->getBody() == Slot || For->getInc() == Slot;
      return isa<CompoundStmt>(Parent);
    }
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

public:
  void checkPreStmt(const CallExpr *Call, CheckerContext &C) const {
    if (!isFallible(Call) || !isDiscarded(Call, C.getASTContext()))
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode();
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Discarded fallible result",
                                     categories::LogicError);
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        "fallible result is discarded; origin '" +
        SM.getFilename(SM.getExpansionLoc(Call->getBeginLoc())).str() +
        "'; context '" + context(C) + "'; expression '" + text(Call, C) + "'";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Call->getSourceRange());
    C.emitReport(std::move(Report));
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<FallibleResultChecker>(
      "ntlibc.FallibleResult", "Finds discarded results from fallible APIs",
      "");
}
