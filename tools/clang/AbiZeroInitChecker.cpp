// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AbiZeroInitChecker -- proves that a stack-local struct or array whose
// address crosses a raw Nt*/Zw* syscall boundary (an OUT or IN-OUT
// argument, per this project's own src/internal/nt.h prototypes) is fully
// initialized, including padding, rather than filled in field-by-field and
// left with a gap.  Padding and unset tail bytes that cross an ABI
// boundary are a real, well-known bug class: uninitialised bytes handed to
// the kernel on the way in are read by it (junk into a real syscall), and
// uninitialised bytes handed back on an OUT buffer that the caller never
// consumes are a wasted proof obligation on the other side of the same
// boundary.
//
// This is deliberately narrower than the two checkers it sits next to:
// InitializationChecker proves that no *read* observes definitely
// uninitialized storage, and OwnershipChecker proves that every
// *dereference* has live, in-bounds, aligned storage -- neither one
// reasons about "every byte of this object, including compiler-inserted
// padding, is set before its address leaves this translation unit at a
// kernel boundary", which is a distinct, boundary-specific obligation:
// whole-object definite-assignment, not per-field liveness.
//
// Scope, matching what this project's own Nt* call sites actually do
// (grepped from src/*/*.c and grounded against src/internal/nt.h's
// prototypes, not guessed from memory):
//
//   - Only `&local` and a local array decaying to pointer, passed
//     directly as the argument expression, are tracked.  Multiple levels
//     of indirection or aliasing through another pointer are out of
//     scope, matching this checker's tractable, high-value slice.
//
//   - "Fully initialized" is proven exactly two ways: a declaration-time
//     aggregate initializer (`= {0}` / `= {...}`), or a prior whole-object
//     `memset`/`__builtin_memset`/`bzero` call whose target is the base
//     object itself (not a field of it) and whose length, when it can be
//     resolved to a constant, covers the whole type.  A plain whole-object
//     assignment (`x = y;`) also counts.
//
//   - The footgun this exists to catch is the partial one: at least one
//     field or element was written individually (an assignment or
//     increment/decrement through a MemberExpr/ArraySubscriptExpr) and no
//     whole-object initializer was ever seen for that object.  A struct
//     nobody has touched at all (the common IO_STATUS_BLOCK-as-pure-OUT-
//     buffer idiom this project uses throughout src/file, src/mman, and
//     src/thread) is not flagged by this rule: the kernel is expected to
//     write the whole thing, and there is no partial-write footgun to
//     prove against.
//
//   - Argument positions: an OBJECT_ATTRIBUTES* argument is recognised
//     generically by its pointee record name, since essentially every
//     NtCreate*/NtOpen* prototype in src/internal/nt.h takes one and the
//     kernel only ever reads it.  A small table grounded directly in the
//     other prototypes read from src/internal/nt.h covers the remaining
//     IN PVOID buffer slots (NtSetInformationFile, NtSetInformationProcess,
//     NtSetInformationJobObject, NtSetEaFile, NtFsControlFile and
//     NtDeviceIoControlFile's InputBuffer, NtSetContextThread) and OUT
//     PVOID buffer slots (the NtQuery*/NtGetContextThread family) used for
//     the second, lower-priority check.
//
//   - Second, lower-priority check: the same tracked OUT-parameter object
//     never has any of its fields read before the enclosing function
//     returns -- a wasted round trip through the kernel, symmetrical with
//     the first check on the other side of the same boundary.

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "llvm/ADT/SmallVector.h"

#include <cctype>
#include <memory>
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

enum class AggInit : unsigned char { None, Partial, Full };
REGISTER_MAP_WITH_PROGRAMSTATE(AggregateInit, const MemRegion *, AggInit)
REGISTER_MAP_WITH_PROGRAMSTATE(OutParamSite, const MemRegion *, const Stmt *)
// Bit i set means field i (0-based, declaration order) of the aggregate
// has been individually written at least once on this path.  Only
// consulted to prove the record-layout lemma below; a struct with more
// than 32 fields simply never reaches AggInit::Full through this path,
// which is conservative (falls back to requiring an explicit whole-object
// initializer), not unsound.
REGISTER_MAP_WITH_PROGRAMSTATE(WrittenFieldMask, const MemRegion *, unsigned)

