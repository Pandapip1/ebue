// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "RelationContracts.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"

#include <cctype>
#include <memory>
#include <optional>
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
REGISTER_MAP_WITH_PROGRAMSTATE(RegistryElement, SymbolRef, const MemRegion *)

namespace {

class PointerProvenanceChecker
    : public Checker<check::PreStmt<BinaryOperator>, check::PreStmt<CastExpr>,
                      check::PreStmt<ReturnStmt>, check::PreCall,
                      check::PostCall, check::BeginFunction, check::Bind> {
  mutable std::unique_ptr<BugType> BT;
  mutable llvm::DenseMap<const FunctionDecl *, bool> RelationEligibility;
  mutable llvm::DenseMap<const VarDecl *, bool> RegistryEligibility;
  mutable llvm::SmallVector<const VarDecl *, 4> ContractRegistries;
  mutable bool ContractRegistriesInitialized = false;

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

  using RelationContract = ntlibc::ElementRelationContract;

  static std::optional<RelationContract>
  relationContract(const FunctionDecl *FD, ntlibc::ElementRelationKind Kind,
                   std::optional<unsigned> Parameter = std::nullopt) {
    if (!FD)
      return std::nullopt;
    for (const FunctionDecl *Redeclaration : FD->redecls()) {
      for (const auto *Attribute : Redeclaration->specific_attrs<AnnotateAttr>()) {
        std::optional<RelationContract> Contract =
            ntlibc::parseElementRelation(Attribute->getAnnotation());
        if (!Contract || Contract->Kind != Kind)
          continue;
        if (Kind == ntlibc::ElementRelationKind::Return || !Parameter ||
            *Parameter == Contract->Parameter)
          return Contract;
      }
    }
    return std::nullopt;
  }

  static const VarDecl *registryDecl(StringRef Name, ASTContext &Ctx) {
    const VarDecl *Result = nullptr;
    for (const Decl *Declaration : Ctx.getTranslationUnitDecl()->decls()) {
      const auto *Variable = dyn_cast<VarDecl>(Declaration);
      if (!Variable || Variable->getName() != Name ||
          !Variable->getType()->isPointerType() ||
          !Variable->hasGlobalStorage())
        continue;
      if (Result && Result->getCanonicalDecl() != Variable->getCanonicalDecl())
        return nullptr;
      Result = Variable;
    }
    return Result ? Result->getCanonicalDecl() : nullptr;
  }

  const llvm::SmallVectorImpl<const VarDecl *> &
  contractRegistries(ASTContext &Ctx) const {
    if (ContractRegistriesInitialized)
      return ContractRegistries;
    ContractRegistriesInitialized = true;
    for (const Decl *Declaration : Ctx.getTranslationUnitDecl()->decls()) {
      const auto *FD = dyn_cast<FunctionDecl>(Declaration);
      if (!FD)
        continue;
      for (const auto *Attribute : FD->specific_attrs<AnnotateAttr>()) {
        std::optional<RelationContract> Contract =
            ntlibc::parseElementRelation(Attribute->getAnnotation());
        StringRef Name = Contract ? Contract->Registry : StringRef();
        const VarDecl *Registry =
            Name.empty() ? nullptr : registryDecl(Name, Ctx);
        if (Registry && llvm::find(ContractRegistries, Registry) ==
                            ContractRegistries.end())
          ContractRegistries.push_back(Registry);
      }
    }
    return ContractRegistries;
  }

  class RegistryExposureVisitor
      : public RecursiveASTVisitor<RegistryExposureVisitor> {
    const VarDecl *Registry;

    bool mentionsRegistry(const Stmt *Statement) const {
      if (!Statement)
        return false;
      if (const auto *Reference = dyn_cast<DeclRefExpr>(Statement))
        return Reference->getDecl()->getCanonicalDecl() == Registry;
      for (const Stmt *Child : Statement->children())
        if (mentionsRegistry(Child))
          return true;
      return false;
    }

  public:
    bool Exposed = false;
    explicit RegistryExposureVisitor(const VarDecl *Registry)
        : Registry(Registry->getCanonicalDecl()) {}

    bool VisitUnaryOperator(UnaryOperator *Operation) {
      if (Operation->getOpcode() != UO_AddrOf)
        return true;
      const auto *Reference = dyn_cast<DeclRefExpr>(
          Operation->getSubExpr()->IgnoreParenImpCasts());
      if (Reference &&
          Reference->getDecl()->getCanonicalDecl() == Registry)
        Exposed = true;
      return true;
    }

    bool VisitGCCAsmStmt(GCCAsmStmt *Assembly) {
      if (mentionsRegistry(Assembly))
        Exposed = true;
      return true;
    }

    bool VisitMSAsmStmt(MSAsmStmt *Assembly) {
      if (mentionsRegistry(Assembly))
        Exposed = true;
      return true;
    }
  };

  bool registryEligible(const VarDecl *Registry, ASTContext &Ctx) const {
    if (!Registry || Registry->getFormalLinkage() != Linkage::Internal ||
        Registry->getType().isVolatileQualified())
      return false;
    Registry = Registry->getCanonicalDecl();
    if (auto Existing = RegistryEligibility.find(Registry);
        Existing != RegistryEligibility.end())
      return Existing->second;
    bool Eligible = true;
    for (const VarDecl *Redeclaration : Registry->redecls())
      if (Redeclaration->hasAttr<AliasAttr>() ||
          Redeclaration->hasAttr<WeakRefAttr>() ||
          Redeclaration->hasAttr<UsedAttr>() ||
          Redeclaration->hasAttr<RetainAttr>())
        Eligible = false;
    RegistryExposureVisitor Visitor(Registry);
    if (Eligible)
      Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    Eligible = Eligible && !Visitor.Exposed;
    RegistryEligibility[Registry] = Eligible;
    return Eligible;
  }

  static bool directCallReference(const DeclRefExpr *Reference,
                                  const FunctionDecl *FD,
                                  ASTContext &Ctx) {
    DynTypedNode Current = DynTypedNode::create(*Reference);
    for (unsigned Depth = 0; Depth < 8; ++Depth) {
      auto Parents = Ctx.getParents(Current);
      if (Parents.size() != 1)
        return false;
      const DynTypedNode &Parent = Parents[0];
      if (const auto *Call = Parent.get<CallExpr>())
        return Call->getDirectCallee() &&
               Call->getDirectCallee()->getCanonicalDecl() ==
                   FD->getCanonicalDecl();
      const Stmt *Statement = Parent.get<Stmt>();
      if (!Statement ||
          (!isa<ParenExpr>(Statement) && !isa<CastExpr>(Statement)))
        return false;
      Current = Parent;
    }
    return false;
  }

  class FunctionReferenceVisitor
      : public RecursiveASTVisitor<FunctionReferenceVisitor> {
    const FunctionDecl *FD;
    ASTContext &Ctx;

  public:
    bool AddressTaken = false;
    unsigned DirectCalls = 0;
    FunctionReferenceVisitor(const FunctionDecl *FD, ASTContext &Ctx)
        : FD(FD->getCanonicalDecl()), Ctx(Ctx) {}

    bool VisitDeclRefExpr(DeclRefExpr *Reference) {
      const auto *Referenced = dyn_cast<FunctionDecl>(Reference->getDecl());
      if (!Referenced || Referenced->getCanonicalDecl() != FD)
        return true;
      if (directCallReference(Reference, FD, Ctx))
        ++DirectCalls;
      else
        AddressTaken = true;
      return true;
    }
  };

  bool relationEligible(const FunctionDecl *FD, ASTContext &Ctx) const {
    if (!FD || FD->getStorageClass() != SC_Static)
      return false;
    FD = FD->getCanonicalDecl();
    if (auto Existing = RelationEligibility.find(FD);
        Existing != RelationEligibility.end())
      return Existing->second;
    bool Eligible = true;
    for (const FunctionDecl *Redeclaration : FD->redecls())
      if (Redeclaration->hasAttr<AliasAttr>() ||
          Redeclaration->hasAttr<WeakRefAttr>() ||
          Redeclaration->hasAttr<IFuncAttr>() ||
          Redeclaration->hasAttr<UsedAttr>() ||
          Redeclaration->hasAttr<RetainAttr>())
        Eligible = false;
    FunctionReferenceVisitor Visitor(FD, Ctx);
    if (Eligible)
      Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    Eligible = Eligible && !Visitor.AddressTaken && Visitor.DirectCalls > 0;
    RelationEligibility[FD] = Eligible;
    return Eligible;
  }

  static const MemRegion *registryBase(const VarDecl *Registry,
                                       CheckerContext &C) {
    if (!Registry)
      return nullptr;
    ProgramStateRef State = C.getState();
    Loc Location = State->getLValue(Registry, C.getLocationContext());
    return baseRegion(State->getSVal(Location), State);
  }

  static const MemRegion *registryStorage(const VarDecl *Registry,
                                          CheckerContext &C) {
    if (!Registry)
      return nullptr;
    return C.getState()
        ->getLValue(Registry, C.getLocationContext())
        .getAsRegion();
  }

  static const MemRegion *elementRegistry(SVal Value,
                                          ProgramStateRef State) {
    const MemRegion *Region = Value.getAsRegion();
    const auto *Symbol =
        Region ? dyn_cast<SymbolicRegion>(Region->getBaseRegion()) : nullptr;
    if (!Symbol)
      return nullptr;
    const MemRegion *const *Registry =
        State->get<RegistryElement>(Symbol->getSymbol());
    return Registry ? *Registry : nullptr;
  }

  static const VarDecl *directRegistryExpression(const Expr *Expression,
                                                 ASTContext &Ctx) {
    Expression = Expression ? Expression->IgnoreParenImpCasts() : nullptr;
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Expression);
    const auto *Variable =
        Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
    if (!Variable || !Variable->getType()->isPointerType() ||
        !Variable->hasGlobalStorage())
      return nullptr;
    return registryDecl(Variable->getName(), Ctx);
  }

