// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
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
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

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
REGISTER_MAP_WITH_PROGRAMSTATE(ConstructFamilyMap, const MemRegion *,
                               const IdentifierInfo *)

enum class CapabilityKind : unsigned char { Linear, Duplicable };
using CapabilityKey = std::pair<const MemRegion *, const IdentifierInfo *>;
REGISTER_MAP_WITH_PROGRAMSTATE(CapabilityMap, CapabilityKey, CapabilityKind)
using SymbolCapabilityKey = std::pair<SymbolRef, const IdentifierInfo *>;
REGISTER_MAP_WITH_PROGRAMSTATE(SymbolCapabilityMap, SymbolCapabilityKey,
                               CapabilityKind)
enum class CarrierCapabilityKind : unsigned char { Absent, Linear, Duplicable };
using CarrierCapabilityKey =
    std::pair<const MemRegion *, const IdentifierInfo *>;
REGISTER_MAP_WITH_PROGRAMSTATE(CarrierCapabilityMap, CarrierCapabilityKey,
                               CarrierCapabilityKind)
using StrictLoanKey = std::pair<const MemRegion *, const IdentifierInfo *>;
REGISTER_MAP_WITH_PROGRAMSTATE(StrictLoanMap, StrictLoanKey, const MemRegion *)
REGISTER_SET_WITH_PROGRAMSTATE(ExpiredStrictLoanSet, const MemRegion *)

REGISTER_MAP_WITH_PROGRAMSTATE(ResourceMap, SymbolRef, unsigned)

namespace {

struct CapabilityPresence {
  bool Known;
  std::optional<CapabilityKind> Kind;
};

static CarrierCapabilityKey carrierKey(const MemRegion *Carrier,
                                       const IdentifierInfo *Family) {
  return {Carrier, Family};
}

static const MemRegion *carrierRegion(const Expr *Expression,
                                      CheckerContext &C) {
  if (!Expression)
    return nullptr;
  const Expr *Core = Expression->IgnoreParenImpCasts();
  if (const auto *Reference = dyn_cast<DeclRefExpr>(Core))
    if (const auto *Variable = dyn_cast<VarDecl>(Reference->getDecl()))
      return C.getState()
          ->getLValue(Variable, C.getLocationContext())
          .getAsRegion();
  if (const auto *Member = dyn_cast<MemberExpr>(Core)) {
    const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
    if (!Field)
      return C.getSVal(Core).getAsRegion();
    SVal Base = C.getSVal(Member->getBase());
    if (!Member->isArrow()) {
      const MemRegion *BaseRegion = carrierRegion(Member->getBase(), C);
      if (!BaseRegion)
        return C.getSVal(Core).getAsRegion();
      Base = loc::MemRegionVal(BaseRegion);
    }
    return C.getState()->getLValue(Field, Base).getAsRegion();
  }
  if (const auto *Unary = dyn_cast<UnaryOperator>(Core))
    if (Unary->getOpcode() == UO_AddrOf)
      return C.getSVal(Expression).getAsRegion();
  return nullptr;
}

static const CapabilityKind *underlyingTokenFor(ProgramStateRef State,
                                                SVal Value,
                                                const IdentifierInfo *Family) {
  if (SymbolRef Symbol = Value.getAsSymbol(true))
    if (const CapabilityKind *Kind =
            State->get<SymbolCapabilityMap>({Symbol, Family}))
      return Kind;
  if (const MemRegion *Region = Value.getAsRegion())
    return State->get<CapabilityMap>({Region, Family});
  return nullptr;
}

static ProgramStateRef removeUnderlyingToken(ProgramStateRef State, SVal Value,
                                             const IdentifierInfo *Family) {
  if (SymbolRef Symbol = Value.getAsSymbol(true))
    State = State->remove<SymbolCapabilityMap>({Symbol, Family});
  if (const MemRegion *Region = Value.getAsRegion())
    State = State->remove<CapabilityMap>({Region, Family});
  return State;
}

static ProgramStateRef setUnderlyingToken(ProgramStateRef State, SVal Value,
                                          const IdentifierInfo *Family,
                                          CapabilityKind Kind) {
  if (SymbolRef Symbol = Value.getAsSymbol(true))
    State = State->set<SymbolCapabilityMap>({Symbol, Family}, Kind);
  if (const MemRegion *Region = Value.getAsRegion())
    State = State->set<CapabilityMap>({Region, Family}, Kind);
  return State;
}

static CapabilityPresence capabilityFor(ProgramStateRef State,
                                        const MemRegion *Carrier, SVal Value,
                                        const IdentifierInfo *Family) {
  if (Carrier)
    if (const CarrierCapabilityKind *CarrierKind =
            State->get<CarrierCapabilityMap>(carrierKey(Carrier, Family))) {
      if (*CarrierKind == CarrierCapabilityKind::Absent)
        return {true, std::nullopt};
      return {true, *CarrierKind == CarrierCapabilityKind::Linear
                        ? CapabilityKind::Linear
                        : CapabilityKind::Duplicable};
    }
  if (const CapabilityKind *Underlying =
          underlyingTokenFor(State, Value, Family))
    return {false, *Underlying};
  return {false, std::nullopt};
}

static const TypedefNameDecl *dialectToken(ASTContext &Context,
                                           StringRef Name) {
  IdentifierInfo &Identifier = Context.Idents.get(Name);
  DeclarationName Declaration(&Identifier);
  for (NamedDecl *Candidate :
       Context.getTranslationUnitDecl()->lookup(Declaration))
    if (const auto *Token = dyn_cast<TypedefNameDecl>(Candidate))
      return Token;
  return nullptr;
}

static bool hasDialectQualifier(const TypedefNameDecl *Token,
                                StringRef Qualifier) {
  if (!Token)
    return false;
  for (const AnnotateAttr *Attr : Token->specific_attrs<AnnotateAttr>())
    if (Attr->getAnnotation() == Qualifier)
      return true;
  return false;
}

static std::optional<CapabilityKind> dialectTokenKind(ASTContext &Context,
                                                       StringRef Name) {
  const TypedefNameDecl *Token = dialectToken(Context, Name);
  if (!Token)
    return std::nullopt;
  return hasDialectQualifier(Token, "qual:l_unlimited")
             ? CapabilityKind::Duplicable
             : CapabilityKind::Linear;
}

static bool dialectTokenPermitsCarrierCopy(const TypedefNameDecl *Token) {
  return Token && hasDialectQualifier(Token, "qual:l_permissive") &&
         !hasDialectQualifier(Token, "qual:l_strict") &&
         !hasDialectQualifier(Token, "qual:l_unlimited");
}

static bool initializedByStringLiteral(const ValueDecl *Declaration) {
  const auto *Variable = dyn_cast_or_null<VarDecl>(Declaration);
  if (!Variable || !Variable->hasInit())
    return false;
  const Expr *Initializer = Variable->getInit()->IgnoreParenImpCasts();
  if (isa<StringLiteral>(Initializer))
    return true;
  if (const auto *List = dyn_cast<InitListExpr>(Initializer))
    return List->getNumInits() == 1 &&
           isa<StringLiteral>(List->getInit(0)->IgnoreParenImpCasts());
  return false;
}

static bool expressionProvidesStringLiteralToken(
    const Expr *Expression, const IdentifierInfo *Family, ASTContext &Context) {
  if (!Expression || !Family)
    return false;
  const TypedefNameDecl *Token =
      dialectToken(Context, Family->getName());
  if (!hasDialectQualifier(Token, "qual:string_literal"))
    return false;
  const Expr *Core = Expression->IgnoreParenImpCasts();
  if (isa<StringLiteral>(Core))
    return true;
  if (const auto *Reference = dyn_cast<DeclRefExpr>(Core))
    return initializedByStringLiteral(Reference->getDecl());
  return false;
}

static std::optional<int64_t> dialectExcludedSentinel(
    const TypedefNameDecl *Token) {
  if (!Token)
    return std::nullopt;
  constexpr StringRef Prefix = "qual:sentinel_exclude=";
  for (const AnnotateAttr *Attr : Token->specific_attrs<AnnotateAttr>()) {
    StringRef Text = Attr->getAnnotation();
    if (!Text.consume_front(Prefix))
      continue;
    if (Text == "NULL")
      return 0;
    int64_t Value = 0;
    if (!Text.getAsInteger(10, Value))
      return Value;
  }
  return std::nullopt;
}

static bool dialectTokenExcludes(const IdentifierInfo *Family,
                                 const Expr *Expression,
                                 ASTContext &Context) {
  if (!Family || !Expression)
    return false;
  std::optional<int64_t> Sentinel = dialectExcludedSentinel(
      dialectToken(Context, Family->getName()));
  if (!Sentinel)
    return false;
  std::optional<llvm::APSInt> Value =
      Expression->IgnoreParenImpCasts()->getIntegerConstantExpr(Context);
  return Value && Value->isSignedIntN(64) &&
         Value->getSExtValue() == *Sentinel;
}

static ProgramStateRef setCarrierToken(ProgramStateRef State,
                                       const MemRegion *Carrier,
                                       const IdentifierInfo *Family,
                                       CapabilityKind Kind) {
  if (!Carrier)
    return State;
  return State->set<CarrierCapabilityMap>(
      carrierKey(Carrier, Family), Kind == CapabilityKind::Linear
                                       ? CarrierCapabilityKind::Linear
                                       : CarrierCapabilityKind::Duplicable);
}

static ProgramStateRef removeCarrierToken(ProgramStateRef State,
                                          const MemRegion *Carrier,
                                          const IdentifierInfo *Family) {
  if (!Carrier)
    return State;
  return State->set<CarrierCapabilityMap>(carrierKey(Carrier, Family),
                                          CarrierCapabilityKind::Absent);
}

static ProgramStateRef setOperationToken(ProgramStateRef State,
                                         const MemRegion *Carrier, SVal Value,
                                         const IdentifierInfo *Family,
                                         CapabilityKind Kind) {
  State = setCarrierToken(State, Carrier, Family, Kind);
  if (const MemRegion *Referent = Value.getAsRegion())
    State = setCarrierToken(State, Referent, Family, Kind);
  return setUnderlyingToken(State, Value, Family, Kind);
}

static ProgramStateRef removeOperationToken(ProgramStateRef State,
                                            const MemRegion *Carrier,
                                            SVal Value,
                                            const IdentifierInfo *Family) {
  State = removeCarrierToken(State, Carrier, Family);
  if (const MemRegion *Referent = Value.getAsRegion())
    State = removeCarrierToken(State, Referent, Family);
  return removeUnderlyingToken(State, Value, Family);
}

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

static bool insideDynamicStorageConsumer(CheckerContext &C) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
  if (!Function)
    return false;
  for (const ParmVarDecl *Parameter : Function->parameters())
    for (const AnnotateAttr *Attribute :
         Parameter->specific_attrs<AnnotateAttr>()) {
      StringRef Text = Attribute->getAnnotation();
      bool Consumer = Text.consume_front("consume:");
      if (!Consumer) {
        Text = Attribute->getAnnotation();
        Consumer = Text.consume_front("consume_if_nonnull_return:");
      }
      if (Consumer && !Text.empty() &&
          !Text.contains(':') &&
          hasDialectQualifier(dialectToken(Function->getASTContext(), Text),
                              "qual:dynamic_storage"))
        return true;
    }
  return false;
}