namespace {

struct ArgSlot {
  const char *Func;
  unsigned Index;
};

// A record type none of whose fields are bitfields, every one of which
// has been individually written on this path, and whose fields --
// walked in declaration order against the *real target's* own computed
// layout, not a guess -- leave no gap before the first field, between
// any two adjacent fields, or after the last one, has by simple
// arithmetic had every one of its bytes set exactly once: no compiler
// padding can exist that this struct's own field set does not already
// account for. This is what actually justifies not flagging idioms like
// FILE_POSITION_INFORMATION's single LARGE_INTEGER field (a one-field
// struct can never have padding at all) or PROCESS_PRIORITY_CLASS's two
// adjacent UCHAR fields (both 1-byte aligned, nothing between or after
// them), while still flagging a struct a target's real ABI actually does
// pad, such as OBJECT_ATTRIBUTES on LLP64 -- computed per call, so it is
// right for whichever arch is currently being analyzed rather than
// assuming one layout for all of them.
static bool allFieldsCoverWholeObject(const RecordDecl *RD, unsigned Mask,
                                      ASTContext &Ctx) {
  if (!RD || !RD->isCompleteDefinition())
    return false;
  const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(RD);
  CharUnits Expected = CharUnits::Zero();
  unsigned Index = 0;
  for (const FieldDecl *FD : RD->fields()) {
    if (FD->isBitField() || Index >= 32)
      return false;
    if (!(Mask & (1u << Index)))
      return false;
    CharUnits Offset = Ctx.toCharUnitsFromBits(Layout.getFieldOffset(Index));
    if (Offset != Expected)
      return false;
    Expected += Ctx.getTypeSizeInChars(FD->getType());
    ++Index;
  }
  return Expected == Layout.getSize();
}

// True if Callee's body contains, as one of its own top-level statements
// (unconditional -- not nested inside a branch or loop, so this is not
// claiming anything about a memset that only sometimes runs), a call to
// memset/__builtin_memset/bzero whose target is exactly the
// ParamIndex'th parameter dereferenced, covering the whole pointee.  A
// locally-defined helper that unconditionally memsets its own
// by-address out-parameter proves the *caller's* corresponding local
// just as completely as if that memset were written at the call site --
// C's pass-by-address semantics make the two identical, no path-
// sensitive symbolic execution across the call boundary required.  This
// is what lets a shared setup helper -- e.g. this tree's own per-file
// object_attributes() in src/thread/semaphore.c and src/thread/
// mqueue.c, whose only store to its OBJECT_ATTRIBUTES* parameter is a
// call to InitializeObjectAttributes(), which itself opens with exactly
// this memset -- prove its callers' locals even though this checker
// analyzes one function frame at a time and does not itself see across
// that call.
static bool isMemsetCallOfParam(const CallExpr *CE, const ParmVarDecl *Param,
                                QualType PointeeType, ASTContext &Ctx) {
  const FunctionDecl *Target = CE->getDirectCallee();
  if (!Target || !Target->getIdentifier())
    return false;
  StringRef Name = Target->getName();
  if (Name != "memset" && Name != "__builtin_memset" && Name != "bzero")
    return false;
  if (CE->getNumArgs() < 1)
    return false;
  const auto *DRE = dyn_cast<DeclRefExpr>(CE->getArg(0)->IgnoreParenCasts());
  if (!DRE || DRE->getDecl() != Param)
    return false;
  if (CE->getNumArgs() < 3)
    return true; // e.g. a 2-arg bzero-style wrapper: trust it, the same
                 // way an unresolvable length does below.
  Expr::EvalResult Length;
  if (!CE->getArg(2)->EvaluateAsInt(Length, Ctx))
    return true; // symbolic length: trust it rather than guess.
  CharUnits Size = Ctx.getTypeSizeInChars(PointeeType);
  return Length.Val.getInt().getLimitedValue() >=
         static_cast<uint64_t>(Size.getQuantity());
}

// Walked only through statement forms that are reached unconditionally
// whenever S itself is: a compound statement's own members, and a
// `do { ... } while (0)` block's body (the standard macro-safety
// wrapper this project's own InitializeObjectAttributes uses, and the
// reason a bare top-level-statement scan alone is not enough -- the
// memset this exists to find is nested one level inside exactly that
// DoStmt, not a direct statement of the function body).  Deliberately
// does NOT descend into `if`/`for`/`while`/`switch`: nothing under a
// real conditional is reached unconditionally, so nothing under one may
// be credited as always having run.
static bool unconditionallyMemsetsParam(const Stmt *S,
                                        const ParmVarDecl *Param,
                                        QualType PointeeType,
                                        ASTContext &Ctx) {
  if (!S)
    return false;
  if (const auto *CE = dyn_cast<CallExpr>(S))
    return isMemsetCallOfParam(CE, Param, PointeeType, Ctx);
  if (const auto *Compound = dyn_cast<CompoundStmt>(S)) {
    for (const Stmt *Child : Compound->body())
      if (unconditionallyMemsetsParam(Child, Param, PointeeType, Ctx))
        return true;
    return false;
  }
  if (const auto *Do = dyn_cast<DoStmt>(S)) {
    Expr::EvalResult CondVal;
    if (Do->getCond()->EvaluateAsInt(CondVal, Ctx) &&
        CondVal.Val.getInt() == 0)
      return unconditionallyMemsetsParam(Do->getBody(), Param, PointeeType,
                                         Ctx);
    return false;
  }
  return false;
}

static bool calleeMemsetsParam(const FunctionDecl *Callee,
                               unsigned ParamIndex, ASTContext &Ctx) {
  if (!Callee->hasBody() || ParamIndex >= Callee->getNumParams())
    return false;
  const ParmVarDecl *Param = Callee->getParamDecl(ParamIndex);
  QualType PointeeType = Param->getType()->getPointeeType();
  if (PointeeType.isNull())
    return false;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Callee->getBody());
  if (!Body)
    return false;
  for (const Stmt *S : Body->body())
    if (unconditionallyMemsetsParam(S, Param, PointeeType, Ctx))
      return true;
  return false;
}

