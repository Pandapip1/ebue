// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <cctype>
#include <memory>
#include <string>

using namespace clang;

namespace {

static const Expr *ignore(const Expr *Expression) {
  Expression = Expression ? Expression->IgnoreParenImpCasts() : nullptr;
  if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Expression))
    if (Address->getOpcode() == UO_AddrOf)
      return Address->getSubExpr()->IgnoreParenImpCasts();
  return Expression;
}

class HandlerCollector : public RecursiveASTVisitor<HandlerCollector> {
  llvm::SmallPtrSetImpl<const FunctionDecl *> &Handlers;

public:
  explicit HandlerCollector(
      llvm::SmallPtrSetImpl<const FunctionDecl *> &Handlers)
      : Handlers(Handlers) {}

  bool VisitCallExpr(CallExpr *Call) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || Callee->getName() != "signal" || Call->getNumArgs() < 2)
      return true;
    const auto *Reference =
        dyn_cast_or_null<DeclRefExpr>(ignore(Call->getArg(1)));
    const auto *Handler =
        Reference ? dyn_cast<FunctionDecl>(Reference->getDecl()) : nullptr;
    if (Handler)
      Handlers.insert(Handler->getCanonicalDecl());
    return true;
  }
};

class HandlerSafetyVisitor : public RecursiveASTVisitor<HandlerSafetyVisitor> {
  ASTContext &Context;
  DiagnosticsEngine &Diagnostics;
  const FunctionDecl *Handler;
  unsigned UnsafeCallDiagnostic;
  unsigned GlobalWriteDiagnostic;

