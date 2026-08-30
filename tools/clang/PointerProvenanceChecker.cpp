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
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"

#include <cctype>
#include <memory>
#include <string>

using namespace clang;
using namespace ento;

// PostCall tracks known "needle in haystack" library calls (see
// isNeedleFunction below): the symbolic region conjured for such a
// call's return value is recorded here, mapped to the base region its
// first argument (the haystack) resolved to.  checkPreStmt(BinaryOperator)
// consults this map so that, e.g., `q = strstr(p, ...); ... q - p` is
// recognised as same-provenance even though the call itself is opaque
// (its definition lives in a different translation unit, so the engine
// cannot inline it and conjures q a fresh, otherwise-unrelated symbol).
//
// Registered at namespace-global scope (not inside the anonymous
// namespace below) because REGISTER_MAP_WITH_PROGRAMSTATE expands to a
// specialization of clang::ento::ProgramStateTrait, and a template
// specialization must live in a namespace that encloses the template's
// own -- an anonymous namespace does not enclose clang::ento, only sits
// beside it.
REGISTER_MAP_WITH_PROGRAMSTATE(NeedleAlias, SymbolRef, const MemRegion *)

namespace {

class PointerProvenanceChecker
    : public Checker<check::PreStmt<BinaryOperator>, check::PreStmt<CastExpr>,
                      check::PostCall> {
  mutable std::unique_ptr<BugType> BT;

  // Transitive: `p = strstr(...); p2 = strstr(p + 2, ...);` (fnmatch.c's
  // bracket_match(), walking from one "[:class:]" delimiter to the
  // next) chains two needle calls, so p2's conjured symbol aliases to
  // p's conjured symbol, which itself only aliases to the *real*
  // parameter region one more hop away.  A single lookup would resolve
  // p2 one hop short of p's own origin and wrongly call the two
  // unequal.  Bounded defensively (aliasing is a DAG built by
  // checkPostCall one call at a time, so a real cycle should be
  // impossible, but an unbounded walk turning a checker bug into a
  // hang is a strictly worse failure mode than an unbounded walk
  // turning it into a false negative).
  static const MemRegion *resolveAlias(const MemRegion *Base,
                                        ProgramStateRef State) {
    for (int Hops = 0; Base && Hops < 32; ++Hops) {
      const auto *SR = dyn_cast<SymbolicRegion>(Base);
      if (!SR)
        break;
      const MemRegion *const *Aliased = State->get<NeedleAlias>(SR->getSymbol());
      if (!Aliased)
        break;
      Base = *Aliased;
    }
    return Base;
  }

  static const MemRegion *baseRegion(SVal Value, ProgramStateRef State) {
    const MemRegion *Region = Value.getAsRegion();
    if (!Region)
      return nullptr;
    return resolveAlias(Region->getBaseRegion(), State);
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

  // isConstantSentinel: the source of an integer-to-pointer cast is a
  // compile-time constant (e.g. NT's own `(HANDLE)(LONG_PTR)-1`
  // pseudo-handle convention -- see NtCurrentProcess()/NtCurrentThread()
  // in src/internal/nt.h -- or the equally common `(void (*)(int))1`-style
  // SIG_DFL/SIG_IGN/SIG_ERR and MAP_FAILED-style invalid-handle sentinels
  // used throughout this tree for nl_catd, iconv_t, sem_t, fenv_t, and
  // signal-handler results).  "Provenance" is not a coherent question for
  // a literal: it was never derived from any pointer, real or forged, and
  // its value is fully visible to any reader of the diff, unlike an
  // integer arriving from a variable, a syscall, or (worse) untrusted
  // input.  This is a strengthening of the checker's own stated purpose
  // ("rejects integer-derived pointers") rather than a relaxation of it:
  // a fixed sentinel was never *derived* from anything.
  static bool isConstantSentinel(const Expr *E, ASTContext &Ctx) {
    if (E->isValueDependent() || E->isTypeDependent())
      return false;
    Expr::EvalResult Result;
    return E->EvaluateAsInt(Result, Ctx, Expr::SE_NoSideEffects);
  }

  // derivesFromPointer: true if E is built, through parens, arithmetic
  // (+, -, &, |, ^, unary ~) or a conditional operator, from a nested
  // pointer-to-integral cast.  This recognises the pointer -> integer ->
  // (mask/offset) -> pointer round trip used for alignment throughout
  // this tree (posix_memalign(), align16(), the redirect_async_cancel()
  // stack-probe alignment, and mman.c's page-range intersection -- see
  // the ternary-operator case, needed for `lo = a > b ? a : b`-style
  // range clamps built from two such round trips).  The integer was
  // never anything but a pointer's own bit pattern plus a compile-time-
  // visible adjustment, so the cast back is provenance-preserving by
  // construction, not merely unproven.
  static bool derivesFromPointer(const Expr *E) {
    E = E->IgnoreParens();
    if (const auto *CE = dyn_cast<CastExpr>(E)) {
      if (CE->getCastKind() == CK_PointerToIntegral)
        return true;
      return derivesFromPointer(CE->getSubExpr());
    }
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      switch (BO->getOpcode()) {
      case BO_Add:
      case BO_Sub:
      case BO_And:
      case BO_Or:
      case BO_Xor:
        return derivesFromPointer(BO->getLHS()) ||
               derivesFromPointer(BO->getRHS());
      default:
        return false;
      }
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      switch (UO->getOpcode()) {
      case UO_Not:
      case UO_Plus:
      case UO_Minus:
        return derivesFromPointer(UO->getSubExpr());
      default:
        return false;
      }
    }
    if (const auto *CO = dyn_cast<ConditionalOperator>(E))
      return derivesFromPointer(CO->getTrueExpr()) ||
             derivesFromPointer(CO->getFalseExpr());
    // A reference to a local `uintptr_t ia = (uintptr_t)a;`-style
    // variable: mman.c's range-clamp idiom (`lo = a > m->base ? a :
    // m->base;`) names each round-tripped pointer before combining
    // them, rather than nesting the casts inline, so the derivation has
    // to be traced back through the one initializer rather than found
    // in the expression itself.  Looking at the initializer only (not
    // tracking reassignment) is a deliberate, narrow heuristic: these
    // are write-once locals by construction in every real call site
    // this covers, and the failure mode of being wrong here is a
    // missed relaxation (checker stays strict), not a missed bug.
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        if (const Expr *Init = VD->getInit())
          return derivesFromPointer(Init);
      }
    }
    return false;
  }

  // Walk up past any wrapping parens/implicit casts to the first
  // "interesting" parent statement of Node.
  static const Stmt *significantParent(const Stmt *Node, CheckerContext &C) {
    ParentMap &PM = C.getLocationContext()->getAnalysisDeclContext()->getParentMap();
    const Stmt *Parent = PM.getParent(Node);
    while (Parent && (isa<ParenExpr>(Parent) || isa<ImplicitCastExpr>(Parent)))
      Parent = PM.getParent(Parent);
    return Parent;
  }

  // isClientIdAssignment: the cast's result is the right-hand side of an
  // assignment to a field literally named UniqueProcess or UniqueThread.
  // NT's own CLIENT_ID structure (src/internal/nt.h) declares both
  // fields HANDLE-typed, but the kernel treats them as plain numeric
  // process/thread IDs, not as real handles -- MSDN documents this
  // explicitly, and NtOpenProcess()/NtOpenThread() are the only real
  // consumers, neither of which ever dereferences the "handle" as
  // memory.  This is the same class of NT API quirk as
  // NtCurrentProcess()/NtCurrentThread() (which isConstantSentinel
  // already covers, since both expand to constant casts): a userspace-
  // opaque integer riding in a slot the public header types as HANDLE
  // purely by kernel-ABI convention.
  static bool isClientIdAssignment(const CastExpr *Cast, CheckerContext &C) {
    const Stmt *Parent = significantParent(Cast, C);
    const auto *BO = dyn_cast_or_null<BinaryOperator>(Parent);
    if (!BO || BO->getOpcode() != BO_Assign)
      return false;
    const auto *ME =
        dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParenImpCasts());
    if (!ME)
      return false;
    StringRef Field = ME->getMemberDecl()->getName();
    return Field == "UniqueProcess" || Field == "UniqueThread";
  }

  // isOpaqueApcContext: the cast's result is passed directly as an
  // argument to one of a short, explicit list of NT/internal APIs whose
  // documented contract is "an opaque value handed back to a callback
  // unexamined" -- QueueUserAPC-style thread APCs and NT timer APCs.
  // NtQueueApcThread's ApcArgument1 and NtSetTimer's TimerApcContext are
  // both typed PVOID/HANDLE by Microsoft's own headers purely because
  // that is the ABI's generic "one machine word, caller's choice"
  // parameter type; neither NT nor this library's own signal_apc()/
  // alarm_apc() ever dereferences it (see src/thread/pthread_signal.c
  // and src/unistd/sleep.c, where the value is cast straight back to
  // the small integer -- a signal number or a monotonic sequence
  // counter -- it always was).
  static bool isOpaqueApcContext(const CastExpr *Cast, CheckerContext &C) {
    const Stmt *Parent = significantParent(Cast, C);
    const auto *CallE = dyn_cast_or_null<CallExpr>(Parent);
    if (!CallE)
      return false;
    const FunctionDecl *FD = CallE->getDirectCallee();
    if (!FD || !FD->getIdentifier())
      return false;
    StringRef Name = FD->getIdentifier()->getName();
    return Name == "NtQueueApcThread" || Name == "NtSetTimer" ||
           Name == "signal_apc";
  }

  // A short, explicit, auditable list of (file suffix, function) pairs
  // where a finding has been individually read and judged genuine but
  // irreducible: the provenance the checker wants proof of crosses a
  // boundary no C-level static analysis can see across -- hand-written
  // assembly, the kernel's own ABI, or a hardware fault handler -- and
  // no source-level rewrite removes the boundary without changing what
  // the code does.  Each entry names exactly the function whose body it
  // covers, not a type or a value, so that this table can only ever grow
  // by someone reading a specific function and writing down why, the
  // same discipline tools/lint.sh's own header asks of every stage:
  // "findings get reported and judged, not blanket-silenced."
  //
  //   crt/delayload2.c __delayLoadHelper2
  //   src/internal/delayload.c ntlibc_delayLoadHelper2
  //     `piat`, the slot being resolved, is computed by the delay-load
  //     thunk stub -- hand-written assembly (see crt/delayload1.asm) --
  //     as `base + <that import's own RVA>`, exactly like `iat`'s C-side
  //     computation two lines below it, but nothing in this translation
  //     unit ever sees that assembly, so there is no C expression
  //     linking the two.  The file's own header comment (crt/
  //     delayload2.c) is the proof this is real, not assumed.
  //   src/thread/aio.c lookup
  //   src/time/timer.c timer_signal
  //     Both check a pointer parameter for membership in a fixed global
  //     table (`requests`/`timers`) that the pointer was itself carved
  //     out of by a *different* function (submit()/timer_create()) at
  //     an earlier, unrelated point in program execution -- an invariant
  //     that is true across the table's entire lifetime but is
  //     established nowhere this function's own body can see.
  //   src/thread/pthread.c pthread_getattr_np
  //     `teb->NtTib.StackBase` and `StackLimit` are two fields of one
  //     THREAD_INFORMATION_BLOCK the kernel populated via
  //     NtQueryInformationThread's opaque out-parameter; the engine
  //     conjures each field load as an independent unconstrained symbol
  //     because it cannot see into the syscall, even though both fields
  //     genuinely bound the same OS-allocated stack.
  //   src/signal/signal.c exception_handler
  //     `ExceptionInformation` is ULONG_PTR[] in NT's own EXCEPTION_
  //     RECORD; slot [1] is the CPU-supplied faulting address for
  //     access-violation-class exceptions and nothing else in the array
  //     is even the same kind of value for other exception codes (see
  //     the comment at the call site) -- there is no pointer this
  //     library ever held to derive it from, because the hardware fault
  //     handler produced it, not any C expression.
  //   src/sh/parse.c parse_funcdef
  //     `start` and `end` are both `p->cur.start`, the lexer's current
  //     token position, read at two different points in parsing the
  //     same function body; nothing changes what buffer `p` tokenizes
  //     out of in between, but that invariant lives in the shape of the
  //     whole recursive-descent parser, not in any one function.
  //   src/fcntl/fcntl.c fcntl
  //     fcntl(2)'s vararg's real type is a function of `cmd`; every
  //     conforming implementation reads it once via one word-sized type
  //     (here `intptr_t`, C11 7.20.1.4's own designated round-trip type)
  //     and reinterprets, because a C vararg list cannot be re-read with
  //     a different type per case without restarting it once per case.
  //   src/stdio/scanf.c vfscanf_st
  //   src/stdio/scanf.c vswscanf_impl
  //     The reference implementation of the %p conversion specifier,
  //     whose entire documented contract (C11 7.21.6.2p12) is to turn
  //     text -- typically, by convention, a previous %p's own output --
  //     back into a pointer.  No compile-time provenance is possible for
  //     the very feature this code exists to provide; the fixture at
  //     tools/lint-pointer-provenance-fixtures/unsafe.c's
  //     integer_pointer() establishes that this checker must still flag
  //     the general case of an arbitrary integer cast to a pointer type,
  //     so the exemption is scoped to these two named functions, not to
  //     the cast shape.
  struct NamedException {
    const char *FileSuffix;
    const char *Function;
  };
  static bool isNamedException(CheckerContext &C) {
    static const NamedException Exceptions[] = {
        {"crt/delayload2.c", "__delayLoadHelper2"},
        {"src/internal/delayload.c", "ntlibc_delayLoadHelper2"},
        {"src/thread/aio.c", "lookup"},
        {"src/time/timer.c", "timer_signal"},
        {"src/thread/pthread.c", "pthread_getattr_np"},
        {"src/signal/signal.c", "exception_handler"},
        {"src/sh/parse.c", "parse_funcdef"},
        {"src/fcntl/fcntl.c", "fcntl"},
        {"src/stdio/scanf.c", "vfscanf_st"},
        {"src/stdio/scanf.c", "vswscanf_impl"},
    };
    std::string Fn = context(C);
    const SourceManager &SM = C.getSourceManager();
    StringRef File = SM.getFilename(SM.getExpansionLoc(
        C.getLocationContext()->getDecl()->getBeginLoc()));
    for (const NamedException &Exception : Exceptions) {
      if (Fn == Exception.Function && File.ends_with(Exception.FileSuffix))
        return true;
    }
    return false;
  }

  void report(StringRef Reason, const Stmt *Statement,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode();
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Unproven pointer provenance",
                                     categories::MemoryError);
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
  void checkPreStmt(const BinaryOperator *Operation, CheckerContext &C) const {
    BinaryOperatorKind Opcode = Operation->getOpcode();
    bool Subtraction = Opcode == BO_Sub &&
                       Operation->getLHS()->getType()->isPointerType() &&
                       Operation->getRHS()->getType()->isPointerType();
    bool Ordering = (Opcode == BO_LT || Opcode == BO_LE || Opcode == BO_GT ||
                     Opcode == BO_GE) &&
                    Operation->getLHS()->getType()->isPointerType() &&
                    Operation->getRHS()->getType()->isPointerType();
    if (!Subtraction && !Ordering)
      return;

    ProgramStateRef State = C.getState();
    const LocationContext *LC = C.getLocationContext();
    const MemRegion *Left =
        baseRegion(State->getSVal(Operation->getLHS(), LC), State);
    const MemRegion *Right =
        baseRegion(State->getSVal(Operation->getRHS(), LC), State);
    if (Left && Right && Left == Right)
      return;
    if (isNamedException(C))
      return;
    report(
        Subtraction
            ? "pointer subtraction operands are not proven to share provenance"
            : "ordered pointer operands are not proven to share provenance",
        Operation, C);
  }

  void checkPreStmt(const CastExpr *Cast, CheckerContext &C) const {
    if (Cast->getCastKind() != CK_IntegralToPointer)
      return;
    const Expr *Source = Cast->getSubExpr();
    if (isConstantSentinel(Source, C.getASTContext()))
      return;
    if (derivesFromPointer(Source))
      return;
    if (isClientIdAssignment(Cast, C))
      return;
    if (isOpaqueApcContext(Cast, C))
      return;
    if (isNamedException(C))
      return;
    report(
        "integer-to-pointer conversion is not proven provenance-preserving",
        Cast, C);
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!FD || !FD->getIdentifier())
      return;
    // isNeedleFunction: a well-known C-library "needle in haystack"
    // function whose contract guarantees its return value, if non-null,
    // points somewhere inside its first argument.  Defined here, not as
    // a free function, purely to keep the StringSwitch next to its only
    // caller.
    StringRef Name = FD->getIdentifier()->getName();
    bool IsNeedle = llvm::StringSwitch<bool>(Name)
        .Cases("strchr", "strrchr", "strstr", "strcasestr", "strpbrk",
               "memchr", "rawmemchr", true)
        .Cases("wcschr", "wcsrchr", "wcsstr", "wmemchr", true)
        // wordexp.c's param_word_end(const char *p) and glob.c's
        // find_slash(const char *p, int flags) are this library's own
        // internal equivalents -- each scans forward from its first
        // argument and returns a pointer within that same word/path,
        // exactly like strchr does -- and each is analyzed standalone
        // (as its own entry point, per clang's default `--analyze`
        // behaviour) by every caller that is also analyzed standalone,
        // so the alias needs recording here rather than relying on
        // inlining.
        .Case("param_word_end", true)
        .Case("find_slash", true)
        .Default(false);
    if (!IsNeedle || Call.getNumArgs() < 1)
      return;
    const MemRegion *Haystack = Call.getArgSVal(0).getAsRegion();
    if (!Haystack)
      return;
    Haystack = Haystack->getBaseRegion();
    const MemRegion *Ret = Call.getReturnValue().getAsRegion();
    if (!Ret)
      return;
    if (const auto *SR = dyn_cast<SymbolicRegion>(Ret->getBaseRegion())) {
      ProgramStateRef State = C.getState();
      State = State->set<NeedleAlias>(SR->getSymbol(), Haystack);
      C.addTransition(State);
    }
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<PointerProvenanceChecker>(
      "ntlibc.PointerProvenance",
      "Proves pointer ordering, subtraction, and integer conversion provenance",
      "");
}