class OwnershipChecker
    : public Checker<check::PreCall, check::PostCall, check::Location> {
  mutable std::unique_ptr<BugType> BT;

  static const FunctionDecl *functionOf(const CallEvent &Call) {
    return dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  }

  static bool returnsOwnership(const CallEvent &Call) {
    const FunctionDecl *Function = functionOf(Call);
    if (!Function)
      return false;
    for (const AnnotateAttr *Attribute :
         Function->specific_attrs<AnnotateAttr>()) {
      StringRef Text = Attribute->getAnnotation();
      if (Text.consume_front("withtok:") && !Text.empty() &&
          !Text.contains(':')) {
        const TypedefNameDecl *Token =
            dialectToken(Function->getASTContext(), Text);
        if (hasDialectQualifier(Token, "qual:dynamic_storage"))
          return true;
      }
    }
    return false;
  }

  static std::optional<unsigned>
  reallocatedArgument(const FunctionDecl *Function) {
    if (!Function)
      return std::nullopt;
    unsigned Argument = 0;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      for (const AnnotateAttr *Attribute :
           Parameter->specific_attrs<AnnotateAttr>()) {
        StringRef Text = Attribute->getAnnotation();
        if (Text.consume_front("consume_if_nonnull_return:") &&
            !Text.empty() && !Text.contains(':') &&
            hasDialectQualifier(
                dialectToken(Function->getASTContext(), Text),
                "qual:dynamic_storage"))
          return Argument;
      }
      ++Argument;
    }
    return std::nullopt;
  }

  static std::optional<unsigned>
  returnedArgument(const FunctionDecl *Function) {
    if (!Function)
      return std::nullopt;
    StringRef ReturnedFamily;
    for (const AnnotateAttr *Attribute :
         Function->specific_attrs<AnnotateAttr>()) {
      StringRef Text = Attribute->getAnnotation();
      if (Text.consume_front("withtok:") && !Text.empty() &&
          !Text.contains(':')) {
        ReturnedFamily = Text;
        break;
      }
    }
    if (ReturnedFamily.empty())
      return std::nullopt;
    unsigned Argument = 0;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      for (const AnnotateAttr *Attribute :
           Parameter->specific_attrs<AnnotateAttr>()) {
        StringRef Text = Attribute->getAnnotation();
        if (Text.consume_front("withtok:") && Text == ReturnedFamily)
          return Argument;
      }
      ++Argument;
    }
    return std::nullopt;
  }

  static std::optional<unsigned>
  ownershipTakenArgument(const FunctionDecl *Function) {
    if (!Function)
      return std::nullopt;
    unsigned Argument = 0;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      for (const AnnotateAttr *Attribute :
           Parameter->specific_attrs<AnnotateAttr>()) {
        StringRef Text = Attribute->getAnnotation();
        if (Text.consume_front("consume:") && !Text.empty() &&
            !Text.contains(':') &&
            hasDialectQualifier(
                dialectToken(Function->getASTContext(), Text),
                "qual:dynamic_storage"))
          return Argument;
      }
      ++Argument;
    }
    return std::nullopt;
  }

  static bool hasName(const CallEvent &Call, StringRef Wanted) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    return Function && Function->getIdentifier() &&
           Function->getName() == Wanted;
  }

  static bool isAllocator(const CallEvent &Call) {
    return returnsOwnership(Call);
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

  // Nothing in clang's own builtin summaries relates a NUL-terminated
  // string SCAN's return value to the dynamic extent of the pointer it
  // scanned -- the same gap allocationSizeInBytes above closes for this
  // tree's own __malloc family, just on the other side of the same
  // relationship: strlen(s)/wcslen(s) cannot return a value L without
  // having read s[0..L] (the L scanned bytes plus the NUL itself) to
  // find it, so the region s points to really does have at least L+1
  // bytes/wchar_t's, exactly as real and exactly as directly available
  // as an allocator's own size argument is. strcspn(s,reject)/
  // wcscspn(s,reject) and strspn(s,accept)/wcsspn(s,accept) share the
  // identical shape: each one stops the instant s[L] is a byte the call
  // itself had to inspect to decide whether to keep going (a member
  // of/absent from the reject/accept set, OR the string's own NUL), so
  // s[L] was read either way and the same "L scanned plus one more"
  // bound holds no matter which of the two stopping conditions actually
  // ended the scan.
  //
  // src/string/strsep.c's real `end = s + strcspn(s, sep); if (*end)
  // *end++ = 0;`, src/string/strtok.c's and strtok_r.c's real `s +=
  // strspn(s, sep); if (!*s) ...`, and src/misc/catgets.c's expand()
  // real `v = lang + strcspn(lang, "_"); if (*v) v++;` are the concrete
  // code this was developed against: all three dereference exactly the
  // scan's own return-value offset into the exact same pointer that was
  // just scanned, with no allocator anywhere in sight -- before this
  // fix, every one of them had nothing but an unconstrained placeholder
  // extent for a plain borrowed parameter no allocator ever touched,
  // and was reported, even though the scan that had *just* run is
  // exactly what proves it safe.
  static bool isScanExtentFunction(const CallEvent &Call) {
    static constexpr llvm::StringLiteral Names[] = {
        "strlen", "strcspn", "strspn", "wcslen", "wcscspn", "wcsspn"};
    for (StringRef Name : Names)
      if (hasName(Call, Name))
        return true;
    return false;
  }

  // Sets the scanned argument's own dynamic extent from the scan's
  // return value, per isScanExtentFunction's comment above.
  //
  // Deliberately narrow in the same way allocationSizeInBytes is
  // deliberately narrow: only fires when the scanned argument's own
  // region IS the base region already -- no pointer arithmetic already
  // applied to it (not itself an ElementRegion at a nonzero offset). A
  // scan of an already-advanced cursor (strtok_r.c's SECOND strcspn()
  // call, scanning `s` after `s += strspn(...)` already moved it) would
  // need the offset already walked PLUS this scan's own return value
  // composed together, which this narrow form does not attempt; that
  // case is left exactly where it already was, an unprovable (but
  // still correct) residual, rather than risk getting the composition
  // wrong.
  //
  // Only ever WRITES an extent that is not already real -- the same
  // "placeholder or nothing" test getDynamicExtentWithOffset's own
  // NoRealExtentInfo branch already applies at the actual dereference
  // site. A base region a previous, unrelated scan or allocation
  // already gave a real extent to is left alone even if THIS scan's
  // own bound happens to be larger: there is no general way to compare
  // two symbolic bounds against each other, so whichever fact was
  // established FIRST along a given path is simply kept. That costs
  // completeness, never soundness -- every one of these scan contracts
  // is a true LOWER bound on the real allocation, never an upper one,
  // so keeping an earlier, possibly smaller, established bound is
  // always still a true fact about the region; it just is not always
  // the tightest one available.
  // The element width used to convert the scan's return value (always
  // in ELEMENTS: a wcslen() count is a count of wchar_t's, not bytes)
  // into the bytes getDynamicExtent()/setDynamicExtent() actually deal
  // in is read off the scanned argument's own real, AST-declared
  // pointee type -- NOT clang's built-in notion of "the target's
  // wchar_t" (ASTContext::getWCharType(), what a wide string LITERAL's
  // own type is built from). Those two are not the same type in this
  // tree and do not even always have the same SIZE: this codebase's own
  // `wchar_t` (arch/*/bits/alltypes.h.in's `TYPEDEF unsigned short
  // wchar_t`) is deliberately kept 2 bytes on EVERY arch it builds for
  // (this tree's own UTF-16 convention, not the platform's native
  // wide-character width) -- confirmed empirically to differ from
  // clang's own builtin wchar_t on a target with no explicit --target
  // triple (aarch64 here, analyzed with the host's native clang and so
  // getWCharType()'s builtin default -- `int`, 4 bytes, glibc's usual
  // UCS-4 convention), while the i386/x86_64 legs' `--target=*-w64-
  // mingw32` happens to make clang's OWN builtin wchar_t 2 bytes too, by
  // coincidence of that target's own ABI matching this tree's typedef.
  // Using getWCharType() worked on i386/x86_64 by that coincidence and
  // silently produced the WRONG byte multiplier on aarch64 (4 instead of
  // 2) -- confirmed by a real regression during development:
  // src/string/wcstok.c's `s += wcsspn(s, sep); if (!*s) ...` (this
  // fix's own wide-scanner target, the wcsspn() twin of strtok_r.c's
  // narrow one) proved fine on i386/x86_64 and stayed reported on
  // aarch64 until this fix switched to the argument's own pointee type.
  // Reading the actual pointee type off the argument is also strictly
  // more general: it needs no separate "is this call one of the wide
  // names" check at all, and does the right thing even if some entirely
  // different fixed-width scanner were ever added to
  // isScanExtentFunction above.
  static void trackScanExtent(const CallEvent &Call, CheckerContext &C) {
    if (Call.getNumArgs() < 1)
      return;
    const MemRegion *Region = Call.getArgSVal(0).getAsRegion();
    if (!Region || Region != Region->getBaseRegion())
      return;
    const Expr *ArgExpr = Call.getArgExpr(0);
    if (!ArgExpr)
      return;
    QualType PointerTy = ArgExpr->IgnoreParenCasts()->getType();
    if (!PointerTy->isPointerType())
      return;
    QualType ElemTy = PointerTy->getPointeeType();
    if (ElemTy.isNull() || ElemTy->isIncompleteType())
      return;
    CharUnits ElemWidth = C.getASTContext().getTypeSizeInChars(ElemTy);
    if (ElemWidth.isZero())
      return;
    ProgramStateRef State = C.getState();
    SValBuilder &Builder = C.getSValBuilder();
    SVal CurrentExtent = getDynamicExtent(State, Region, Builder);
    bool NoRealExtentInfo =
        CurrentExtent.isUnknownOrUndef() ||
        isa_and_nonnull<SymbolExtent>(CurrentExtent.getAsSymbol());
    if (!NoRealExtentInfo)
      return;
    std::optional<DefinedOrUnknownSVal> Scanned =
        Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
    if (!Scanned)
      return;
    QualType SizeTy = C.getASTContext().getSizeType();
    SVal Elements = Builder.evalBinOp(State, BO_Add, *Scanned,
                                      Builder.makeIntVal(1, SizeTy), SizeTy);
    SVal Bytes =
        ElemWidth.isOne()
            ? Elements
            : Builder.evalBinOp(
                  State, BO_Mul, Elements,
                  Builder.makeIntVal(ElemWidth.getQuantity(), SizeTy), SizeTy);
    std::optional<DefinedOrUnknownSVal> DefinedBytes =
        Bytes.getAs<DefinedOrUnknownSVal>();
    if (!DefinedBytes)
      return;
    C.addTransition(setDynamicExtent(State, Region, *DefinedBytes, Builder));
  }

  static bool insideOwnershipConsumer(CheckerContext &C) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    return ownershipTakenArgument(Function).has_value() ||
           reallocatedArgument(Function).has_value();
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
    if (isScanExtentFunction(Call)) {
      trackScanExtent(Call, C);
      return;
    }
    if (!isAllocator(Call))
      return;
    ProgramStateRef State = C.getState();
    const FunctionDecl *Function = functionOf(Call);
    if (std::optional<unsigned> Argument = returnedArgument(Function)) {
      if (*Argument >= Call.getNumArgs())
        return;
      std::optional<DefinedOrUnknownSVal> ArgumentValue =
          Call.getArgSVal(*Argument).getAs<DefinedOrUnknownSVal>();
      if (!ArgumentValue)
        return;
      auto [ArgumentNonNullState, ArgumentNullState] =
          State->assume(*ArgumentValue);
      if (ArgumentNonNullState)
        C.addTransition(ArgumentNonNullState);
      if (!ArgumentNullState)
        return;
      State = ArgumentNullState;
    }
    SVal ReturnValue = Call.getReturnValue();
    if (std::optional<unsigned> Argument = reallocatedArgument(Function);
        Argument && *Argument < Call.getNumArgs() &&
        State->isNull(ReturnValue).isConstrainedFalse()) {
      SymbolRef Old = allocationSymbol(Call.getArgSVal(*Argument));
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
          State = setDynamicExtent(State, Region->getBaseRegion(), *DefinedSize,
                                   C.getSValBuilder());
      }
    }
    C.addTransition(State);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const FunctionDecl *Function = functionOf(Call);
    std::optional<unsigned> Taken = ownershipTakenArgument(Function);
    std::optional<unsigned> Reallocated = reallocatedArgument(Function);
    bool Deallocator = Taken.has_value();
    bool Reallocator = Reallocated.has_value();
    if (!Deallocator && !Reallocator)
      return;
    unsigned ArgumentIndex = Deallocator ? *Taken : *Reallocated;
    if (ArgumentIndex >= Call.getNumArgs())
      return;
    if (insideOwnershipConsumer(C))
      return;
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;

    SVal Argument = Call.getArgSVal(ArgumentIndex);
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
    if (Kind && *Kind == OwnershipKind::Consumed &&
        !insideDynamicStorageConsumer(C))
      report("borrow accesses a consumed owner", Statement, C.getState(), C);
  }
};

