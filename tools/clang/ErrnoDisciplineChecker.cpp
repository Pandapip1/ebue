// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
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
#include <string>

using namespace clang;
using namespace ento;

/* The call whose return-value symbol is being tracked, keyed by that
 * symbol, for every call this path has made to a function capable of
 * setting errno (checkPostCall fills this in below). */
REGISTER_MAP_WITH_PROGRAMSTATE(ErrnoSetterOf, SymbolRef, const Stmt *)

/* Three per-path facts, keyed by a fixed slot number the way
 * LockDisciplineChecker keys HeldLocks by a MemRegion: the call currently
 * "under diagnosis" (the most recent capable call whose return value was
 * compared for failure), the most recent capable call at all (whether or
 * not it was ever compared), and -- reusing the same map, since a direct
 * `errno = ...` write is its own trusted origin and only needs a
 * non-null marker -- the statement of the most recent such write.  When
 * Diagnosed and LastCapable diverge, an errno read is reading the wrong
 * call's errno; when neither Diagnosed nor LastCapable nor Assigned is
 * set, an errno read has nothing behind it but function-entry state. */
REGISTER_MAP_WITH_PROGRAMSTATE(CallSlot, unsigned, const Stmt *)

namespace {

constexpr unsigned SlotDiagnosed = 0;
constexpr unsigned SlotLastCapable = 1;
constexpr unsigned SlotAssigned = 2;

class ErrnoDisciplineChecker
    : public Checker<check::PostCall, check::PreStmt<BinaryOperator>,
                     check::PreStmt<UnaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

  /* Functions this codebase's own implementation proves capable of
   * setting errno as a side effect, grounded against this tree (not
   * glibc convention) via `grep -rn "errno = " src/` and its callers,
   * plus src/internal/libc.h's own "-1 with errno"/"NULL with errno"
   * doc comments.  Almost everything here is an ntlibc-internal
   * NT-syscall-wrapping helper; close() and munmap() are kept as the
   * two POSIX-named "cleanup after a diagnosed failure" calls the CERT
   * ERR30-C pattern this checker looks for actually uses in this tree. */
  static bool isErrnoCapable(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return false;
    static constexpr llvm::StringLiteral Names[] = {
        /* The canonical NTSTATUS -> errno mapper; most entries below
         * either call this directly or duplicate its "errno = map(st);
         * return -1" shape inline. */
        "__set_errno_status",
        /* Path translation: POSIX path -> UNICODE_STRING/OBJECT_ATTRIBUTES.
         * "Returns 0 or -1 with errno." */
        "__ntpath", "__ntpath_native", "__ntpath_at", "__ntpath_at_native",
        /* The guts of open()/openat(), unlink()/rmdir(), stat()/fstat():
         * each documented "Returns 0, or -1 with errno." */
        "__open_handle", "__unlink_at", "__fstat_handle",
        /* The descriptor table: each documented "-1 with errno" or
         * "NULL with errno=EBADF". */
        "__fd_alloc", "__fd_install", "__fd_get", "__fd_handle",
        "__fd_pos_save", "__fd_runtime_data",
        /* The fixed POSIX namespace resolver: "-1 is a path error with
         * errno set." */
        "__vfs_resolve_at", "__vfs_open_dir", "__vfs_stat",
        /* Process/exec and WSL mode-attribute helpers: "-1 with errno." */
        "__spawn", "__lxmod_set", "__find_program",
        /* nanosleep()/sleep()/clock_nanosleep()'s shared alertable wait:
         * "-1 with errno=EINTR". */
        "__alertable_delay",
        /* RLIMIT_FSIZE enforcement: "sets errno to EFBIG". */
        "__fsize_exceeded",
        /* UTF-8/UTF-16 conversion: "NULL with errno" / "-1 with errno". */
        "__utf8_to_utf16", "__utf16_to_utf8_buf",
        /* AFD (Winsock) helpers and the directory-stream cursor, plus the
         * handle-to-path resolver: each contains its own "errno = ..."
         * assignment. */
        "__afd_open", "__afd_addr_from_sockaddr", "__dirstream_next",
        "__handle_path",
        /* close() sets errno via __fd_get()/__set_errno_status()
         * internally; munmap() sets it directly.  Both are exactly the
         * "cleanup after a diagnosed failure" call that clobbers errno. */
        "close", "munmap",
    };
    StringRef Name = Function->getName();
    for (StringRef Candidate : Names)
      if (Name == Candidate)
        return true;
    return false;
  }

  static bool isErrnoLocationCall(const Expr *E) {
    const auto *Call = dyn_cast_or_null<CallExpr>(E->IgnoreParenImpCasts());
    if (!Call)
      return false;
    const FunctionDecl *Function = Call->getDirectCallee();
    return Function && Function->getIdentifier() &&
           Function->getName() == "__errno_location";
  }

  /* errno expands to `(*__errno_location())`; this recognises that
   * expansion regardless of which macro instantiated it. */
  static bool isErrnoDeref(const UnaryOperator *Op) {
    return Op->getOpcode() == UO_Deref &&
           isErrnoLocationCall(Op->getSubExpr());
  }