  // The standard strto* family writes either its input pointer or a pointer
  // later in that same array through endptr.  The analyzer correctly gives
  // the stored pointer a fresh symbol, but that symbol otherwise loses the
  // standard library's same-object contract.  Keep the list literal and
  // restricted to the narrow and wide conversion functions whose second
  // argument is endptr.
  static bool isStringConversionFunction(const FunctionDecl *FD) {
    if (!FD || !FD->getIdentifier())
      return false;
    StringRef Name = FD->getName();
    static constexpr llvm::StringLiteral Names[] = {
        "strtod",  "strtof",   "strtold", "strtol",  "strtoll",
        "strtoul", "strtoull", "wcstod",  "wcstof",  "wcstold",
        "wcstol",  "wcstoll",  "wcstoul", "wcstoull"};
    for (StringRef Candidate : Names)
      if (Name == Candidate)
        return true;
    return false;
  }

  // A narrow interprocedural summary for the common static cursor helper:
  // every non-null return is the same pointer parameter, possibly advanced
  // by ordinary pointer arithmetic.  Because the function has internal
  // linkage and the call is direct, its complete definition is available;
  // rejecting address exposure, resets, asm, and non-returning fallthrough
  // makes the returned pointer's array provenance an intrinsic contract of
  // the body rather than a call-site assumption.
  class ReturnedParameterVisitor
      : public RecursiveASTVisitor<ReturnedParameterVisitor> {
    const ParmVarDecl *Candidate;
    ASTContext &Ctx;
    bool Valid = true;
    bool SawDerivedReturn = false;

    bool isCandidate(const Expr *E) const {
      E = E->IgnoreParenImpCasts();
      const auto *DRE = dyn_cast<DeclRefExpr>(E);
      return DRE && DRE->getDecl() == Candidate;
    }

    bool isNull(const Expr *E) const {
      return E->isNullPointerConstant(Ctx,
                                      Expr::NPC_ValueDependentIsNotNull);
    }

    bool derivesFromCandidate(const Expr *E) const {
      E = E->IgnoreParens();
      if (const auto *CE = dyn_cast<CastExpr>(E)) {
        if (CE->getCastKind() == CK_NoOp ||
            CE->getCastKind() == CK_BitCast ||
            CE->getCastKind() == CK_LValueToRValue)
          return derivesFromCandidate(CE->getSubExpr());
        return false;
      }
      if (isCandidate(E))
        return true;
      if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->getOpcode() == BO_Add) {
          return (derivesFromCandidate(BO->getLHS()) &&
                  BO->getRHS()->getType()->isIntegerType()) ||
                 (BO->getLHS()->getType()->isIntegerType() &&
                  derivesFromCandidate(BO->getRHS()));
        }
        if (BO->getOpcode() == BO_Sub)
          return derivesFromCandidate(BO->getLHS()) &&
                 BO->getRHS()->getType()->isIntegerType();
        if (BO->getOpcode() == BO_Comma)
          return derivesFromCandidate(BO->getRHS());
        return false;
      }
      if (const auto *CO = dyn_cast<ConditionalOperator>(E))
        return (isNull(CO->getTrueExpr()) ||
                derivesFromCandidate(CO->getTrueExpr())) &&
               (isNull(CO->getFalseExpr()) ||
                derivesFromCandidate(CO->getFalseExpr()));
      return false;
    }

  public:
    ReturnedParameterVisitor(const ParmVarDecl *Candidate, ASTContext &Ctx)
        : Candidate(Candidate), Ctx(Ctx) {}

    bool VisitReturnStmt(ReturnStmt *Return) {
      const Expr *Value = Return->getRetValue();
      if (!Value) {
        Valid = false;
      } else if (!isNull(Value)) {
        if (!derivesFromCandidate(Value))
          Valid = false;
        else
          SawDerivedReturn = true;
      }
      return true;
    }

    bool VisitUnaryOperator(UnaryOperator *Operation) {
      if (Operation->getOpcode() == UO_AddrOf &&
          isCandidate(Operation->getSubExpr()))
        Valid = false;
      return true;
    }

    bool VisitBinaryOperator(BinaryOperator *Operation) {
      if (!Operation->isAssignmentOp() ||
          !isCandidate(Operation->getLHS()))
        return true;
      if (Operation->getOpcode() == BO_Assign) {
        if (!derivesFromCandidate(Operation->getRHS()))
          Valid = false;
      } else if (Operation->getOpcode() != BO_AddAssign &&
                 Operation->getOpcode() != BO_SubAssign) {
        Valid = false;
      }
      return true;
    }

    bool VisitGCCAsmStmt(GCCAsmStmt *) {
      Valid = false;
      return true;
    }

    bool VisitMSAsmStmt(MSAsmStmt *) {
      Valid = false;
      return true;
    }

    bool valid() const { return Valid && SawDerivedReturn; }
  };

  static std::optional<unsigned>
  returnedPointerParameter(const FunctionDecl *FD, ASTContext &Ctx) {
    if (!FD || FD->getStorageClass() != SC_Static ||
        !FD->getReturnType()->isPointerType())
      return std::nullopt;
    for (const FunctionDecl *Redeclaration : FD->redecls())
      if (Redeclaration->hasAttr<AliasAttr>() ||
          Redeclaration->hasAttr<WeakRefAttr>() ||
          Redeclaration->hasAttr<IFuncAttr>() ||
          Redeclaration->hasAttr<UsedAttr>() ||
          Redeclaration->hasAttr<RetainAttr>())
        return std::nullopt;
    const FunctionDecl *Definition = FD->getDefinition();
    const auto *Body =
        Definition ? dyn_cast_or_null<CompoundStmt>(Definition->getBody())
                   : nullptr;
    if (!Body || Body->body_empty() ||
        !isa<ReturnStmt>(Body->body_back()))
      return std::nullopt;
    std::optional<unsigned> Result;
    for (unsigned Index = 0; Index < Definition->getNumParams(); ++Index) {
      const ParmVarDecl *Parameter = Definition->getParamDecl(Index);
      if (!Parameter->getType()->isPointerType())
        continue;
      ReturnedParameterVisitor Visitor(Parameter, Ctx);
      Visitor.TraverseStmt(const_cast<Stmt *>(Definition->getBody()));
      if (!Visitor.valid())
        continue;
      if (Result)
        return std::nullopt;
      Result = Index;
    }
    return Result;
  }

  // isConstantSentinel: the source of an integer-to-pointer cast is a
  // compile-time constant (NT's `(HANDLE)(LONG_PTR)-1` pseudo-handle
  // convention, SIG_DFL/SIG_IGN/SIG_ERR- and MAP_FAILED-style invalid-
  // handle sentinels). "Provenance" isn't a coherent question for a
  // literal: it was never derived from any pointer and is fully visible
  // in the diff, unlike an integer from a variable, syscall, or untrusted
  // input. A strengthening of the checker's purpose, not a relaxation:
  // a fixed sentinel was never *derived* from anything.
  static bool isConstantSentinel(const Expr *E, ASTContext &Ctx) {
    if (E->isValueDependent() || E->isTypeDependent())
      return false;
    Expr::EvalResult Result;
    return E->EvaluateAsInt(Result, Ctx, Expr::SE_NoSideEffects);
  }

  // derivesFromPointer: true if E is built, through parens, arithmetic
  // (+, -, &, |, ^, unary ~) or a conditional operator, from a nested
  // pointer-to-integral cast. Recognises the pointer -> integer ->
  // (mask/offset) -> pointer round trip used for alignment throughout
  // this tree (posix_memalign(), align16(), stack-probe alignment,
  // mman.c's page-range intersection); the ternary case handles
  // `lo = a > b ? a : b`-style range clamps built from two such round
  // trips. The integer was never anything but a pointer's bit pattern
  // plus a compile-time-visible adjustment, so the cast back is
  // provenance-preserving by construction.
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
           Name == "signal_apc" || Name == "__plat_thread_queue_apc";
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
    const MemRegion *LeftRegistry =
        elementRegistry(State->getSVal(Operation->getLHS(), LC), State);
    const MemRegion *RightRegistry =
        elementRegistry(State->getSVal(Operation->getRHS(), LC), State);
    if (const VarDecl *Registry = directRegistryExpression(
            Operation->getLHS(), C.getASTContext()))
      if (registryEligible(Registry, C.getASTContext()))
        LeftRegistry = registryStorage(Registry, C);
    if (const VarDecl *Registry = directRegistryExpression(
            Operation->getRHS(), C.getASTContext()))
      if (registryEligible(Registry, C.getASTContext()))
        RightRegistry = registryStorage(Registry, C);
    if (LeftRegistry && LeftRegistry == RightRegistry)
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
    report(
        "integer-to-pointer conversion is not proven provenance-preserving",
        Cast, C);
  }

  void checkPreStmt(const ReturnStmt *Return, CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(
        C.getLocationContext()->getDecl());
    std::optional<RelationContract> Contract = relationContract(
        FD, ntlibc::ElementRelationKind::Return);
    const Expr *Value = Return->getRetValue();
    if (!Contract || !Value ||
        Value->isNullPointerConstant(C.getASTContext(),
                                     Expr::NPC_ValueDependentIsNotNull) ||
        !relationEligible(FD, C.getASTContext()))
      return;
    const VarDecl *Registry =
        registryDecl(Contract->Registry, C.getASTContext());
    if (!registryEligible(Registry, C.getASTContext()))
      return;
    ProgramStateRef State = C.getState();
    const MemRegion *Returned = baseRegion(C.getSVal(Value), State);
    const MemRegion *RegistryBase = registryBase(Registry, C);
    const MemRegion *Relation = elementRegistry(C.getSVal(Value), State);
    const MemRegion *Storage = registryStorage(Registry, C);
    if ((!Returned || !RegistryBase || Returned != RegistryBase) &&
        (!Relation || Relation != Storage))
      report("element relation contract is not proven", Return, C);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    const auto *OriginCall = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());
    if (!FD || !OriginCall || OriginCall->getDirectCallee() != FD ||
        !relationEligible(FD, C.getASTContext()))
      return;
    for (unsigned Index = 0; Index < Call.getNumArgs(); ++Index) {
      std::optional<RelationContract> Contract = relationContract(
          FD, ntlibc::ElementRelationKind::Parameter, Index);
      if (!Contract)
        continue;
      const VarDecl *Registry =
          registryDecl(Contract->Registry, C.getASTContext());
      if (!registryEligible(Registry, C.getASTContext()))
        continue;
      ProgramStateRef State = C.getState();
      const MemRegion *Argument = baseRegion(Call.getArgSVal(Index), State);
      const MemRegion *RegistryBase = registryBase(Registry, C);
      const MemRegion *Relation =
          elementRegistry(Call.getArgSVal(Index), State);
      const MemRegion *Storage = registryStorage(Registry, C);
      if ((!Argument || !RegistryBase || Argument != RegistryBase) &&
          (!Relation || Relation != Storage)) {
        report("element relation contract is not proven", OriginCall, C);
        return;
      }
    }
  }

  void checkBeginFunction(CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(
        C.getLocationContext()->getDecl());
    if (!FD || !relationEligible(FD, C.getASTContext()))
      return;
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (unsigned Index = 0; Index < FD->getNumParams(); ++Index) {
      std::optional<RelationContract> Contract = relationContract(
          FD, ntlibc::ElementRelationKind::Parameter, Index);
      if (!Contract)
        continue;
      const VarDecl *Registry =
          registryDecl(Contract->Registry, C.getASTContext());
      if (!registryEligible(Registry, C.getASTContext()))
        continue;
      const MemRegion *Storage = registryStorage(Registry, C);
      Loc ParameterLocation =
          State->getLValue(FD->getParamDecl(Index), C.getLocationContext());
      const MemRegion *ParameterValue =
          State->getSVal(ParameterLocation).getAsRegion();
      const auto *ParameterSymbol =
          ParameterValue
              ? dyn_cast<SymbolicRegion>(ParameterValue->getBaseRegion())
              : nullptr;
      if (!Storage || !ParameterSymbol)
        continue;
      State = State->set<RegistryElement>(ParameterSymbol->getSymbol(),
                                          Storage);
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkBind(SVal Location, SVal Value, const Stmt *,
                 CheckerContext &C) const {
    const MemRegion *Written = Location.getAsRegion();
    if (!Written)
      return;
    Written = Written->getBaseRegion();
    ProgramStateRef Original = C.getState();
    ProgramStateRef State = Original;
    for (const auto &Entry : Original->get<RegistryElement>())
      if (Entry.second == Written)
        State = State->remove<RegistryElement>(Entry.first);
    const MemRegion *ValueRegion = Value.getAsRegion();
    const auto *ValueSymbol =
        ValueRegion
            ? dyn_cast<SymbolicRegion>(ValueRegion->getBaseRegion())
            : nullptr;
    if (ValueSymbol) {
      for (const VarDecl *Registry : contractRegistries(C.getASTContext())) {
        if (!registryEligible(Registry, C.getASTContext()))
          continue;
        const MemRegion *CurrentBase = registryBase(Registry, C);
        if (CurrentBase && CurrentBase == ValueRegion->getBaseRegion()) {
          const MemRegion *Storage = registryStorage(Registry, C);
          if (Storage)
            State = State->set<RegistryElement>(ValueSymbol->getSymbol(),
                                                Storage);
        }
      }
    }
    if (State != Original)
      C.addTransition(State);
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!FD || !FD->getIdentifier())
      return;
    if (const auto *OriginCall =
            dyn_cast_or_null<CallExpr>(Call.getOriginExpr())) {
      std::optional<RelationContract> Contract = relationContract(
          FD, ntlibc::ElementRelationKind::Return);
      if (Contract && OriginCall->getDirectCallee() == FD &&
          relationEligible(FD, C.getASTContext())) {
        ProgramStateRef State = C.getState();
        const VarDecl *Registry =
            registryDecl(Contract->Registry, C.getASTContext());
        if (!registryEligible(Registry, C.getASTContext()))
          return;
        const MemRegion *Storage = registryStorage(Registry, C);
        const MemRegion *Return = Call.getReturnValue().getAsRegion();
        const auto *ReturnSymbol =
            Return ? dyn_cast<SymbolicRegion>(Return->getBaseRegion())
                   : nullptr;
        if (Storage && ReturnSymbol) {
          State = State->set<RegistryElement>(ReturnSymbol->getSymbol(),
                                              Storage);
          C.addTransition(State);
          return;
        }
      }
    }
    if (isStringConversionFunction(FD) && Call.getNumArgs() > 1) {
      ProgramStateRef State = C.getState();
      const MemRegion *Haystack = Call.getArgSVal(0).getAsRegion();
      const MemRegion *EndStorage = Call.getArgSVal(1).getAsRegion();
      if (!Haystack || !EndStorage)
        return;
      Haystack = resolveAlias(Haystack->getBaseRegion(), State);
      const MemRegion *EndValue = State->getSVal(EndStorage).getAsRegion();
      const auto *EndSymbol =
          EndValue ? dyn_cast<SymbolicRegion>(EndValue->getBaseRegion())
                   : nullptr;
      if (!Haystack || !EndSymbol)
        return;
      State = State->set<NeedleAlias>(EndSymbol->getSymbol(), Haystack);
      C.addTransition(State);
      return;
    }
    const auto *OriginCall = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());
    if (OriginCall && OriginCall->getDirectCallee() == FD) {
      std::optional<unsigned> Parameter =
          returnedPointerParameter(FD, C.getASTContext());
      if (Parameter && *Parameter < Call.getNumArgs()) {
        ProgramStateRef State = C.getState();
        const MemRegion *Origin = Call.getArgSVal(*Parameter).getAsRegion();
        const MemRegion *Return = Call.getReturnValue().getAsRegion();
        const auto *ReturnSymbol =
            Return ? dyn_cast<SymbolicRegion>(Return->getBaseRegion())
                   : nullptr;
        if (Origin && ReturnSymbol) {
          Origin = resolveAlias(Origin->getBaseRegion(), State);
          State = State->set<NeedleAlias>(ReturnSymbol->getSymbol(), Origin);
          C.addTransition(State);
          return;
        }
      }
    }
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