enum class ConstructOperation : unsigned char { Construct, Destroy, Use };

struct ConstructCall {
  ConstructOperation Operation;
  const IdentifierInfo *Family;
  unsigned Argument;
  bool StaticInitialization;
};

class OwnedConstructChecker : public Checker<check::PreCall, check::PostCall> {
  mutable std::unique_ptr<BugType> BT;

  static const IdentifierInfo *parameterAnnotation(const FunctionDecl *Function,
                                                   const AnnotateAttr *Attr,
                                                   StringRef Prefix) {
    StringRef Text = Attr->getAnnotation();
    if (!Text.consume_front(Prefix) || Text.empty() || Text.contains(':'))
      return nullptr;
    return &Function->getASTContext().Idents.get(Text);
  }

  static bool hasParameterAnnotation(const FunctionDecl *Function,
                                     const ParmVarDecl *Parameter,
                                     StringRef Prefix,
                                     const IdentifierInfo *Family) {
    for (const AnnotateAttr *Attr : Parameter->specific_attrs<AnnotateAttr>())
      if (parameterAnnotation(Function, Attr, Prefix) == Family)
        return true;
    return false;
  }

  static llvm::SmallVector<ConstructCall, 4>
  protocolsFor(const CallEvent &Call) {
    llvm::SmallVector<ConstructCall, 4> Protocols;
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function)
      return Protocols;
    struct OperationAnnotation {
      llvm::StringLiteral Prefix;
      ConstructOperation Operation;
    };
    static constexpr OperationAnnotation Operations[] = {
        {"construct:", ConstructOperation::Construct},
        {"destroy:", ConstructOperation::Destroy},
        {"handle:", ConstructOperation::Use}};
    unsigned Argument = 0;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      for (const AnnotateAttr *Attr : Parameter->specific_attrs<AnnotateAttr>())
        for (const OperationAnnotation &Candidate : Operations)
          if (const IdentifierInfo *Family =
                  parameterAnnotation(Function, Attr, Candidate.Prefix))
            Protocols.push_back(
                {Candidate.Operation, Family, Argument,
                 hasParameterAnnotation(Function, Parameter,
                                        "static_handle:", Family)});
      ++Argument;
    }
    return Protocols;
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

  static bool hasStaticInitialization(const MemRegion *Region, bool Accepted) {
    if (!Accepted)
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

  void requireLive(const CallEvent &Call, const ConstructCall &Protocol,
                   CheckerContext &C) const {
    unsigned Argument = Protocol.Argument;
    const MemRegion *Region = argumentRegion(Call, Argument);
    if (!Region ||
        C.getState()->isNull(Call.getArgSVal(Argument)).isConstrainedTrue())
      return;
    const ConstructKind *Kind = C.getState()->get<ConstructMap>(Region);
    if (!Kind &&
        (hasStaticInitialization(Region, Protocol.StaticInitialization) ||
         isOpaqueBorrow(Region)))
      return;
    const Stmt *Statement = Call.getOriginExpr();
    if (!Statement)
      return;
    if (!Kind)
      report("owned construct is not proven initialized", Statement,
             C.getState(), C);
    else if (*Kind == ConstructKind::Destroyed)
      report(Protocol.Operation == ConstructOperation::Destroy
                 ? "owned construct is already destroyed"
                 : "operation accesses a destroyed owned construct",
             Statement, C.getState(), C);
    else if (const IdentifierInfo *const *Actual =
                 C.getState()->get<ConstructFamilyMap>(Region)) {
      if (*Actual != Protocol.Family)
        report("owned construct ownership class does not match operation",
               Statement, C.getState(), C);
    }
  }

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    for (const ConstructCall &Protocol : protocolsFor(Call)) {
      if (Protocol.Operation == ConstructOperation::Use) {
        requireLive(Call, Protocol, C);
        continue;
      }
      const MemRegion *Region = argumentRegion(Call, Protocol.Argument);
      if (!Region)
        continue;
      const ConstructKind *Kind = C.getState()->get<ConstructMap>(Region);
      bool StaticLive = !Kind && hasStaticInitialization(
                                     Region, Protocol.StaticInitialization);
      const Stmt *Statement = Call.getOriginExpr();
      if (!Statement)
        continue;

      if (Protocol.Operation == ConstructOperation::Construct) {
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
        continue;
      }
      requireLive(Call, Protocol, C);
    }
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    llvm::SmallVector<ConstructCall, 4> Protocols = protocolsFor(Call);
    if (Protocols.empty())
      return;
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function)
      return;
    if (Function->getReturnType()->isVoidType()) {
      ProgramStateRef State = C.getState();
      for (const ConstructCall &Protocol : Protocols) {
        if (Protocol.Operation == ConstructOperation::Use)
          continue;
        const MemRegion *Region = argumentRegion(Call, Protocol.Argument);
        if (!Region)
          continue;
        State = State->set<ConstructMap>(
            Region, Protocol.Operation == ConstructOperation::Construct
                        ? ConstructKind::Live
                        : ConstructKind::Destroyed);
        if (Protocol.Operation == ConstructOperation::Construct)
          State = State->set<ConstructFamilyMap>(Region, Protocol.Family);
      }
      C.addTransition(State);
      return;
    }
    SVal Return = Call.getReturnValue();
    if (Return.isUnknownOrUndef())
      return;
    std::optional<DefinedOrUnknownSVal> DefinedReturn =
        Return.getAs<DefinedOrUnknownSVal>();
    if (!DefinedReturn)
      return;
    SValBuilder &Builder = C.getSValBuilder();
    DefinedOrUnknownSVal Success =
        Builder.evalEQ(C.getState(), *DefinedReturn,
                       Builder.makeZeroVal(Function->getReturnType()));
    auto [Succeeded, Failed] = C.getState()->assume(Success);
    if (Succeeded) {
      for (const ConstructCall &Protocol : Protocols) {
        if (Protocol.Operation == ConstructOperation::Use)
          continue;
        const MemRegion *Region = argumentRegion(Call, Protocol.Argument);
        if (!Region)
          continue;
        ConstructKind Next = Protocol.Operation == ConstructOperation::Construct
                                 ? ConstructKind::Live
                                 : ConstructKind::Destroyed;
        Succeeded = Succeeded->set<ConstructMap>(Region, Next);
        if (Protocol.Operation == ConstructOperation::Construct)
          Succeeded =
              Succeeded->set<ConstructFamilyMap>(Region, Protocol.Family);
      }
      C.addTransition(Succeeded);
    }
    if (Failed)
      C.addTransition(Failed);
  }
};

enum class CapabilityOperation : unsigned char {
  Require,
  RequireAbsent,
  Consume,
  ConsumeAny,
  Drop,
  GrantLinear,
  GrantDuplicable
};

static DefinedOrUnknownSVal protocolSucceeded(const FunctionDecl *Function,
                                              DefinedOrUnknownSVal Return,
                                              SValBuilder &Builder,
                                              ProgramStateRef State) {
  DefinedOrUnknownSVal IsZero = Builder.evalEQ(
      State, Return, Builder.makeZeroVal(Function->getReturnType()));
  if (!Function->getReturnType()->isPointerType())
    return IsZero;
  return Builder.evalBinOp(State, BO_EQ, IsZero,
                           Builder.makeTruthVal(false), Builder.getConditionType())
      .castAs<DefinedOrUnknownSVal>();
}

struct CapabilityProtocol {
  CapabilityOperation Operation;
  const IdentifierInfo *Family;
  unsigned Argument;
  llvm::SmallVector<unsigned, 2> Parameters;
};

class OwnershipContractChecker : public Checker<check::ASTDecl<FunctionDecl>> {
  static bool isHeaderPath(StringRef Path) {
    return Path.ends_with(".h") || Path.ends_with(".hh") ||
           Path.ends_with(".hpp");
  }

  static void emitContract(const FunctionDecl *Function, const Attr *Attribute,
                           StringRef Contract, StringRef Path, unsigned Line) {
    StringRef Kind;
    if (!Function->doesThisDeclarationHaveABody())
      Kind = isHeaderPath(Path) ? "header-declaration" : "source-declaration";
    else
      Kind = Attribute->isInherited() ? "definition-inherited"
                                      : "definition-explicit";
    llvm::errs() << "ownership-contract: " << Kind << '\t' << Contract << '\t'
                 << Function->getQualifiedNameAsString() << '\t' << Path << '\t'
                 << Line << '\n';
  }

  static bool isOwnershipContract(StringRef Annotation) {
    return Annotation.starts_with("withtok:") ||
           Annotation.starts_with("withouttok:") ||
           Annotation.starts_with("consume:") ||
           Annotation.starts_with("consume_any:") ||
           Annotation.starts_with("drop:") ||
           Annotation.starts_with("grant:") ||
           Annotation.starts_with("consume_if_nonnull_return:") ||
           Annotation.starts_with("construct:") ||
           Annotation.starts_with("destroy:") ||
           Annotation.starts_with("handle:") ||
           Annotation.starts_with("withhandle:") ||
           Annotation.starts_with("static_handle:");
  }

public:
  void checkASTDecl(const FunctionDecl *Function, AnalysisManager &,
                    BugReporter &) const {
    if (!Function->getIdentifier())
      return;
    const SourceManager &SM = Function->getASTContext().getSourceManager();
    SourceLocation Location = SM.getExpansionLoc(Function->getLocation());
    StringRef Path = SM.getFilename(Location);
    unsigned Line = SM.getSpellingLineNumber(Location);
    if (Function->doesThisDeclarationHaveABody())
      llvm::errs() << "ownership-contract: definition\t-\t"
                   << Function->getQualifiedNameAsString() << '\t' << Path
                   << '\t' << Line << '\n';
    for (const AnnotateAttr *Attribute :
         Function->specific_attrs<AnnotateAttr>()) {
      StringRef Contract = Attribute->getAnnotation();
      if (isOwnershipContract(Contract))
        emitContract(Function, Attribute, Contract, Path, Line);
    }
    unsigned Argument = 1;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      for (const AnnotateAttr *Attribute :
           Parameter->specific_attrs<AnnotateAttr>()) {
        StringRef Annotation = Attribute->getAnnotation();
        if (!isOwnershipContract(Annotation))
          continue;
        std::string Contract =
            ("parameter:" + llvm::Twine(Argument) + ":" + Annotation).str();
        emitContract(Function, Attribute, Contract, Path, Line);
      }
      ++Argument;
    }
  }
};