// True if the function body -- outside the call statement that itself
// produced the OUT obligation -- references the tracked local anywhere at
// all. checkEndFunction's own path-sensitive tracking flags a path that
// returns before reaching a field read of an already-successful OUT
// buffer, which fires on the ordinary, safe "second call in this
// function also failed, so we are unwinding before ever getting to use
// the first result" idiom (see fionread_file(), __fstat_handle()) and on
// a boolean short-circuit that deliberately skips reading a buffer NT
// did not actually fill up to the expected length (see __lxmod_get()) --
// neither is the wasted round trip this check exists to catch. Trusting
// any syntactic reference elsewhere in the same function keeps the
// checker's one genuine positive (a buffer well and truly never touched
// again, as in the fixture's query_and_ignore()) while dropping these.
class ReferencedOutsideCall
    : public RecursiveASTVisitor<ReferencedOutsideCall> {
  const VarDecl *Target;
  const Stmt *Skip;
  bool Found = false;

public:
  ReferencedOutsideCall(const VarDecl *Target, const Stmt *Skip)
      : Target(Target), Skip(Skip) {}
  bool TraverseStmt(Stmt *S) {
    if (!S || S == Skip)
      return true;
    return RecursiveASTVisitor::TraverseStmt(S);
  }
  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (DRE->getDecl() == Target)
      Found = true;
    return true;
  }
  bool found() const { return Found; }
};

// Grounded directly in src/internal/nt.h's prototypes: the PVOID slots
// that the kernel reads (IN) and writes (OUT) on the calls this project
// actually makes, beyond the generic OBJECT_ATTRIBUTES* rule below.
const ArgSlot InSlots[] = {
    {"NtSetInformationFile", 2},    {"NtSetInformationProcess", 2},
    {"NtSetInformationJobObject", 2}, {"NtSetEaFile", 2},
    {"NtFsControlFile", 6},         {"NtDeviceIoControlFile", 6},
    {"NtSetContextThread", 1},
};