  std::string text(const Stmt *Statement) const {
    const SourceManager &SM = Context.getSourceManager();
    StringRef Raw =
        Lexer::getSourceText(CharSourceRange::getTokenRange(
                                 SM.getSpellingLoc(Statement->getBeginLoc()),
                                 SM.getSpellingLoc(Statement->getEndLoc())),
                             SM, Context.getLangOpts());
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

  static bool asyncSafe(StringRef Name) {
    static constexpr llvm::StringLiteral Names[] = {"_Exit",
                                                    "_exit",
                                                    "abort",
                                                    "accept",
                                                    "access",
                                                    "alarm",
                                                    "bind",
                                                    "chdir",
                                                    "chmod",
                                                    "chown",
                                                    "close",
                                                    "connect",
                                                    "dup",
                                                    "dup2",
                                                    "execle",
                                                    "execve",
                                                    "fchmod",
                                                    "fchown",
                                                    "fcntl",
                                                    "fork",
                                                    "fstat",
                                                    "fsync",
                                                    "ftruncate",
                                                    "getegid",
                                                    "geteuid",
                                                    "getgid",
                                                    "getgroups",
                                                    "getpeername",
                                                    "getpgrp",
                                                    "getpid",
                                                    "getppid",
                                                    "getsockname",
                                                    "getsockopt",
                                                    "getuid",
                                                    "kill",
                                                    "link",
                                                    "listen",
                                                    "lseek",
                                                    "mkdir",
                                                    "open",
                                                    "pause",
                                                    "pipe",
                                                    "poll",
                                                    "posix_trace_event",
                                                    "pselect",
                                                    "raise",
                                                    "read",
                                                    "readlink",
                                                    "recv",
                                                    "recvfrom",
                                                    "recvmsg",
                                                    "rename",
                                                    "rmdir",
                                                    "select",
                                                    "sem_post",
                                                    "send",
                                                    "sendmsg",
                                                    "sendto",
                                                    "setgid",
                                                    "setpgid",
                                                    "setsid",
                                                    "setsockopt",
                                                    "setuid",
                                                    "shutdown",
                                                    "sigaction",
                                                    "sigaddset",
                                                    "sigdelset",
                                                    "sigemptyset",
                                                    "sigfillset",
                                                    "sigismember",
                                                    "signal",
                                                    "sigpause",
                                                    "sigpending",
                                                    "sigprocmask",
                                                    "sigqueue",
                                                    "sigsuspend",
                                                    "sleep",
                                                    "socket",
                                                    "socketpair",
                                                    "stat",
                                                    "symlink",
                                                    "tcdrain",
                                                    "tcflow",
                                                    "tcflush",
                                                    "tcgetattr",
                                                    "tcgetpgrp",
                                                    "tcsendbreak",
                                                    "tcsetattr",
                                                    "tcsetpgrp",
                                                    "time",
                                                    "timer_getoverrun",
                                                    "timer_gettime",
                                                    "timer_settime",
                                                    "times",
                                                    "umask",
                                                    "uname",
                                                    "unlink",
                                                    "utime",
                                                    "wait",
                                                    "waitpid",
                                                    "write"};
    for (StringRef Candidate : Names)
      if (Name == Candidate)
        return true;
    return false;
  }

  static const VarDecl *globalTarget(const Expr *Expression) {
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(ignore(Expression));
    const auto *Variable =
        Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
    return Variable && Variable->hasGlobalStorage() ? Variable : nullptr;
  }

  static bool permittedAtomic(const VarDecl *Variable) {
    QualType Type = Variable->getType();
    if (!Type.isVolatileQualified())
      return false;
    Type = Type.getUnqualifiedType();
    const auto *Typedef = Type->getAs<TypedefType>();
    return Typedef && Typedef->getDecl()->getName() == "sig_atomic_t";
  }

public:
  HandlerSafetyVisitor(ASTContext &Context, const FunctionDecl *Handler)
      : Context(Context), Diagnostics(Context.getDiagnostics()),
        Handler(Handler), UnsafeCallDiagnostic(Diagnostics.getCustomDiagID(
                              DiagnosticsEngine::Warning,
                              "signal handler call is not proven "
                              "async-signal-safe; context '%0'; "
                              "expression '%1' [ntlibc.SignalSafety]")),
        GlobalWriteDiagnostic(Diagnostics.getCustomDiagID(
            DiagnosticsEngine::Warning,
            "signal handler writes non-atomic global state; context '%0'; "
            "expression '%1' [ntlibc.SignalSafety]")) {}

  bool VisitCallExpr(CallExpr *Call) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || !asyncSafe(Callee->getName()))
      Diagnostics.Report(Call->getExprLoc(), UnsafeCallDiagnostic)
          << Handler->getQualifiedNameAsString() << text(Call);
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *Operation) {
    if (!Operation->isAssignmentOp())
      return true;
    const VarDecl *Variable = globalTarget(Operation->getLHS());
    if (Variable && !permittedAtomic(Variable))
      Diagnostics.Report(Operation->getExprLoc(), GlobalWriteDiagnostic)
          << Handler->getQualifiedNameAsString() << text(Operation);
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *Operation) {
    if (!Operation->isIncrementDecrementOp())
      return true;
    const VarDecl *Variable = globalTarget(Operation->getSubExpr());
    if (Variable && !permittedAtomic(Variable))
      Diagnostics.Report(Operation->getExprLoc(), GlobalWriteDiagnostic)
          << Handler->getQualifiedNameAsString() << text(Operation);
    return true;
  }
};

class SignalSafetyConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &Context) override {
    llvm::SmallPtrSet<const FunctionDecl *, 8> Handlers;
    HandlerCollector(Handlers).TraverseDecl(Context.getTranslationUnitDecl());
    for (const FunctionDecl *Canonical : Handlers) {
      const FunctionDecl *Definition = Canonical->getDefinition();
      if (!Definition || !Definition->hasBody())
        continue;
      HandlerSafetyVisitor(Context, Definition)
          .TraverseStmt(Definition->getBody());
    }
  }
};

class SignalSafetyAction : public PluginASTAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                 StringRef) override {
    return std::make_unique<SignalSafetyConsumer>();
  }

  bool ParseArgs(const CompilerInstance &,
                 const std::vector<std::string> &) override {
    return true;
  }

  ActionType getActionType() override { return AddAfterMainAction; }
};

} // namespace

static FrontendPluginRegistry::Add<SignalSafetyAction>
    X("ntlibc-signal-safety", "prove registered signal-handler safety");