class CapabilityTokenChecker
    : public Checker<check::BeginFunction, check::PreCall, check::PostCall,
                     check::EndFunction> {
  mutable std::unique_ptr<BugType> BT;

  static bool parameterAnnotation(
      const FunctionDecl *Function, const AnnotateAttr *Attr, StringRef Prefix,
      const IdentifierInfo *&Family,
      llvm::SmallVectorImpl<unsigned> &Parameters) {
    StringRef Text = Attr->getAnnotation();
    if (!Text.consume_front(Prefix) || Text.empty() || Text.contains(':'))
      return false;
    StringRef FamilyName = Text;
    StringRef Arguments;
    size_t Open = Text.find('(');
    if (Open != StringRef::npos) {
      if (!Text.ends_with(")"))
        return false;
      FamilyName = Text.take_front(Open).trim();
      Arguments = Text.slice(Open + 1, Text.size() - 1);
    }
    if (FamilyName.empty())
      return false;
    Family = &Function->getASTContext().Idents.get(FamilyName);
    while (!Arguments.empty()) {
      auto [Name, Rest] = Arguments.split(',');
      Name = Name.trim();
      if (Name.empty())
        return false;
      bool Found = false;
      for (unsigned Index = 0; Index < Function->getNumParams(); ++Index)
        if (Function->getParamDecl(Index)->getName() == Name) {
          Parameters.push_back(Index);
          Found = true;
          break;
        }
      if (!Found)
        return false;
      Arguments = Rest;
    }
    return true;
  }

  static const IdentifierInfo *instantiatedFamily(
      const CapabilityProtocol &Protocol, ArrayRef<SVal> Values,
      ASTContext &Context) {
    if (Values.empty())
      return Protocol.Family;
    std::string Name = Protocol.Family->getName().str();
    Name.push_back('(');
    for (unsigned Index = 0; Index < Values.size(); ++Index) {
      if (Index)
        Name.push_back(',');
      const SVal &Value = Values[Index];
      Name += std::to_string(static_cast<unsigned>(Value.getKind()));
      Name.push_back(':');
      if (const MemRegion *Region = Value.getAsRegion())
        Name += std::to_string(reinterpret_cast<uintptr_t>(Region));
      else if (SymbolRef Symbol = Value.getAsSymbol(true))
        Name += std::to_string(reinterpret_cast<uintptr_t>(Symbol));
      else {
        llvm::FoldingSetNodeID Identity;
        Value.Profile(Identity);
        Name += std::to_string(Identity.ComputeHash());
      }
    }
    Name.push_back(')');
    return &Context.Idents.get(Name);
  }

  static const IdentifierInfo *callFamily(const CapabilityProtocol &Protocol,
                                          const CallEvent &Call,
                                          CheckerContext &C) {
    llvm::SmallVector<SVal, 2> Values;
    for (unsigned Parameter : Protocol.Parameters) {
      if (Parameter >= Call.getNumArgs())
        return Protocol.Family;
      Values.push_back(Call.getArgSVal(Parameter));
    }
    return instantiatedFamily(Protocol, Values, C.getASTContext());
  }

  static const IdentifierInfo *functionFamily(
      const CapabilityProtocol &Protocol, const FunctionDecl *Function,
      ProgramStateRef State, const LocationContext *LC) {
    llvm::SmallVector<SVal, 2> Values;
    for (unsigned Parameter : Protocol.Parameters) {
      if (Parameter >= Function->getNumParams())
        return Protocol.Family;
      const ParmVarDecl *Declaration = Function->getParamDecl(Parameter);
      Values.push_back(State->getSVal(State->getLValue(Declaration, LC)));
    }
    return instantiatedFamily(Protocol, Values, Function->getASTContext());
  }

  static const ValueDecl *declarationFor(const Expr *Expression) {
    if (!Expression)
      return nullptr;
    Expression = Expression->IgnoreParenImpCasts();
    if (const auto *Reference = dyn_cast<DeclRefExpr>(Expression))
      return dyn_cast<ValueDecl>(Reference->getDecl());
    if (const auto *Member = dyn_cast<MemberExpr>(Expression))
      return Member->getMemberDecl();
    if (const auto *Unary = dyn_cast<UnaryOperator>(Expression))
      if (Unary->getOpcode() == UO_AddrOf)
        return declarationFor(Unary->getSubExpr());
    return nullptr;
  }

  static std::optional<CapabilityKind>
  declaredTokenFor(const Expr *Expression, const IdentifierInfo *Family) {
    const ValueDecl *Declaration = declarationFor(Expression);
    if (!Declaration)
      return std::nullopt;
    for (const AnnotateAttr *Attr :
         Declaration->specific_attrs<AnnotateAttr>()) {
      StringRef Text = Attr->getAnnotation();
      if (Text.consume_front("withtok:") && Text == Family->getName())
        return dialectTokenKind(Declaration->getASTContext(), Text);
    }
    return std::nullopt;
  }

  static llvm::SmallVector<CapabilityProtocol, 6>
  protocolsFor(const FunctionDecl *Function) {
    llvm::SmallVector<CapabilityProtocol, 6> Protocols;
    if (!Function)
      return Protocols;
    struct OperationAnnotation {
      llvm::StringLiteral Prefix;
      CapabilityOperation Operation;
    };
    static constexpr OperationAnnotation Operations[] = {
        {"withtok:", CapabilityOperation::Require},
        {"withouttok:", CapabilityOperation::RequireAbsent},
        {"consume:", CapabilityOperation::Consume},
        {"consume_any:", CapabilityOperation::ConsumeAny},
        {"drop:", CapabilityOperation::Drop},
        {"grant:", CapabilityOperation::GrantLinear}};
    unsigned Argument = 0;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      for (const AnnotateAttr *Attr : Parameter->specific_attrs<AnnotateAttr>())
        for (const OperationAnnotation &Candidate : Operations)
        {
            const IdentifierInfo *Family = nullptr;
            llvm::SmallVector<unsigned, 2> Parameters;
            if (parameterAnnotation(Function, Attr, Candidate.Prefix, Family,
                                    Parameters)) {
              const TypedefNameDecl *Token =
                  dialectToken(Function->getASTContext(), Family->getName());
              if (hasDialectQualifier(Token, "qual:extent_at_least") ||
                  hasDialectQualifier(Token, "qual:disjoint_extent"))
                continue;
              if ((Candidate.Operation == CapabilityOperation::Require ||
                   Candidate.Operation == CapabilityOperation::Consume) &&
                  hasDialectQualifier(
                      Token, "qual:dynamic_storage"))
                continue;
              CapabilityOperation Operation = Candidate.Operation;
              if (Candidate.Prefix == "grant:") {
                std::optional<CapabilityKind> Kind = dialectTokenKind(
                    Function->getASTContext(), Family->getName());
                if (Kind && *Kind == CapabilityKind::Duplicable)
                  Operation = CapabilityOperation::GrantDuplicable;
              }
              Protocols.push_back(
                  {Operation, Family, Argument, std::move(Parameters)});
            }
          }
      ++Argument;
    }
    return Protocols;
  }

  static bool hasInputProtocol(ArrayRef<CapabilityProtocol> Protocols,
                               const CapabilityProtocol &Output) {
    return llvm::any_of(Protocols, [&](const CapabilityProtocol &Input) {
      return Input.Argument == Output.Argument &&
             Input.Family == Output.Family &&
             Input.Parameters == Output.Parameters &&
             (Input.Operation == CapabilityOperation::Require ||
              Input.Operation == CapabilityOperation::Consume ||
              Input.Operation == CapabilityOperation::ConsumeAny ||
              Input.Operation == CapabilityOperation::Drop);
    });
  }

  static bool acceptsStaticInitialization(const FunctionDecl *Function,
                                          unsigned Argument) {
    if (!Function)
      return false;
    if (Argument >= Function->getNumParams())
      return false;
    const ParmVarDecl *Parameter = Function->getParamDecl(Argument);
    for (const AnnotateAttr *Attr : Parameter->specific_attrs<AnnotateAttr>()) {
      StringRef Text = Attr->getAnnotation();
      if (Text.consume_front("static_handle:") && !Text.empty() &&
          !Text.contains(':'))
        return true;
    }
    return false;
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

  static bool hasStaticInitialToken(const FunctionDecl *Function,
                                    const CallEvent &Call, unsigned Argument,
                                    const CapabilityPresence &Existing) {
    if (Existing.Known || !acceptsStaticInitialization(Function, Argument) ||
        Argument >= Call.getNumArgs())
      return false;
    const auto *Variable =
        dyn_cast_or_null<VarRegion>(Call.getArgSVal(Argument).getAsRegion());
    if (!Variable)
      return false;
    const VarDecl *Declaration = Variable->getDecl();
    if (!Declaration->hasInit())
      return Declaration->hasGlobalStorage();
    return isZeroInitializer(Declaration->getInit());
  }

  void report(StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Invalid ownership capability",
                                     categories::MemoryError);
    auto Report = std::make_unique<PathSensitiveBugReport>(
        *BT, diagnosticMessage(Reason, Statement, C), Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  bool preconditionsHold(ProgramStateRef State, const CallEvent &Call,
                         ArrayRef<CapabilityProtocol> Protocols,
                         CheckerContext &C, bool EmitDiagnostics) const {
    const Stmt *Statement = Call.getOriginExpr();
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    bool Valid = true;
    for (const CapabilityProtocol &Protocol : Protocols) {
      if (Protocol.Argument >= Call.getNumArgs())
        continue;
      SVal Value = Call.getArgSVal(Protocol.Argument);
      const IdentifierInfo *StateFamily = callFamily(Protocol, Call, C);
      CapabilityPresence Existing = capabilityFor(
          State, carrierRegion(Call.getArgExpr(Protocol.Argument), C), Value,
          StateFamily);
      if (Protocol.Parameters.empty() && !Existing.Known && !Existing.Kind)
        Existing.Kind =
            declaredTokenFor(Call.getArgExpr(Protocol.Argument),
                             Protocol.Family);
      if (!Existing.Kind && expressionProvidesStringLiteralToken(
                                Call.getArgExpr(Protocol.Argument),
                                Protocol.Family, C.getASTContext()))
        Existing.Kind = CapabilityKind::Duplicable;
      if (!Existing.Kind &&
          hasStaticInitialToken(Function, Call, Protocol.Argument, Existing))
        Existing.Kind = CapabilityKind::Linear;
      if (Protocol.Operation == CapabilityOperation::ConsumeAny)
        continue;
      if ((Protocol.Operation == CapabilityOperation::Require ||
           Protocol.Operation == CapabilityOperation::Consume) &&
          !Existing.Kind) {
        if (EmitDiagnostics && Statement)
          report("required ownership capability token is not held", Statement,
                 State, C);
        Valid = false;
      } else if (Protocol.Operation == CapabilityOperation::RequireAbsent &&
                 Existing.Kind) {
        if (EmitDiagnostics && Statement)
          report(
              "operation is blocked while ownership capability token is held",
              Statement, State, C);
        Valid = false;
      } else if (Protocol.Operation == CapabilityOperation::GrantLinear &&
                 Existing.Kind) {
        if (EmitDiagnostics && Statement)
          report("linear ownership capability token would be duplicated",
                 Statement, State, C);
        Valid = false;
      } else if (Protocol.Operation == CapabilityOperation::GrantDuplicable &&
                 Existing.Kind &&
                 *Existing.Kind != CapabilityKind::Duplicable) {
        if (EmitDiagnostics && Statement)
          report("ownership capability token duplication class does not match",
                 Statement, State, C);
        Valid = false;
      }
    }
    llvm::SmallVector<unsigned, 4> CheckedArguments;
    for (const CapabilityProtocol &Alternative : Protocols) {
      if (Alternative.Operation != CapabilityOperation::ConsumeAny ||
          llvm::is_contained(CheckedArguments, Alternative.Argument))
        continue;
      CheckedArguments.push_back(Alternative.Argument);
      bool Held = false;
      for (const CapabilityProtocol &Candidate : Protocols)
        if (Candidate.Operation == CapabilityOperation::ConsumeAny &&
            Candidate.Argument == Alternative.Argument) {
          CapabilityPresence Existing = capabilityFor(
              State, carrierRegion(Call.getArgExpr(Candidate.Argument), C),
              Call.getArgSVal(Candidate.Argument),
              callFamily(Candidate, Call, C));
          if (Candidate.Parameters.empty() && !Existing.Known && !Existing.Kind)
            Existing.Kind =
                declaredTokenFor(Call.getArgExpr(Candidate.Argument),
                                 Candidate.Family);
          if (!Existing.Kind && expressionProvidesStringLiteralToken(
                                    Call.getArgExpr(Candidate.Argument),
                                    Candidate.Family, C.getASTContext()))
            Existing.Kind = CapabilityKind::Duplicable;
          if (!Existing.Kind &&
              hasStaticInitialToken(Function, Call, Candidate.Argument,
                                    Existing))
            Existing.Kind = CapabilityKind::Linear;
          if (!Existing.Kind)
            continue;
          Held = true;
          break;
        }
      if (!Held) {
        if (EmitDiagnostics && Statement)
          report("none of the required ownership capability tokens is held",
                 Statement, State, C);
        Valid = false;
      }
    }
    return Valid;
  }

  static ProgramStateRef transition(ProgramStateRef State,
                                    const CallEvent &Call,
                                    ArrayRef<CapabilityProtocol> Protocols,
                                    CheckerContext &C) {
    llvm::SmallVector<unsigned, 4> ConsumedArguments;
    for (const CapabilityProtocol &Alternative : Protocols) {
      if (Alternative.Operation != CapabilityOperation::ConsumeAny ||
          llvm::is_contained(ConsumedArguments, Alternative.Argument))
        continue;
      ConsumedArguments.push_back(Alternative.Argument);
      for (const CapabilityProtocol &Candidate : Protocols)
        if (Candidate.Operation == CapabilityOperation::ConsumeAny &&
            Candidate.Argument == Alternative.Argument) {
          SVal Value = Call.getArgSVal(Candidate.Argument);
          const MemRegion *Carrier =
              carrierRegion(Call.getArgExpr(Candidate.Argument), C);
          const IdentifierInfo *StateFamily = callFamily(Candidate, Call, C);
          if (capabilityFor(State, Carrier, Value, StateFamily).Kind) {
            State =
                removeOperationToken(State, Carrier, Value, StateFamily);
            break;
          }
        }
    }
    for (const CapabilityProtocol &Protocol : Protocols) {
      if (Protocol.Argument >= Call.getNumArgs())
        continue;
      SVal Value = Call.getArgSVal(Protocol.Argument);
      const MemRegion *Carrier =
          carrierRegion(Call.getArgExpr(Protocol.Argument), C);
      const IdentifierInfo *StateFamily = callFamily(Protocol, Call, C);
      switch (Protocol.Operation) {
      case CapabilityOperation::Require:
      case CapabilityOperation::RequireAbsent:
        break;
      case CapabilityOperation::Consume:
        State = removeOperationToken(State, Carrier, Value, StateFamily);
        break;
      case CapabilityOperation::ConsumeAny:
        break;
      case CapabilityOperation::Drop:
        State = removeOperationToken(State, Carrier, Value, StateFamily);
        break;
      case CapabilityOperation::GrantLinear:
        State = setOperationToken(State, Carrier, Value, StateFamily,
                                  CapabilityKind::Linear);
        break;
      case CapabilityOperation::GrantDuplicable:
        State = setOperationToken(State, Carrier, Value, StateFamily,
                                  CapabilityKind::Duplicable);
        break;
      }
    }
    return State;
  }

public:
  void checkBeginFunction(CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function)
      return;
    ProgramStateRef State = C.getState();
    const LocationContext *LC = C.getLocationContext();
    bool Changed = false;
    llvm::SmallVector<CapabilityProtocol, 6> Protocols = protocolsFor(Function);
    for (const CapabilityProtocol &Protocol : Protocols) {
      const ParmVarDecl *Parameter = Function->getParamDecl(Protocol.Argument);
      SVal Value = State->getSVal(State->getLValue(Parameter, LC));
      const MemRegion *Carrier = State->getLValue(Parameter, LC).getAsRegion();
      const IdentifierInfo *StateFamily =
          functionFamily(Protocol, Function, State, LC);
      if (Protocol.Operation == CapabilityOperation::Require ||
          Protocol.Operation == CapabilityOperation::Consume ||
          Protocol.Operation == CapabilityOperation::Drop) {
        CapabilityKind Kind = dialectTokenKind(
                                  Function->getASTContext(),
                                  Protocol.Family->getName())
                                  .value_or(CapabilityKind::Linear);
        State = setOperationToken(State, Carrier, Value, StateFamily,
                                  Kind);
        Changed = true;
      } else if ((Protocol.Operation == CapabilityOperation::GrantLinear ||
                  Protocol.Operation == CapabilityOperation::GrantDuplicable) &&
                 !hasInputProtocol(Protocols, Protocol)) {
        State = removeOperationToken(State, Carrier, Value, StateFamily);
        Changed = true;
      }
    }
    llvm::SmallVector<ProgramStateRef, 4> Alternatives{State};
    llvm::SmallVector<unsigned, 4> ExpandedArguments;
    for (const CapabilityProtocol &Protocol : Protocols) {
      if (Protocol.Operation != CapabilityOperation::ConsumeAny ||
          llvm::is_contained(ExpandedArguments, Protocol.Argument))
        continue;
      ExpandedArguments.push_back(Protocol.Argument);
      const ParmVarDecl *Parameter =
          Function->getParamDecl(Protocol.Argument);
      SVal Value = State->getSVal(State->getLValue(Parameter, LC));
      const MemRegion *Carrier =
          State->getLValue(Parameter, LC).getAsRegion();
      llvm::SmallVector<ProgramStateRef, 4> Expanded;
      for (ProgramStateRef Alternative : Alternatives)
        for (const CapabilityProtocol &Candidate : Protocols) {
          if (Candidate.Operation != CapabilityOperation::ConsumeAny ||
              Candidate.Argument != Protocol.Argument)
            continue;
          CapabilityKind Kind = dialectTokenKind(
                                    Function->getASTContext(),
                                    Candidate.Family->getName())
                                    .value_or(CapabilityKind::Linear);
          Expanded.push_back(setOperationToken(
              Alternative, Carrier, Value,
              functionFamily(Candidate, Function, Alternative, LC), Kind));
        }
      Alternatives = std::move(Expanded);
      Changed = true;
    }
    if (Changed)
      for (ProgramStateRef Alternative : Alternatives)
        C.addTransition(Alternative);
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    llvm::SmallVector<CapabilityProtocol, 6> Protocols = protocolsFor(Function);
    if (!Protocols.empty())
      preconditionsHold(C.getState(), Call, Protocols, C, true);
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    llvm::SmallVector<CapabilityProtocol, 6> Protocols = protocolsFor(Function);
    if (Protocols.empty() ||
        !preconditionsHold(C.getState(), Call, Protocols, C, false))
      return;

    ProgramStateRef State = C.getState();
    if (Function->getReturnType()->isVoidType()) {
      C.addTransition(transition(State, Call, Protocols, C));
      return;
    }
    std::optional<DefinedOrUnknownSVal> Return =
        Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
    if (!Return)
      return;
    DefinedOrUnknownSVal Success = protocolSucceeded(
        Function, *Return, C.getSValBuilder(), State);
    auto [Succeeded, Failed] = State->assume(Success);
    if (Succeeded)
      C.addTransition(transition(Succeeded, Call, Protocols, C));
    if (Failed)
      C.addTransition(Failed);
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function || !Function->doesThisDeclarationHaveABody())
      return;
    ProgramStateRef State = C.getState();
    if (!Function->getReturnType()->isVoidType()) {
      if (!Return || !Return->getRetValue())
        return;
      std::optional<DefinedOrUnknownSVal> Result =
          C.getSVal(Return->getRetValue()).getAs<DefinedOrUnknownSVal>();
      if (!Result)
        return;
      DefinedOrUnknownSVal IsSuccess = protocolSucceeded(
          Function, *Result, C.getSValBuilder(), State);
      State = State->assume(IsSuccess).first;
      if (!State)
        return;
    }
    const Stmt *Site =
        Return ? static_cast<const Stmt *>(Return) : Function->getBody();
    const LocationContext *LC = C.getLocationContext();
    for (const CapabilityProtocol &Protocol : protocolsFor(Function)) {
      if (Protocol.Argument >= Function->getNumParams())
        continue;
      const ParmVarDecl *Parameter = Function->getParamDecl(Protocol.Argument);
      Loc Location = State->getLValue(Parameter, LC);
      SVal Value = State->getSVal(Location);
      const IdentifierInfo *StateFamily =
          functionFamily(Protocol, Function, State, LC);
      CapabilityPresence Present =
          capabilityFor(State, Location.getAsRegion(), Value, StateFamily);
      bool Regranted = llvm::any_of(
          protocolsFor(Function), [&](const CapabilityProtocol &Output) {
            return Output.Argument == Protocol.Argument &&
                   Output.Family == Protocol.Family &&
                   Output.Parameters == Protocol.Parameters &&
                   (Output.Operation == CapabilityOperation::GrantLinear ||
                    Output.Operation ==
                        CapabilityOperation::GrantDuplicable);
          });
      if ((Protocol.Operation == CapabilityOperation::Consume ||
           Protocol.Operation == CapabilityOperation::ConsumeAny ||
           Protocol.Operation == CapabilityOperation::Drop) &&
          Present.Kind && !Regranted) {
        report("declared ownership token drop is not proven by function body",
               Site, State, C);
        return;
      }
      if (Protocol.Operation != CapabilityOperation::GrantLinear &&
          Protocol.Operation != CapabilityOperation::GrantDuplicable)
        continue;
      CapabilityKind Required =
          Protocol.Operation == CapabilityOperation::GrantLinear
              ? CapabilityKind::Linear
              : CapabilityKind::Duplicable;
      if (!Present.Kind || *Present.Kind != Required) {
        report("declared ownership token addition is not proven by function "
               "body",
               Site, State, C);
        return;
      }
    }
  }
};