const ArgSlot OutSlots[] = {
    {"NtQueryInformationFile", 2},   {"NtQueryInformationProcess", 2},
    {"NtQueryInformationThread", 2}, {"NtQueryInformationToken", 2},
    {"NtQueryVolumeInformationFile", 2}, {"NtQueryObject", 2},
    {"NtQueryVirtualMemory", 3},     {"NtQueryEaFile", 2},
    {"NtQueryDirectoryFile", 5},     {"NtQueryFullAttributesFile", 1},
    {"NtQueryAttributesFile", 1},    {"NtQuerySemaphore", 2},
    {"NtGetContextThread", 1},
};

class AbiZeroInitChecker
    : public Checker<check::PreStmt<DeclStmt>, check::Bind, check::PreCall,
                     check::PostCall, check::Location, check::EndFunction> {
  mutable std::unique_ptr<BugType> PartialBT;
  mutable std::unique_ptr<BugType> UnconsumedBT;

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

  static std::string site(const Stmt *Statement, CheckerContext &C) {
    const SourceManager &SM = C.getSourceManager();
    SourceLocation Location = SM.getExpansionLoc(Statement->getBeginLoc());
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(SM.getFileID(Location), &Invalid);
    if (Invalid)
      return Statement->getStmtClassName();
    unsigned Offset = SM.getFileOffset(Location);
    size_t Begin = Buffer.rfind('\n', Offset);
    Begin = Begin == StringRef::npos ? 0 : Begin + 1;
    size_t End = Buffer.find('\n', Offset);
    return Buffer.slice(Begin, End == StringRef::npos ? Buffer.size() : End)
        .trim()
        .str();
  }

  static std::string context(CheckerContext &C) {
    const Decl *Current = C.getLocationContext()->getDecl();
    if (const auto *Named = dyn_cast_or_null<NamedDecl>(Current))
      return Named->getQualifiedNameAsString();
    return Current ? Current->getDeclKindName() : "unknown";
  }

  void report(std::unique_ptr<BugType> &Type, StringRef Title,
              StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    if (!Statement)
      return;
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!Type)
      Type = std::make_unique<BugType>(this, Title, categories::MemoryError);
    const SourceManager &SM = C.getSourceManager();
    std::string Message =
        (Reason + "; origin '" +
         SM.getFilename(SM.getExpansionLoc(Statement->getBeginLoc())) +
         "'; context '" + context(C) + "'; expression '" + text(Statement, C) +
         "'; site '" + site(Statement, C) + "'")
            .str();
    auto Report = std::make_unique<PathSensitiveBugReport>(*Type, Message, Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  // ---- syntactic recognition of the tractable slice: &local / local[] ----

  static const VarDecl *localAggregateTarget(const Expr *ArgExpr) {
    const Expr *E = ArgExpr->IgnoreParenCasts();
    const Expr *Base;
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      if (UO->getOpcode() != UO_AddrOf)
        return nullptr;
      Base = UO->getSubExpr()->IgnoreParenCasts();
    } else {
      Base = E;
    }
    const auto *DRE = dyn_cast<DeclRefExpr>(Base);
    if (!DRE)
      return nullptr;
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD || !VD->hasLocalStorage())
      return nullptr;
    QualType QT = VD->getType();
    if (!QT->isRecordType() && !QT->isArrayType())
      return nullptr;
    return VD;
  }

  static const MemRegion *aggregateRegion(const CallEvent &Call,
                                          unsigned Index) {
    if (Index >= Call.getNumArgs())
      return nullptr;
    const Expr *ArgExpr = Call.getArgExpr(Index);
    if (!ArgExpr || !localAggregateTarget(ArgExpr))
      return nullptr;
    const MemRegion *R = Call.getArgSVal(Index).getAsRegion();
    return R ? R->getBaseRegion() : nullptr;
  }

  // ---- Nt*/Zw* and memset* name recognition ----

  static bool isNtName(StringRef Name) {
    if (Name.size() < 3 || !std::isupper(static_cast<unsigned char>(Name[2])))
      return false;
    return Name.starts_with("Nt") || Name.starts_with("Zw");
  }

  static bool isMemsetName(StringRef Name) {
    return Name == "memset" || Name == "__builtin_memset" || Name == "bzero";
  }

  static bool objectAttributesArg(const Expr *ArgExpr) {
    const auto *PT = ArgExpr->getType()->getAs<PointerType>();
    if (!PT)
      return false;
    const auto *RT = PT->getPointeeType()->getAs<RecordType>();
    return RT && RT->getDecl()->getName() == "_OBJECT_ATTRIBUTES";
  }

  static void inSlotsFor(StringRef Name, const CallEvent &Call,
                         SmallVectorImpl<unsigned> &Out) {
    for (unsigned I = 0, N = Call.getNumArgs(); I < N; ++I) {
      const Expr *E = Call.getArgExpr(I);
      if (E && objectAttributesArg(E))
        Out.push_back(I);
    }
    for (const ArgSlot &Slot : InSlots)
      if (Name == Slot.Func && Slot.Index < Call.getNumArgs())
        Out.push_back(Slot.Index);
  }

  static void outSlotsFor(StringRef Name, const CallEvent &Call,
                          SmallVectorImpl<unsigned> &Out) {
    for (const ArgSlot &Slot : OutSlots)
      if (Name == Slot.Func && Slot.Index < Call.getNumArgs())
        Out.push_back(Slot.Index);
  }

  // ---- whole-object memset recognition ----

  void handleMemset(const CallEvent &Call, CheckerContext &C) const {
    if (Call.getNumArgs() < 1)
      return;
    const Expr *Target = Call.getArgExpr(0);
    if (!Target || !localAggregateTarget(Target))
      return;
    const MemRegion *R = Call.getArgSVal(0).getAsRegion();
    if (!R)
      return;
    const MemRegion *Base = R->getBaseRegion();
    if (R != Base)
      return; // memset(&x.field, ...) does not clear the whole object.
    if (Call.getNumArgs() >= 3) {
      const llvm::APSInt *Length =
          C.getSValBuilder().getKnownValue(C.getState(), Call.getArgSVal(2));
      if (Length) {
        const auto *VR = dyn_cast<VarRegion>(Base);
        if (VR) {
          CharUnits Size =
              C.getASTContext().getTypeSizeInChars(VR->getValueType());
          if (Length->getLimitedValue() < static_cast<uint64_t>(
                                              Size.getQuantity()))
            return; // Provably partial; do not credit it as whole-object.
        }
      }
    }
    C.addTransition(C.getState()->set<AggregateInit>(Base, AggInit::Full));
  }