  /* True when Node is exactly the left-hand side of a plain (non-compound)
   * assignment -- a write that gives errno a fresh, trusted origin, not a
   * read this checker should evaluate.
   *
   * The `errno` macro itself is `(*__errno_location())` -- note the
   * macro's own parentheses -- so the UnaryOperator's immediate syntactic
   * parent is a ParenExpr, not the BinaryOperator, for every use of the
   * macro. Walk up through any wrapping ParenExpr nodes before checking
   * for the assignment, or every `errno = ...;` misses its own trusted
   * origin and both the assignment and the next read falsely report as
   * unproven. */
  static bool isAssignmentTarget(const UnaryOperator *Node,
                                 CheckerContext &C) {
    DynTypedNode Current = DynTypedNode::create(*Node);
    for (;;) {
      auto Parents = C.getASTContext().getParents(Current);
      if (Parents.size() != 1)
        return false;
      if (const auto *Paren = Parents[0].get<ParenExpr>()) {
        Current = DynTypedNode::create(*Paren);
        continue;
      }
      const auto *Parent = Parents[0].get<BinaryOperator>();
      return Parent && Parent->getOpcode() == BO_Assign &&
             Parent->getLHS()->IgnoreParens() == Node;
    }
  }

  static std::string calleeName(const Stmt *CallStmt) {
    if (!CallStmt)
      return "nothing";
    if (const auto *Call = dyn_cast_or_null<CallExpr>(CallStmt)) {
      if (const FunctionDecl *Function = Call->getDirectCallee())
        if (Function->getIdentifier())
          return Function->getNameAsString();
      return "<call>";
    }
    return "a direct errno assignment";
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

  void report(const std::string &Reason, const Stmt *Statement,
              ProgramStateRef State, CheckerContext &C) const {
    if (!Statement)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven errno discipline",
                                     categories::LogicError);
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        (llvm::Twine(Reason) + "; origin '" +
         SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
         "'; context '" + context(C) + "'; expression '" + text(Statement, C) +
         "'")
            .str();
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    if (!isErrnoCapable(Call))
      return;
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;
    ProgramStateRef State =
        C.getState()->set<CallSlot>(SlotLastCapable, Statement);
    SymbolRef Symbol = Call.getReturnValue().getAsSymbol(true);
    if (Symbol)
      State = State->set<ErrnoSetterOf>(Symbol, Statement);
    C.addTransition(State);
  }

  /* Recognise `<capable-call> <cmp> <sentinel>` (and the transitive form
   * through a variable the call's result was copied into, which the
   * engine's own symbolic execution already resolves to the same
   * symbol) as "the code is diagnosing this call's failure" -- CERT
   * ERR30-C's precondition for trusting errno afterward. */
  void checkPreStmt(const BinaryOperator *Operation, CheckerContext &C) const {
    switch (Operation->getOpcode()) {
    case BO_LT:
    case BO_LE:
    case BO_GT:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
      break;
    default:
      return;
    }
    ProgramStateRef State = C.getState();
    SymbolRef Symbol = C.getSVal(Operation->getLHS()).getAsSymbol(true);
    if (!Symbol)
      Symbol = C.getSVal(Operation->getRHS()).getAsSymbol(true);
    if (!Symbol)
      return;
    const Stmt *const *Setter = State->get<ErrnoSetterOf>(Symbol);
    if (!Setter)
      return;
    C.addTransition(State->set<CallSlot>(SlotDiagnosed, *Setter));
  }

  void checkPreStmt(const UnaryOperator *Operation, CheckerContext &C) const {
    if (!isErrnoDeref(Operation))
      return;
    ProgramStateRef State = C.getState();
    if (isAssignmentTarget(Operation, C)) {
      /* A direct `errno = ...` write is its own trusted origin: it
       * outranks whatever call was previously under diagnosis. */
      State = State->set<CallSlot>(SlotAssigned, Operation);
      State = State->remove<CallSlot>(SlotDiagnosed);
      State = State->remove<CallSlot>(SlotLastCapable);
      C.addTransition(State);
      return;
    }
    const Stmt *const *Diagnosed = State->get<CallSlot>(SlotDiagnosed);
    const Stmt *const *LastCapable = State->get<CallSlot>(SlotLastCapable);
    if (Diagnosed) {
      if (!LastCapable || *LastCapable != *Diagnosed) {
        std::string Reason =
            "errno read after an intervening call to '" +
            calleeName(LastCapable ? *LastCapable : nullptr) +
            "' may not reflect '" + calleeName(*Diagnosed) + "'s failure";
        report(Reason, Operation, State, C);
      }
      return;
    }
    if (LastCapable)
      return; /* a capable call happened; its result was just never
               * compared, which is not one of this checker's two proof
               * obligations. */
    if (!State->get<CallSlot>(SlotAssigned))
      report("errno is read with no proven prior call or assignment that "
             "could have set it",
             Operation, State, C);
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<ErrnoDisciplineChecker>(
      "ntlibc.ErrnoDiscipline",
      "Proves errno is read only from the call whose failure it reports, "
      "and only after some call or assignment could have set it",
      "");
}