enum class OwnershipTypeMember : unsigned char {
  Handle,
  LinearToken,
  DuplicableToken
};

struct OwnershipTypeEntry {
  const IdentifierInfo *Family;
  OwnershipTypeMember Member;
};

class OwnershipTypeChecker
    : public Checker<
          check::BeginFunction, check::PreStmt<DeclStmt>,
          check::PostStmt<DeclStmt>, check::PreStmt<BinaryOperator>,
          check::PostStmt<BinaryOperator>, check::PreStmt<UnaryOperator>,
          check::PreStmt<ArraySubscriptExpr>, check::PreStmt<MemberExpr>,
          check::PostStmt<ImplicitCastExpr>, check::PreStmt<ReturnStmt>,
          check::BranchCondition, check::PreCall, check::PostCall,
          check::EndFunction> {
  mutable std::unique_ptr<BugType> BT;

  static llvm::SmallVector<OwnershipTypeEntry, 4>
  bundleFor(const ValueDecl *Declaration) {
    llvm::SmallVector<OwnershipTypeEntry, 4> Bundle;
    if (!Declaration)
      return Bundle;
    struct MemberAnnotation {
      llvm::StringLiteral Prefix;
      OwnershipTypeMember Member;
    };
    static constexpr MemberAnnotation Annotations[] = {
        {"withhandle:", OwnershipTypeMember::Handle}};
    for (const AnnotateAttr *Attr :
         Declaration->specific_attrs<AnnotateAttr>()) {
      for (const MemberAnnotation &Candidate : Annotations) {
        StringRef Text = Attr->getAnnotation();
        if (!Text.consume_front(Candidate.Prefix) || Text.empty() ||
            Text.contains(':'))
          continue;
        Bundle.push_back(
            {&Declaration->getASTContext().Idents.get(Text), Candidate.Member});
      }
      StringRef Text = Attr->getAnnotation();
      if (!Text.consume_front("withtok:") || Text.empty() ||
          Text.contains(':'))
        continue;
      std::optional<CapabilityKind> Kind =
          dialectTokenKind(Declaration->getASTContext(), Text);
      if (!Kind)
        continue;
      if (hasDialectQualifier(
              dialectToken(Declaration->getASTContext(), Text),
              "qual:dynamic_storage"))
        continue;
      Bundle.push_back(
          {&Declaration->getASTContext().Idents.get(Text),
           *Kind == CapabilityKind::Duplicable
               ? OwnershipTypeMember::DuplicableToken
               : OwnershipTypeMember::LinearToken});
    }
    return Bundle;
  }

  static const ValueDecl *declarationFor(const Expr *Expression) {
    if (!Expression)
      return nullptr;
    Expression = Expression->IgnoreParenImpCasts();
    if (const auto *Reference = dyn_cast<DeclRefExpr>(Expression))
      return dyn_cast<ValueDecl>(Reference->getDecl());
    if (const auto *Member = dyn_cast<MemberExpr>(Expression))
      return Member->getMemberDecl();
    if (const auto *Call = dyn_cast<CallExpr>(Expression))
      return Call->getDirectCallee();
    if (const auto *Unary = dyn_cast<UnaryOperator>(Expression))
      if (Unary->getOpcode() == UO_AddrOf)
        return declarationFor(Unary->getSubExpr());
    return nullptr;
  }

  static llvm::SmallVector<OwnershipTypeEntry, 4>
  bundleFor(const Expr *Expression) {
    return bundleFor(declarationFor(Expression));
  }

  struct SentinelTrait {
    const IdentifierInfo *Family;
    int64_t Value;
  };

  static llvm::SmallVector<SentinelTrait, 2>
  dialectSentinelTraits(const ValueDecl *Declaration) {
    llvm::SmallVector<SentinelTrait, 2> Traits;
    if (!Declaration)
      return Traits;
    for (const OwnershipTypeEntry &Entry : bundleFor(Declaration)) {
      if (Entry.Member == OwnershipTypeMember::Handle)
        continue;
      const TypedefNameDecl *Token =
          dialectToken(Declaration->getASTContext(), Entry.Family->getName());
      if (std::optional<int64_t> Sentinel = dialectExcludedSentinel(Token))
        Traits.push_back({Entry.Family, *Sentinel});
    }
    return Traits;
  }

  static std::optional<int64_t> integerConstant(const Expr *Expression,
                                                ASTContext &Context) {
    if (!Expression)
      return std::nullopt;
    Expression = Expression->IgnoreParenImpCasts();
    std::optional<llvm::APSInt> Value =
        Expression->getIntegerConstantExpr(Context);
    if (!Value || !Value->isSignedIntN(64))
      return std::nullopt;
    return Value->getSExtValue();
  }

  static bool contains(ArrayRef<OwnershipTypeEntry> Bundle,
                       const OwnershipTypeEntry &Wanted) {
    return llvm::any_of(Bundle, [&](const OwnershipTypeEntry &Entry) {
      return Entry.Family == Wanted.Family && Entry.Member == Wanted.Member;
    });
  }

  void report(StringRef Reason, const Stmt *Statement, ProgramStateRef State,
              CheckerContext &C) const {
    ExplodedNode *Node = C.generateNonFatalErrorNode(State);
    if (!Node)
      return;
    if (!BT)
      BT = std::make_unique<BugType>(this, "Mismatched ownership type",
                                     categories::MemoryError);
    auto Report = std::make_unique<PathSensitiveBugReport>(
        *BT, diagnosticMessage(Reason, Statement, C), Node);
    Report->addRange(Statement->getSourceRange());
    C.emitReport(std::move(Report));
  }

  void requireSameBundle(const ValueDecl *Destination, const Expr *Source,
                         const Stmt *Statement, CheckerContext &C) const {
    if (!Statement)
      return;
    if (Source->isNullPointerConstant(
            C.getASTContext(), Expr::NPC_ValueDependentIsNotNull))
      return;
    llvm::SmallVector<OwnershipTypeEntry, 4> DestinationBundle =
        bundleFor(Destination);
    llvm::SmallVector<OwnershipTypeEntry, 4> SourceBundle = bundleFor(Source);
    ProgramStateRef State = C.getState();
    const MemRegion *SourceCarrier = carrierRegion(Source, C);
    SVal SourceValue = C.getSVal(Source);
    if (SourceCarrier && State->contains<ExpiredStrictLoanSet>(SourceCarrier)) {
      report("borrow accesses a consumed owner", Statement, State, C);
      return;
    }
    for (const OwnershipTypeEntry &Entry : DestinationBundle) {
      if (Entry.Member == OwnershipTypeMember::Handle)
        continue;
      CapabilityPresence Present =
          capabilityFor(State, SourceCarrier, SourceValue, Entry.Family);
      if (!Present.Kind &&
          expressionProvidesStringLiteralToken(Source, Entry.Family,
                                               C.getASTContext()))
        Present.Kind = CapabilityKind::Duplicable;
      if (!Present.Kind) {
        if (dialectTokenExcludes(Entry.Family, Source, C.getASTContext()))
          continue;
        report(contains(SourceBundle, Entry)
                   ? "source ownership token has already moved"
                   : "source ownership type does not provide destination "
                     "token bundle",
               Statement, State, C);
        return;
      }
      CapabilityKind Required = Entry.Member == OwnershipTypeMember::LinearToken
                                    ? CapabilityKind::Linear
                                    : CapabilityKind::Duplicable;
      if (*Present.Kind != Required) {
        report("ownership token duplication class does not match", Statement,
               State, C);
        return;
      }
    }
  }

  static SVal valueForExpression(const Expr *Expression, CheckerContext &C) {
    const Expr *Core = Expression ? Expression->IgnoreParenImpCasts() : nullptr;
    if (const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Core))
      if (const auto *Variable = dyn_cast<VarDecl>(Reference->getDecl())) {
        ProgramStateRef State = C.getState();
        return State->getSVal(
            State->getLValue(Variable, C.getLocationContext()));
      }
    return Expression ? C.getSVal(Expression) : UnknownVal();
  }

  static ProgramStateRef expireStrictLoans(
      ProgramStateRef State, const MemRegion *Owner,
      const IdentifierInfo *Family) {
    if (!Owner || !Family)
      return State;
    for (const auto &Loan : State->get<StrictLoanMap>())
      if (Loan.second == Owner && Loan.first.second == Family)
        State = State->add<ExpiredStrictLoanSet>(Loan.first.first);
    return State;
  }

  static ProgramStateRef copyStrictLoans(ProgramStateRef State,
                                         const MemRegion *Destination,
                                         const MemRegion *Source) {
    if (!Destination || !Source || Destination == Source)
      return State;
    for (const auto &Loan : State->get<StrictLoanMap>())
      if (Loan.first.first == Source)
        State = State->set<StrictLoanMap>(
            {Destination, Loan.first.second}, Loan.second);
    if (State->contains<ExpiredStrictLoanSet>(Source))
      State = State->add<ExpiredStrictLoanSet>(Destination);
    return State;
  }

  void requireDereferenceAllowed(const Expr *Pointer, const Stmt *Statement,
                                 CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    SVal Value = C.getSVal(Pointer);
    const MemRegion *Carrier = carrierRegion(Pointer, C);
    if (Carrier && State->contains<ExpiredStrictLoanSet>(Carrier)) {
      report("borrow accesses a consumed owner", Statement, State, C);
      return;
    }
    for (const OwnershipTypeEntry &Entry : bundleFor(declarationFor(Pointer))) {
      if (Entry.Member == OwnershipTypeMember::Handle)
        continue;
      const TypedefNameDecl *Token =
          dialectToken(C.getASTContext(), Entry.Family->getName());
      if (hasDialectQualifier(Token, "qual:blocks_dereference") &&
          capabilityFor(State, Carrier, Value, Entry.Family).Kind) {
        report("pointer operation is blocked while unchecked ownership token "
               "is held",
               Statement, State, C);
        return;
      }
    }
  }

  ProgramStateRef transferTokens(const ValueDecl *Destination,
                                 const MemRegion *DestinationCarrier,
                                 const Expr *Source, const Stmt *Statement,
                                 ProgramStateRef State,
                                 CheckerContext &C) const {
    if (!Destination || !Source || !Statement)
      return State;
    llvm::SmallVector<OwnershipTypeEntry, 4> DestinationBundle =
        bundleFor(Destination);
    llvm::SmallVector<OwnershipTypeEntry, 4> SourceBundle = bundleFor(Source);
    const MemRegion *SourceCarrier = carrierRegion(Source, C);
    SVal SourceValue = C.getSVal(Source);

    State = copyStrictLoans(State, DestinationCarrier, SourceCarrier);

    for (const OwnershipTypeEntry &Entry : SourceBundle) {
      if (Entry.Member != OwnershipTypeMember::LinearToken ||
          contains(DestinationBundle, Entry) || !SourceCarrier ||
          !DestinationCarrier || SourceCarrier == DestinationCarrier)
        continue;
      const TypedefNameDecl *Token =
          dialectToken(C.getASTContext(), Entry.Family->getName());
      if (!dialectTokenPermitsCarrierCopy(Token))
        State = State->set<StrictLoanMap>(
            {DestinationCarrier, Entry.Family}, SourceCarrier);
    }

    if (Source->isNullPointerConstant(
            C.getASTContext(), Expr::NPC_ValueDependentIsNotNull)) {
      for (const OwnershipTypeEntry &Entry : DestinationBundle)
        if (Entry.Member != OwnershipTypeMember::Handle)
          State = removeCarrierToken(State, DestinationCarrier, Entry.Family);
      return State;
    }

    for (const OwnershipTypeEntry &Entry : SourceBundle) {
      if (Entry.Member == OwnershipTypeMember::Handle ||
          contains(DestinationBundle, Entry))
        continue;
      State = removeCarrierToken(State, DestinationCarrier, Entry.Family);
    }

    for (const OwnershipTypeEntry &Entry : DestinationBundle) {
      if (Entry.Member == OwnershipTypeMember::Handle)
        continue;
      if (dialectTokenExcludes(Entry.Family, Source, C.getASTContext())) {
        State = removeCarrierToken(State, DestinationCarrier, Entry.Family);
        continue;
      }
      CapabilityPresence SourceToken =
          capabilityFor(State, SourceCarrier, SourceValue, Entry.Family);
      if (!SourceToken.Kind &&
          expressionProvidesStringLiteralToken(Source, Entry.Family,
                                               C.getASTContext()))
        SourceToken.Kind = CapabilityKind::Duplicable;
      if (!SourceToken.Kind)
        continue;
      CapabilityKind Required = Entry.Member == OwnershipTypeMember::LinearToken
                                    ? CapabilityKind::Linear
                                    : CapabilityKind::Duplicable;
      if (*SourceToken.Kind != Required) {
        continue;
      }
      State =
          setCarrierToken(State, DestinationCarrier, Entry.Family, Required);
      if (Required == CapabilityKind::Linear && SourceCarrier &&
          SourceCarrier != DestinationCarrier) {
        const TypedefNameDecl *Token =
            dialectToken(C.getASTContext(), Entry.Family->getName());
        if (!dialectTokenPermitsCarrierCopy(Token))
          State = expireStrictLoans(State, SourceCarrier, Entry.Family);
        State = removeCarrierToken(State, SourceCarrier, Entry.Family);
      }
    }
    return State;
  }