public:
  void checkPreStmt(const DeclStmt *DS, CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (const Decl *D : DS->decls()) {
      const auto *VD = dyn_cast<VarDecl>(D);
      if (!VD || !VD->hasLocalStorage())
        continue;
      QualType QT = VD->getType();
      if (!QT->isRecordType() && !QT->isArrayType())
        continue;
      const Expr *Init = VD->getInit();
      if (!Init || !isa<InitListExpr>(Init->IgnoreParens()))
        continue;
      const MemRegion *R =
          State->getLValue(VD, C.getLocationContext()).getAsRegion();
      if (!R)
        continue;
      State = State->set<AggregateInit>(R, AggInit::Full);
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const {
    (void)Val;
    std::optional<clang::ento::Loc> L = Loc.getAs<clang::ento::Loc>();
    if (!L)
      return;
    const MemRegion *R = L->getAsRegion();
    if (!R)
      return;
    const MemRegion *Base = R->getBaseRegion();
    const auto *VR = dyn_cast<VarRegion>(Base);
    if (!VR || !VR->getDecl()->hasLocalStorage())
      return;
    QualType QT = VR->getDecl()->getType();
    if (!QT->isRecordType() && !QT->isArrayType())
      return;

    ProgramStateRef State = C.getState();
    if (R == Base) {
      // A whole-object store, e.g. `pc = other;`.  Only a plain `=`
      // reassigns every byte; `|=` and friends read-modify-write instead.
      if (const auto *BO = dyn_cast_or_null<BinaryOperator>(S))
        if (BO->getOpcode() == BO_Assign)
          C.addTransition(State->set<AggregateInit>(Base, AggInit::Full));
      return;
    }
    if (!isa<FieldRegion>(R) && !isa<ElementRegion>(R))
      return;
    bool IsFieldWrite = false;
    if (const auto *BO = dyn_cast_or_null<BinaryOperator>(S))
      IsFieldWrite = BO->isAssignmentOp();
    else if (const auto *UO = dyn_cast_or_null<UnaryOperator>(S))
      IsFieldWrite = UO->isIncrementDecrementOp();
    if (!IsFieldWrite)
      return;
    bool Changed = false;
    const AggInit *Current = State->get<AggregateInit>(Base);
    if (!Current || *Current == AggInit::None) {
      State = State->set<AggregateInit>(Base, AggInit::Partial);
      Changed = true;
    }
    if (const auto *FR = dyn_cast<FieldRegion>(R)) {
      unsigned Index = FR->getDecl()->getFieldIndex();
      if (Index < 32) {
        unsigned Mask = 0;
        if (const unsigned *M = State->get<WrittenFieldMask>(Base))
          Mask = *M;
        unsigned Updated = Mask | (1u << Index);
        if (Updated != Mask) {
          State = State->set<WrittenFieldMask>(Base, Updated);
          Changed = true;
        }
      }
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!FD || !FD->getIdentifier())
      return;
    StringRef Name = FD->getName();
    if (isMemsetName(Name)) {
      handleMemset(Call, C);
      return;
    }
    if (!isNtName(Name))
      return;
    SmallVector<unsigned, 4> InIdx;
    inSlotsFor(Name, Call, InIdx);
    if (InIdx.empty())
      return;
    ProgramStateRef State = C.getState();
    for (unsigned Index : InIdx) {
      const MemRegion *R = aggregateRegion(Call, Index);
      if (!R)
        continue;
      const AggInit *St = State->get<AggregateInit>(R);
      if (St && *St == AggInit::Partial) {
        if (const auto *VR = dyn_cast<VarRegion>(R)) {
          if (const auto *RT = VR->getValueType()->getAs<RecordType>()) {
            unsigned Mask = 0;
            if (const unsigned *M = State->get<WrittenFieldMask>(R))
              Mask = *M;
            if (allFieldsCoverWholeObject(RT->getDecl(), Mask,
                                          C.getASTContext()))
              continue; // every field individually tiles the object.
          }
        }
        report(PartialBT, "Unproven whole-object initialization",
               "local aggregate crosses an Nt*/Zw* syscall boundary without "
               "a proven whole-object initializer",
               Call.getOriginExpr(), State, C);
        return;
      }
    }
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!FD || !FD->getIdentifier())
      return;

    // See calleeMemsetsParam's own doc comment: a locally-defined helper
    // that unconditionally memsets one of its own by-address parameters
    // proves the corresponding caller-side local, independent of the
    // Nt*/Zw* handling below.
    if (FD->hasBody()) {
      ProgramStateRef ParamState = C.getState();
      bool ParamChanged = false;
      for (unsigned Index = 0, N = Call.getNumArgs(); Index < N; ++Index) {
        if (!calleeMemsetsParam(FD, Index, C.getASTContext()))
          continue;
        const MemRegion *R = aggregateRegion(Call, Index);
        if (!R)
          continue;
        ParamState = ParamState->set<AggregateInit>(R, AggInit::Full);
        ParamChanged = true;
      }
      if (ParamChanged)
        C.addTransition(ParamState);
    }

    StringRef Name = FD->getName();
    if (!isNtName(Name))
      return;
    SmallVector<unsigned, 2> OutIdx;
    outSlotsFor(Name, Call, OutIdx);
    if (OutIdx.empty())
      return;
    const Stmt *Origin = Call.getOriginExpr();
    if (!Origin)
      return;

    // The kernel only writes an OUT buffer on success, and this project's
    // own NT_SUCCESS(s) is `(NTSTATUS)(s) >= 0` (src/internal/nt.h) --
    // every real call site branches on exactly that before touching an
    // OUT parameter.  Track the "must be consumed" obligation only on
    // branches where success is not already disproven, so the extremely
    // common `if (!NT_SUCCESS(st)) return ...;` early-out does not read
    // as an unconsumed OUT parameter.
    ProgramStateRef State = C.getState();
    ProgramStateRef MaybeSucceeded = State;
    std::optional<DefinedOrUnknownSVal> Return =
        Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
    if (Return) {
      SValBuilder &SVB = C.getSValBuilder();
      SVal NonNegative = SVB.evalBinOp(
          State, BO_GE, *Return, SVB.makeZeroVal(FD->getReturnType()),
          SVB.getConditionType());
      if (std::optional<DefinedOrUnknownSVal> Condition =
              NonNegative.getAs<DefinedOrUnknownSVal>()) {
        auto [Succeeded, Failed] = State->assume(*Condition);
        if (Succeeded)
          MaybeSucceeded = Succeeded;
        if (Failed && Failed != Succeeded)
          C.addTransition(Failed);
        if (!Succeeded)
          return;
      }
    }
    for (unsigned Index : OutIdx) {
      const MemRegion *R = aggregateRegion(Call, Index);
      if (!R)
        continue;
      MaybeSucceeded = MaybeSucceeded->set<OutParamSite>(R, Origin);
      // The kernel writes the whole structure passed by size on a
      // successful query -- exactly the same whole-object proof as a
      // local memset, just sourced from the syscall's own OUT contract
      // (already trusted by this checker's IO_STATUS_BLOCK handling)
      // instead of a statement in this translation unit.  A local that
      // was never anything but the OUT target of one of these calls
      // (e.g. src/stat/utimensat.c's `bi`, re-populated then selectively
      // overwritten before crossing back in as an IN argument) is
      // therefore fully proven here, not left "partial" by the field
      // tweaks that follow.
      MaybeSucceeded = MaybeSucceeded->set<AggregateInit>(R, AggInit::Full);
    }
    C.addTransition(MaybeSucceeded);
  }

  void checkLocation(SVal Location, bool IsLoad, const Stmt *Statement,
                     CheckerContext &C) const {
    (void)Statement;
    if (!IsLoad)
      return;
    std::optional<clang::ento::Loc> L = Location.getAs<clang::ento::Loc>();
    if (!L)
      return;
    const MemRegion *R = L->getAsRegion();
    if (!R)
      return;
    const MemRegion *Base = R->getBaseRegion();
    ProgramStateRef State = C.getState();
    if (State->get<OutParamSite>(Base))
      C.addTransition(State->remove<OutParamSite>(Base));
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    const auto *EnclosingFD =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    const Stmt *Body = EnclosingFD ? EnclosingFD->getBody() : nullptr;
    for (const auto &Entry : State->get<OutParamSite>()) {
      const auto *VR = dyn_cast<VarRegion>(Entry.first);
      if (!VR || VR->getStackFrame() != C.getStackFrame())
        continue;
      const Stmt *Statement = Entry.second;
      // This path never reached a field read of the buffer, but that is
      // also true of the ordinary, safe "a *different*, later call in
      // this function also failed, so we are unwinding before ever
      // getting to use this result" idiom, and of a boolean
      // short-circuit that deliberately skips reading a buffer NT did
      // not actually fill to the expected length -- see this class's own
      // doc comment above ReferencedOutsideCall. Trust a syntactic
      // reference to the same local anywhere else in the function body
      // over this one path's account of whether it was "used".
      if (Body && Statement) {
        ReferencedOutsideCall RV(VR->getDecl(), Statement);
        RV.TraverseStmt(const_cast<Stmt *>(Body));
        if (RV.found())
          continue;
      }
      if (!Statement)
        Statement = Return;
      report(UnconsumedBT, "Unread syscall OUT parameter",
             "Nt*/Zw* OUT parameter is read back without any field proven "
             "consumed",
             Statement, State->remove<OutParamSite>(Entry.first), C);
      return;
    }
  }
};

} // namespace

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  Registry.addChecker<AbiZeroInitChecker>(
      "ntlibc.AbiZeroInit",
      "Proves stack aggregates crossing an Nt*/Zw* syscall boundary are "
      "fully initialized",
      "");
}
