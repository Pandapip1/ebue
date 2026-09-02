// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TokenAlgebra.h"

#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace ntlibc::algebra;

static bool require(bool Condition, const char *Message) {
  if (!Condition)
    std::fprintf(stderr, "token-algebra-test: %s\n", Message);
  return Condition;
}

int main() {
  constexpr const char *Source = R"(
typedef struct { char byte; } dynamic_token
  __attribute__((annotate("qual:dynamic_storage")));
typedef struct { char byte; } wrong_qualifier
  __attribute__((annotate("qual:dynamic_storage_extra")));
int value_only __attribute__((annotate("qual:dynamic_storage")));
typedef struct { char byte; } malformed_word
  __attribute__((annotate("qual:sentinel_exclude=not-a-number")));
typedef struct { char byte; } malformed_overflow
  __attribute__((annotate("qual:sentinel_exclude=9223372036854775808")));
typedef struct { char byte; } null_sentinel
  __attribute__((annotate("qual:sentinel_exclude=NULL")));
typedef struct { char byte; } minimum_sentinel
  __attribute__((annotate("qual:sentinel_exclude=-9223372036854775808")));
typedef struct { char byte; } maximum_sentinel
  __attribute__((annotate("qual:sentinel_exclude=9223372036854775807")));
)";
  std::unique_ptr<clang::ASTUnit> AST =
      clang::tooling::buildASTFromCodeWithArgs(
          Source, std::vector<std::string>{"-xc", "-std=c11"},
          "token-algebra-fixture.c");
  if (!require(AST != nullptr, "failed to parse fixture"))
    return 1;
  clang::ASTContext &Context = AST->getASTContext();
  const TokenSort *Dynamic = findTokenSort(Context, "dynamic_token");
  bool Passed = true;
  Passed &= require(Dynamic != nullptr, "nominal token typedef not found");
  Passed &= require(hasQualifier(Dynamic, "qual:dynamic_storage"),
                    "exact qualifier not found");
  Passed &= require(!hasQualifier(findTokenSort(Context, "wrong_qualifier"),
                                  "qual:dynamic_storage"),
                    "prefix lookalike accepted as qualifier");
  Passed &= require(findTokenSort(Context, "value_only") == nullptr,
                    "annotation on non-typedef created a token sort");
  Passed &= require(!excludedSentinel(
                         findTokenSort(Context, "malformed_word")),
                    "nonnumeric sentinel accepted");
  Passed &= require(!excludedSentinel(
                         findTokenSort(Context, "malformed_overflow")),
                    "out-of-range sentinel accepted");
  Passed &= require(excludedSentinel(
                         findTokenSort(Context, "null_sentinel")) == 0,
                    "NULL sentinel did not denote zero");
  Passed &= require(
      excludedSentinel(findTokenSort(Context, "minimum_sentinel")) ==
          std::numeric_limits<int64_t>::min(),
      "minimum signed sentinel was not preserved");
  Passed &= require(
      excludedSentinel(findTokenSort(Context, "maximum_sentinel")) ==
          std::numeric_limits<int64_t>::max(),
      "maximum signed sentinel was not preserved");
  return Passed ? 0 : 1;
}