public:
  void checkBeginFunction(CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (!Function)
      return;
    ProgramStateRef State = C.getState();
    const LocationContext *LC = C.getLocationContext();
    bool Changed = false;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      Loc ParameterLocation = State->getLValue(Parameter, LC);
      SVal Value = State->getSVal(ParameterLocation);
      const MemRegion *Carrier = ParameterLocation.getAsRegion();
      for (const OwnershipTypeEntry &Entry : bundleFor(Parameter)) {
        if (Entry.Member == OwnershipTypeMember::Handle)
          continue;
        CapabilityKind Kind = Entry.Member == OwnershipTypeMember::LinearToken
                                  ? CapabilityKind::Linear
                                  : CapabilityKind::Duplicable;
        State = setCarrierToken(State, Carrier, Entry.Family, Kind);
        State = setUnderlyingToken(State, Value, Entry.Family, Kind);
        Changed = true;
      }
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPreStmt(const DeclStmt *Statement, CheckerContext &C) const {
    for (const Decl *Declaration : Statement->decls()) {
      const auto *Variable = dyn_cast<VarDecl>(Declaration);
      if (Variable && Variable->hasInit())
        requireSameBundle(Variable, Variable->getInit(), Statement, C);
    }
  }

  void checkPostStmt(const DeclStmt *Statement, CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (const Decl *Declaration : Statement->decls()) {
      const auto *Variable = dyn_cast<VarDecl>(Declaration);
      if (!Variable || !Variable->hasInit())
        continue;
      const MemRegion *DestinationCarrier =
          State->getLValue(Variable, C.getLocationContext()).getAsRegion();
      State = transferTokens(Variable, DestinationCarrier, Variable->getInit(),
                             Statement, State, C);
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPreStmt(const BinaryOperator *Statement, CheckerContext &C) const {
    if (!Statement->isAssignmentOp())
      return;
    requireSameBundle(declarationFor(Statement->getLHS()), Statement->getRHS(),
                      Statement, C);
  }

  void consumeEqualityToken(const BinaryOperator *Statement,
                            CheckerContext &C) const {
    if (Statement->getOpcode() != BO_EQ)
      return;
    const Expr *ValueExpression = Statement->getLHS();
    std::optional<int64_t> Sentinel =
        integerConstant(Statement->getRHS(), C.getASTContext());
    if (!Sentinel) {
      ValueExpression = Statement->getRHS();
      Sentinel = integerConstant(Statement->getLHS(), C.getASTContext());
    }
    if (!Sentinel)
      return;
    ProgramStateRef State = C.getState();
    SVal Value = C.getSVal(ValueExpression);
    const MemRegion *Carrier = carrierRegion(ValueExpression, C);
    bool Changed = false;
    llvm::SmallVector<SentinelTrait, 2> Traits =
        dialectSentinelTraits(declarationFor(ValueExpression));
    for (const SentinelTrait &Trait : Traits) {
      if (Trait.Value != *Sentinel ||
          !capabilityFor(State, Carrier, Value, Trait.Family).Kind)
        continue;
      State = removeCarrierToken(State, Carrier, Trait.Family);
      State = removeUnderlyingToken(State, Value, Trait.Family);
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPostStmt(const BinaryOperator *Statement, CheckerContext &C) const {
    if (Statement->isAssignmentOp()) {
      const ValueDecl *Destination = declarationFor(Statement->getLHS());
      const MemRegion *DestinationCarrier =
          carrierRegion(Statement->getLHS(), C);
      ProgramStateRef State =
          transferTokens(Destination, DestinationCarrier, Statement->getRHS(),
                         Statement, C.getState(), C);
      C.addTransition(State);
      return;
    }
    consumeEqualityToken(Statement, C);
  }

  void checkBranchCondition(const Stmt *Statement, CheckerContext &C) const {
    if (const auto *Comparison = dyn_cast<BinaryOperator>(Statement))
      consumeEqualityToken(Comparison, C);
  }

  void consumeSwitchToken(const SwitchStmt *Statement,
                          CheckerContext &C) const {
    const Expr *Condition = Statement->getCond();
    ProgramStateRef State = C.getState();
    SVal Value = valueForExpression(Condition, C);
    const MemRegion *Carrier = carrierRegion(Condition, C);
    bool Changed = false;
    llvm::SmallVector<SentinelTrait, 2> Traits =
        dialectSentinelTraits(declarationFor(Condition));
    for (const SentinelTrait &Trait : Traits) {
      bool HasSentinelCase = false;
      for (const SwitchCase *Case = Statement->getSwitchCaseList(); Case;
           Case = Case->getNextSwitchCase()) {
        const auto *ValueCase = dyn_cast<CaseStmt>(Case);
        if (!ValueCase)
          continue;
        std::optional<int64_t> CaseValue =
            integerConstant(ValueCase->getLHS(), C.getASTContext());
        if (CaseValue && *CaseValue == Trait.Value) {
          HasSentinelCase = true;
          break;
        }
      }
      if (!HasSentinelCase ||
          !capabilityFor(State, Carrier, Value, Trait.Family).Kind)
        continue;
      State = removeCarrierToken(State, Carrier, Trait.Family);
      State = removeUnderlyingToken(State, Value, Trait.Family);
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPostStmt(const ImplicitCastExpr *Statement,
                     CheckerContext &C) const {
    const Stmt *Current = Statement;
    for (unsigned Depth = 0; Current && Depth != 4; ++Depth) {
      DynTypedNodeList Parents = C.getASTContext().getParents(*Current);
      if (Parents.empty())
        return;
      if (const auto *Switch = Parents[0].get<SwitchStmt>()) {
        consumeSwitchToken(Switch, C);
        return;
      }
      Current = Parents[0].get<Expr>();
    }
  }

  void checkPreStmt(const UnaryOperator *Statement, CheckerContext &C) const {
    if (Statement->getOpcode() == UO_Deref)
      requireDereferenceAllowed(Statement->getSubExpr(), Statement, C);
  }

  void checkPreStmt(const ArraySubscriptExpr *Statement,
                    CheckerContext &C) const {
    requireDereferenceAllowed(Statement->getBase(), Statement, C);
  }

  void checkPreStmt(const MemberExpr *Statement, CheckerContext &C) const {
    if (Statement->isArrow())
      requireDereferenceAllowed(Statement->getBase(), Statement, C);
  }

  void checkPreStmt(const ReturnStmt *Statement, CheckerContext &C) const {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(C.getLocationContext()->getDecl());
    if (Function && Statement->getRetValue()) {
      requireSameBundle(Function, Statement->getRetValue(), Statement, C);
      ProgramStateRef State =
          transferTokens(Function, nullptr, Statement->getRetValue(), Statement,
                         C.getState(), C);
      C.addTransition(State);
    }
  }

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function)
      return;
    unsigned Count = std::min(Call.getNumArgs(), Function->getNumParams());
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (unsigned Argument = 0; Argument < Count; ++Argument) {
      requireSameBundle(Function->getParamDecl(Argument),
                        Call.getArgExpr(Argument), Call.getOriginExpr(), C);
      State = transferTokens(Function->getParamDecl(Argument), nullptr,
                             Call.getArgExpr(Argument), Call.getOriginExpr(),
                             State, C);
      for (const AnnotateAttr *Attribute :
           Function->getParamDecl(Argument)->specific_attrs<AnnotateAttr>()) {
        StringRef Text = Attribute->getAnnotation();
        if (!Text.consume_front("consume:") || Text.empty() ||
            Text.contains(':'))
          continue;
        const TypedefNameDecl *Token =
            dialectToken(C.getASTContext(), Text);
        if (!dialectTokenPermitsCarrierCopy(Token))
          State = expireStrictLoans(
              State, carrierRegion(Call.getArgExpr(Argument), C),
              &C.getASTContext().Idents.get(Text));
      }
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function)
      return;
    ProgramStateRef State = C.getState();
    bool Changed = false;
    for (const OwnershipTypeEntry &Entry : bundleFor(Function)) {
      if (Entry.Member == OwnershipTypeMember::Handle)
        continue;
      State =
          setUnderlyingToken(State, Call.getReturnValue(), Entry.Family,
                             Entry.Member == OwnershipTypeMember::LinearToken
                                 ? CapabilityKind::Linear
                                 : CapabilityKind::Duplicable);
      Changed = true;
    }
    if (Changed)
      C.addTransition(State);
  }

  void checkEndFunction(const ReturnStmt *Return, CheckerContext &C) const {
    ProgramStateRef State = C.getState();
    for (const auto &Entry : State->get<CarrierCapabilityMap>()) {
      if (Entry.second == CarrierCapabilityKind::Absent)
        continue;
      const MemRegion *Carrier = Entry.first.first;
      const IdentifierInfo *Family = Entry.first.second;
      const auto *Variable =
          dyn_cast_or_null<VarRegion>(Carrier ? Carrier->getBaseRegion()
                                              : nullptr);
      if (!Variable || Variable->getStackFrame() != C.getStackFrame())
        continue;
      /* Parameter tokens describe the function boundary: withtok preserves
       * them, while consume/grant postconditions are proved independently by
       * CapabilityTokenChecker.  They are not local values being abandoned
       * at this return site. */
      if (isa<ParmVarDecl>(Variable->getDecl()))
        continue;
      const TypedefNameDecl *Token =
          dialectToken(C.getASTContext(), Family->getName());
      if (!Token || hasDialectQualifier(Token, "qual:implicit_drop"))
        continue;
      const auto *Function = dyn_cast_or_null<FunctionDecl>(
          C.getLocationContext()->getDecl());
      const Stmt *Site = Return ? static_cast<const Stmt *>(Return)
                                : Function ? Function->getBody() : nullptr;
      if (Site)
        report("ownership token is not implicitly droppable", Site, State, C);
      return;
    }
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
  //
  // Beyond these three individually-named cases, also honor GCC/
  // Clang's own `returns_nonnull` attribute -- the exact return-value
  // counterpart of the `nonnull` parameter attribute checkBeginFunction
  // already trusts below, and the standard way a function states "this
  // never returns NULL" as part of its real, published contract rather
  // than something a caller must re-derive. src/string/strchr.c's own
  // `char *r = strchrnul(s, c); return *(unsigned char *)r == ...;` is
  // the motivating case: strchrnul() (include/string.h) is documented
  // and marked exactly this way, and without this, every strchr() call
  // produced an unprovable "not proven nonnull" finding on r for a fact
  // strchrnul()'s own signature already states truthfully. Symmetric
  // with the `nonnull` parameter mechanism: this only trusts a return
  // value for a function this project has itself explicitly annotated,
  // one function at a time, never a blanket relaxation.
  static bool isAlwaysNonNull(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return false;
    if (Function->hasAttr<ReturnsNonNullAttr>())
      return true;
    StringRef Name = Function->getName();
    return Name == "__errno_location" || Name == "__teb" ||
           Name == "localeconv";
  }

  // The strto* family writes either its input pointer or a pointer later in
  // that same string through endptr.  Consequently, whenever endptr itself
  // is supplied, the value written through it cannot be NULL.  Clang's
  // generic invalidation correctly gives the written value a fresh symbol,
  // but does not attach this library contract to that symbol; every ordinary
  // `strtol(s, &end, 10); if (*end) ...` therefore looked like a possible
  // null dereference even though the conversion call itself established the
  // opposite.  Keep this list literal and limited to the standard narrow and
  // wide conversion families whose second argument is endptr.
  static bool writesNonNullEndPointer(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return false;
    StringRef Name = Function->getName();
    static constexpr llvm::StringLiteral Names[] = {
        "strtod",  "strtof",   "strtold", "strtol",  "strtoll",
        "strtoul", "strtoull", "wcstod",  "wcstof",  "wcstold",
        "wcstol",  "wcstoll",  "wcstoul", "wcstoull"};
    for (StringRef Candidate : Names)
      if (Name == Candidate)
        return true;
    return false;
  }

  static bool isLineInputFunction(const CallEvent &Call) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!Function || !Function->getIdentifier())
      return false;
    StringRef Name = Function->getName();
    return Name == "getline" || Name == "getdelim";
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
    if (!Variable || !Variable->getIdentifier() ||
        !Variable->hasGlobalStorage())
      return false;
    StringRef Name = Variable->getName();
    if (Name == "__peb")
      return true;
    // The child table has the same cross-translation-unit invariant as
    // __peb: it starts at the address of the fixed __child_seed array and
    // child_grow() replaces it only after a checked __malloc succeeds.
    // The old allocation is freed before publication of the replacement,
    // but the global itself is never cleared.  Exact-name matching keeps
    // this OS/process-table contract from becoming a general relaxation
    // for arbitrary global pointers.  The name alone is insufficient:
    // require the external-linkage declaration published by libc.h and its
    // canonical `struct __child *` type, so an unrelated file-local or
    // differently-typed variable with the same reserved spelling remains
    // subject to the ordinary proof.
    if (Name != "__children" || !Variable->hasExternalFormalLinkage())
      return false;
    QualType Type = Variable->getType().getCanonicalType();
    if (!Type->isPointerType())
      return false;
    const auto *Record = Type->getPointeeType()->getAs<RecordType>();
    if (!Record)
      return false;
    const auto *Declaration =
        cast<RecordDecl>(Record->getDecl()->getCanonicalDecl());
    return Declaration->isStruct() && Declaration->getIdentifier() &&
           Declaration->getName() == "__child";
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
    const auto *Super = dyn_cast<TypedValueRegion>(Element->getSuperRegion());
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
    SVal Count =
        Builder.makeIntVal(ArrayType->getSize().getZExtValue(), IndexType);
    SVal Below = Builder.evalBinOp(State, BO_LT, *DefinedIndex, Count,
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
      SVal NonNegative = Builder.evalBinOp(State, BO_GE, *DefinedIndex,
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
        collectLinearTerms(SymExprB->getRHS(), Op == BO_Sub ? !Negate : Negate,
                           Terms, Constant);
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
    return Constant >= 0 &&
           static_cast<uint64_t>(Constant) >= static_cast<uint64_t>(Required);
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
    ProgramStateRef State = C.getState();
    bool Changed = false;

    // A successful getline/getdelim call returns the number of bytes read,
    // stores a nonnull buffer through lineptr, and places a terminating NUL
    // immediately after those bytes.  Model that lower bound on the success
    // branch while retaining the untouched failure branch.  Without it,
    // idiomatic `if (len >= 0) line[len]` callers can prove neither the
    // pointer nor its extent even though both are the call's contract.
    if (isLineInputFunction(Call) && Call.getNumArgs() > 0) {
      const auto *Function = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
      std::optional<DefinedOrUnknownSVal> Result =
          Call.getReturnValue().getAs<DefinedOrUnknownSVal>();
      if (Function && Result) {
        SValBuilder &Builder = C.getSValBuilder();
        QualType ReturnTy = Function->getReturnType();
        SVal NonNegative = Builder.evalBinOp(State, BO_GE, *Result,
                                             Builder.makeZeroVal(ReturnTy),
                                             Builder.getConditionType());
        if (std::optional<DefinedOrUnknownSVal> Condition =
                NonNegative.getAs<DefinedOrUnknownSVal>()) {
          auto [Succeeded, Failed] = State->assume(*Condition);
          if (Succeeded) {
            const MemRegion *BufferStorage = Call.getArgSVal(0).getAsRegion();
            SVal Buffer = BufferStorage ? Succeeded->getSVal(BufferStorage)
                                        : UnknownVal();
            if (std::optional<DefinedOrUnknownSVal> DefinedBuffer =
                    Buffer.getAs<DefinedOrUnknownSVal>())
              Succeeded = Succeeded->assume(*DefinedBuffer, true);
            if (Succeeded) {
              const MemRegion *BufferRegion = Buffer.getAsRegion();
              QualType SizeTy = C.getASTContext().getSizeType();
              SVal SizeResult = Builder.evalCast(*Result, SizeTy, ReturnTy);
              SVal Extent =
                  Builder.evalBinOp(Succeeded, BO_Add, SizeResult,
                                    Builder.makeIntVal(1, SizeTy), SizeTy);
              if (BufferRegion) {
                if (std::optional<DefinedOrUnknownSVal> DefinedExtent =
                        Extent.getAs<DefinedOrUnknownSVal>())
                  Succeeded = setDynamicExtent(Succeeded, BufferRegion,
                                               *DefinedExtent, Builder);
              }
              C.addTransition(Succeeded);
            }
          }
          if (Failed)
            C.addTransition(Failed);
          return;
        }
      }
    }

    if (writesNonNullEndPointer(Call) && Call.getNumArgs() > 1) {
      const MemRegion *EndStorage = Call.getArgSVal(1).getAsRegion();
      if (EndStorage &&
          !State->isNull(Call.getArgSVal(1)).isConstrainedTrue()) {
        SVal EndValue = State->getSVal(EndStorage);
        if (std::optional<DefinedOrUnknownSVal> Defined =
                EndValue.getAs<DefinedOrUnknownSVal>()) {
          if (ProgramStateRef NonNull = State->assume(*Defined, true)) {
            State = NonNull;
            Changed = true;
          }
        }
      }
    }

    if (isAlwaysNonNull(Call)) {
      if (std::optional<DefinedOrUnknownSVal> Defined =
              Call.getReturnValue().getAs<DefinedOrUnknownSVal>()) {
        if (ProgramStateRef NonNull = State->assume(*Defined, true)) {
          State = NonNull;
          Changed = true;
        }
      }
    }

    if (Changed)
      C.addTransition(State);
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
      if (Kind && *Kind == OwnershipKind::Consumed &&
          !insideDynamicStorageConsumer(C)) {
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
          report("dereference alignment is not proven valid", Statement, State,
                 C);
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
    SVal BaseExtent =
        getDynamicExtent(State, Region->getBaseRegion(), C.getSValBuilder());
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
        report("dereference extent is not proven sufficient", Statement, State,
               C);
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
        {"NtCreateFile", 0},
        {"NtOpenFile", 0},
        {"NtCreateEvent", 0},
        {"NtCreateSemaphore", 0},
        {"NtOpenSemaphore", 0},
        {"NtCreateMutant", 0},
        {"NtCreateThreadEx", 0},
        {"NtOpenProcess", 0},
        {"NtCreateJobObject", 0},
        {"NtCreateSection", 0},
        {"NtCreateNamedPipeFile", 0},
        {"NtCreateTimer", 0},
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

  // A resource passed directly as a function parameter was acquired by
  // the caller, outside this per-function analysis.  Most scalar resource
  // parameters retain a SymbolRef and are handled by the absent-ResourceMap
  // branch below, but opaque pointer resources such as NT HANDLE can be
  // represented as a region value with no recoverable symbol.  That
  // representation difference must not turn the same borrowed-resource
  // contract into a fabricated-resource finding.  Keep this deliberately
  // direct: values loaded from globals, fields, arrays, or arbitrary
  // expressions still go through the ordinary proof logic.
  static bool isDirectParameterArgument(const CallEvent &Call,
                                        unsigned Argument) {
    const Expr *ArgExpr = Call.getArgExpr(Argument);
    if (!ArgExpr)
      return false;
    const auto *Ref = dyn_cast<DeclRefExpr>(ArgExpr->IgnoreParenCasts());
    return Ref && isa<ParmVarDecl>(Ref->getDecl());
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
      if (isDirectParameterArgument(Call, Argument)) {
        if (Consume && Symbol)
          C.addTransition(
              C.getState()->set<ResourceMap>(Symbol, released(Expected)));
        return;
      }
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

void registerAllocationLifetimeChecker(CheckerRegistry &Registry);

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

extern "C" void clang_registerCheckers(CheckerRegistry &Registry) {
  registerAllocationLifetimeChecker(Registry);
  Registry.addChecker<OwnershipChecker>(
      "ntlibc.Ownership",
      "Proves allocator provenance and borrow lifetime at deallocation", "");
  Registry.addChecker<OwnedConstructChecker>(
      "ntlibc.OwnedConstruct",
      "Proves synchronization object construction and destruction", "");
  Registry.addChecker<OwnershipContractChecker>(
      "ntlibc.OwnershipContract",
      "Requires source definitions to repeat header ownership contracts", "");
  Registry.addChecker<CapabilityTokenChecker>(
      "ntlibc.CapabilityToken",
      "Proves explicit linear and duplicable ownership-token transitions", "");
  Registry.addChecker<OwnershipTypeChecker>(
      "ntlibc.OwnershipType",
      "Proves ownership token bundles across value and storage types", "");
  Registry.addChecker<ValidPointerChecker>(
      "ntlibc.ValidPointer",
      "Proves every memory access has a nonnull, live, in-bounds, aligned "
      "pointer",
      "");
  Registry.addChecker<ResourceLifecycleChecker>(
      "ntlibc.Resource", "Proves acquire, use, and release resource lifecycles",
      "");
}
